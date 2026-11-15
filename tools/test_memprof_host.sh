#!/bin/sh
# Standalone Linux regression tests for the real src/memprof.c implementation.
# No SDL or full game build required. Run: sh tools/test_memprof_host.sh
set -eu
cd "$(dirname "$0")/.."
temp_dir="$(mktemp -d)"
trap 'rm -rf "$temp_dir"' EXIT HUP INT TERM

for flavor in nonpie pie pic_nonpie; do
    case "$flavor" in
        nonpie) flags="-fno-pie -no-pie" ;;
        pie) flags="-fPIE -pie" ;;
        pic_nonpie) flags="-fPIC -no-pie" ;;
    esac
    binary="$temp_dir/memprof_$flavor"
    gcc -std=c11 -D_POSIX_C_SOURCE=200809L -g -O2 -fno-omit-frame-pointer \
        -Wall -Wextra $flags tools/test_memprof_host.c -o "$binary"
    result="$(cd "$temp_dir" && "$binary")"
    printf '%s: %s\n' "$flavor" "$(printf '%s\n' "$result" | tail -1)"
    pc="$(printf '%s\n' "$result" | sed -n 's/^symbol_addr=\(0x[0-9a-fA-F]*\)$/\1/p')"
    [ -n "$pc" ] || { echo "missing ELF address: $flavor" >&2; exit 1; }
    symbol="$(addr2line -f -e "$binary" "$pc" | head -1)"
    [ "$symbol" = "memprof_test_marker" ] || {
        echo "ELF symbolization failed for $flavor: $symbol" >&2
        exit 1
    }
done
echo "PASS: Linux memory profiler (non-PIE, PIE, PIC/non-PIE)"
