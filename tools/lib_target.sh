#!/bin/sh
# Shared target selection for the render/qualification harnesses.
#
# RENDERING_PLAN.md 13.2 and the M9 review: a harness run for `win64` must
# actually build and run a 64-bit binary with the 64-bit kit. Duplicating that
# logic in every script let it drift -- four harnesses kept a hardcoded -m32
# and silently produced 32-bit evidence under --target win64.
#
# Sourced (not executed) by:
#   test_render_host.sh test_render_regress.sh test_render_video.sh
#   test_gl_mvp.sh test_render_policy.sh test_gles_shader.sh test_vita_caps.sh
#
# On return it sets:
#   TARGET        win32 | win64 (from MUGEN_TEST_TARGET, default win32)
#   CC            compiler for that target
#   ARCHFLAGS     -m32 | -m64
#   EXPECT_BITS   32 | 64      (harnesses assert the built binary matches)
#   PKGCONF       pkg-config belonging to the same kit
#   PLAT_INC      extra include dir for the 32-bit kit's SDL 1.2 headers
#
# It also prepends the chosen compiler's own directory to PATH: the kits' gcc
# wrappers need their bin dir first to find cc1/as for the right target, and an
# inherited assembler from the other kit rejects the other --32/--64.

TARGET="${MUGEN_TEST_TARGET:-win32}"

case "$TARGET" in
win32)
    CC="${CC:-/c/x86devkit/bin/gcc}"
    ARCHFLAGS="-m32"
    EXPECT_BITS=32
    PKGCONF="${PKG_CONFIG:-/c/x86devkit/bin/pkg-config}"
    PLAT_INC="-Ithird_party/sdk/include"
    # The kit's pkg-config must be the one answering. An inherited
    # PKG_CONFIG_PATH from an MSYS2 MINGW64 shell points at C:/msys64/mingw64,
    # whose .pc files then supply headers and libraries for the *other* ABI:
    # the 32-bit link died on "libmingw32.a: file format not recognized" and
    # the 64-bit link mixed msys64 headers with the kit's libraries.
    if [ -z "${MUGEN_TEST_KEEP_PKG_CONFIG_PATH:-}" ]; then
        unset PKG_CONFIG_PATH
    fi
    ;;
win64)
    CC="${CC:-/c/w64devkit/bin/gcc}"
    ARCHFLAGS="-m64"
    EXPECT_BITS=64
    PKGCONF="${PKG_CONFIG:-/c/w64devkit/bin/pkg-config}"
    PLAT_INC=""
    # Same rule as win32: never let an inherited path supply the other ABI's
    # SDL headers. The kit's own pkg-config knows its prefix.
    if [ -z "${MUGEN_TEST_KEEP_PKG_CONFIG_PATH:-}" ]; then
        unset PKG_CONFIG_PATH
    fi
    ;;
*)
    echo "unsupported MUGEN_TEST_TARGET $TARGET (win32|win64)" >&2
    exit 2
    ;;
esac

# Prepend the directory of the chosen compiler so its cc1/as win.
case "$CC" in
*/*) _cc_dir=${CC%/*} ;;
*) _cc_dir=$(command -v "$CC") ; _cc_dir=${_cc_dir%/*} ;;
esac
case ":$PATH:" in
"$_cc_dir:"*) ;;
*) PATH="$_cc_dir:$PATH"; export PATH ;;
esac


# Runs a built harness and enforces the target's pointer width.
#
# Prints the harness output, fails when it reports a different width than the
# target asked for (a harness that silently stayed -m32 must not produce
# evidence for win64), and passes the harness exit status through unchanged so
# 77 still means SKIP to tools/test_qualify.sh.
run_harness() {
    _rh_out=""
    _rh_rc=0
    _rh_out=$("$@") || _rh_rc=$?
    printf '%s\n' "$_rh_out"
    if printf '%s\n' "$_rh_out" | grep -q '^abi: '; then
        if ! printf '%s\n' "$_rh_out" | grep -q "^abi: $EXPECT_BITS-bit build$"; then
            echo "FAIL: harness is not a $EXPECT_BITS-bit build" >&2
            return 1
        fi
    fi
    return "$_rh_rc"
}
