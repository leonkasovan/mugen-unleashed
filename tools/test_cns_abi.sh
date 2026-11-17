#!/bin/sh
# Pure host CNS/controller tests. No SDL library or window is needed: the
# SDL2 declarations supply types; the harness supplies explicit lock/mixer stubs.
# CC/CFLAGS/LDFLAGS select a native or cross compiler; RUNNER can run a cross
# binary (for example qemu-i386-static). SANITIZE=1 enables ASan/UBSan.
set -eu
cd "$(dirname "$0")/.."
TARGET_FLAGS=""
if [ -n "${MUGEN_TEST_TARGET:-}" ]; then
    . tools/lib_target.sh
    TARGET_FLAGS="$ARCHFLAGS -DTEST_EXPECT_BITS=$EXPECT_BITS"
fi
CC="${CC:-cc}"
OUT="${OUT:-$PWD/build/tmp/cns-abi-test}"
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"
FLAGS="$TARGET_FLAGS ${CFLAGS:-} -DMUGEN_MEMPROF_NO_WRAP=1 -std=gnu11 -O1 -g -UNDEBUG -ffunction-sections -fdata-sections"
FLAGS="$FLAGS -fno-asynchronous-unwind-tables -DSDL_MAIN_HANDLED"
FLAGS="$FLAGS -Isrc ${SDL_CFLAGS:-$(${PKG_CONFIG:-pkg-config} --cflags sdl2)} -idirafter third_party/sdk/include"
FLAGS="$FLAGS -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15 -Ithird_party/libpng-1.2.38"
FLAGS="$FLAGS -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include"
# Some PE linkers keep references from unused functions in the included
# engine units despite gc-sections. Eliminate them before the MinGW link.
case "$($CC -dumpmachine)" in
    *mingw*) FLAGS="$FLAGS -flto=auto" ;;
esac
ZSS_SOURCES=""
ZSS_LEAF=""
if [ "${TEST_ZSS_CONTROLLER:-0}" = 1 ]; then
    FLAGS="$FLAGS -DTEST_ZSS_CONTROLLER"
    LDFLAGS="${LDFLAGS:-} -Wl,--wrap=calloc -Wl,--wrap=free"
    ZSS_SOURCES="src/ini.c src/scriptparams.c src/zss_controller.c src/zss_lex.c src/zss_params.c src/zss_statedef.c src/zss_load.c"
    # Installed-function tests use expression statements; the game/AI leaf
    # adapter is an aborting boundary, not a link of every game subsystem.
    ZSS_LEAF="-DStateCtrl_RunCtx=Test_ZssBoundLeaf"
fi
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then
    SAN="-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer"
fi
# globals.c contains callback tables for subsystems outside this host test;
# leave them uninstrumented so gc-sections can discard the unused tables.
# shellcheck disable=SC2086
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
# The harness injects allocation failures in its own Mem_Calloc. Keep the real
# string helpers while renaming util.c's allocator to avoid a duplicate symbol.
# shellcheck disable=SC2086
$CC $FLAGS $SAN -DMem_Calloc=Test_UtilMem_Calloc -c src/util.c -o "$OUT/util.o"
# shellcheck disable=SC2086
$CC $FLAGS $SAN $ZSS_LEAF -c src/zss_program.c -o "$OUT/zss_program.o"
# shellcheck disable=SC2086
$CC $FLAGS $SAN tools/test_cns_abi.c $ZSS_SOURCES src/zss_assert.c src/zss_map.c src/cnsparse.c src/keyedarray.c src/compat_linux.c \
    "$OUT/globals.o" "$OUT/util.o" "$OUT/zss_program.o" -Wl,--gc-sections ${LDFLAGS:-} -lm -o "$OUT/test_cns_abi"
# shellcheck disable=SC2086
${RUNNER:-} "$OUT/test_cns_abi"
if [ "${TEST_ZSS_CONTROLLER:-0}" = 1 ]; then
    # The isolated resource-boundary harness does not use allocator wrappers.
    # shellcheck disable=SC2086
    $CC $FLAGS $SAN tools/test_zss_loading.c -Wl,--gc-sections -lm -o "$OUT/test_zss_loading"
    # shellcheck disable=SC2086
    ${RUNNER:-} "$OUT/test_zss_loading"
    # Real input decision logic with recording command/state-entry boundaries.
    # No allocator wrappers are needed: assertion storage is allocation-free.
    # shellcheck disable=SC2086
    $CC $FLAGS $SAN tools/test_zss_input.c src/zss_assert.c "$OUT/globals.o" \
        -Wl,--gc-sections -lm -o "$OUT/test_zss_input"
    # shellcheck disable=SC2086
    ${RUNNER:-} "$OUT/test_zss_input"
fi
