#!/bin/sh
# Measure the Memprof pointer side table at different bucket counts.
#
# src/memprof.c keeps a fixed MEMPROF_BUCKETS (1024) pointer side table and
# hashes on pointer bits. An on-device profile of the HyperDBZ pack showed
# Memprof_FindEntryLocked at 11.7% of samples with ~99,000 live allocations,
# i.e. an average chain of ~97 entries walked on every malloc and every free.
#
# This builds tools/test_memprof_buckets.c against src/memprof.c once per bucket
# count and reports the churn cost, so the effect is measured rather than
# asserted. Host-side and deterministic: no driver, no vsync, no frame skipping.
#
#   sh tools/test_memprof_buckets.sh
#   BUCKETS="1024 65536 262144" LIVE=99000 sh tools/test_memprof_buckets.sh
#
# For the handheld, cross-compile the same benchmark and run it there -- the
# effect is much larger on the device (cache-miss bound) than on a desktop:
#
#   aarch64-none-linux-gnu-gcc -std=gnu11 -O2 -DSDL_MAIN_HANDLED -static \
#     -Isrc -Ithird_party/glad-es-3.0 -Ithird_party/minizip-0.15 \
#     -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include \
#     -Ibuild/sysroots/win32-sysroot/usr/include/SDL2 \
#     -Ibuild/sysroots/win32-sysroot/usr/include \
#     tools/test_memprof_buckets.c build/tmp/memprof-<N>.c -o bench-<N>
#
# (the SDL/zlib headers are only needed because mugen.h includes them; this
# benchmark uses neither, so the host-neutral headers are fine for a cross build)
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/tmp/test_memprof_buckets"
CC="${CC:-cc}"
BUCKETS="${BUCKETS:-1024 4096 16384 65536 262144}"
LIVE="${LIVE:-99000}"
CHURN="${CHURN:-200000}"

mkdir -p "$OUT"

# The include set src/memprof.c needs to compile on its own. memprof.c includes
# mugen.h, which pulls in glad/SDL/minizip/lua headers even though this
# translation unit uses none of them.
INC="-I$ROOT/src"
INC="$INC -I$ROOT/third_party/glad-2.1"
INC="$INC -I$ROOT/third_party/minizip-0.15"
INC="$INC -I$ROOT/third_party/lua-5.1.4/src"
INC="$INC -I$ROOT/third_party/libresample/include"
# SDL headers are needed only because mugen.h includes <SDL.h>; this benchmark
# uses nothing from SDL. Prefer pkg-config (a host-correct answer), then the
# host's own sysroot. Never the cross trees: their SDL_config.h dispatches to
# SDL2/_real_SDL_config.h, which is a cross-build artefact and will not compile
# for the host.
SDL_INC=""
if command -v pkg-config >/dev/null 2>&1; then
    SDL_INC=$(pkg-config --cflags-only-I sdl2 2>/dev/null || true)
fi
if [ -z "$SDL_INC" ]; then
    for d in "$ROOT"/build/sysroots/*-sysroot/usr/include/SDL2; do
        [ -d "$d" ] || continue
        case "$d" in
            *arm64*|*aarch64*) continue ;;   # cross tree, not for the host
        esac
        SDL_INC="-I$d"
        break
    done
fi
INC="$INC $SDL_INC"

printf '%-10s %-12s %-12s %s\n' buckets "drain_total" "unit" "note"
printf '%-10s %-12s %-12s %s\n' ------- ----------- -------- ----

for n in $BUCKETS; do
    src="$OUT/memprof-$n.c"
    bin="$OUT/bench-$n"
    # Rewrite only the bucket constant; everything else is the real source.
    sed "s/^#define MEMPROF_BUCKETS .*/#define MEMPROF_BUCKETS $n/" \
        "$ROOT/src/memprof.c" > "$src"
    grep -q "^#define MEMPROF_BUCKETS $n$" "$src" || {
        echo "error: could not rewrite MEMPROF_BUCKETS in src/memprof.c" >&2
        exit 1
    }
    $CC -std=gnu11 -O2 -DSDL_MAIN_HANDLED $INC \
        "$ROOT/tools/test_memprof_buckets.c" "$src" -o "$bin"

    # Three runs, keep the fastest: the fastest is the least-perturbed sample.
    #
    # The metric is `drain`, not `churn`. Churn allocates and immediately frees,
    # and insert is at the chain head, so that free always finds its entry at
    # depth 1 and the cost is O(1) whatever the bucket count -- it measures the
    # CRT, not the table. Draining the live set frees oldest-first, so each free
    # walks past every later entry that landed in the same bucket. That is the
    # chain length, and it is what an engine freeing long-lived objects pays.
    best=""
    for _ in 1 2 3; do
        # drain <n> frees  <ms> ms  ( <us> us each )
        line=$("$bin" "$LIVE" "$CHURN" | awk '/^drain/ {print $4}')
        if [ -z "$best" ] || awk "BEGIN{exit !($line < $best)}"; then
            best=$line
        fi
    done
    table_kib=$(awk "BEGIN{printf \"%.0f\", $n * 8 / 1024}")
    printf '%-10s %-12.2f %-12s static table %s KiB\n' "$n" "$best" "ms" "$table_kib"
done

echo
echo "Lower drain_total is better. 1024 is the shipped value."
echo "Static table is buckets * sizeof(pointer) in .bss, present for the whole run."
