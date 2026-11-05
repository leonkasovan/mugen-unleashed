#!/bin/sh
# M9 qualification runner (RENDERING_PLAN.md section 13).
#
# Maps each test ID to the harness that implements it and stores evidence
# under build/render-validation/<sha>/<target>/ (plan section 13.3).
# Test IDs without an implemented harness are reported as OPEN with the
# reason -- never as pass (plan section 13.4).
#
# Usage: tools/test_qualify.sh [--target win32|win64] [--engine <exe>]
#   --target win32 (default): i686 kit, -m32 harnesses, Win32 SDL2 E2E.
#   --target win64: w64devkit, -m64 harnesses and a Win64 E2E binary. The
#     target is passed to the harnesses as MUGEN_TEST_TARGET, which changes
#     the compiler, the bitness and the dependency kit -- it is not just an
#     evidence directory name. A harness whose built pointer width does not
#     match the target fails (tools/test_render_host.sh checks it).
# A harness that cannot run here exits 77 and is recorded as SKIP, which is
# listed with the OPEN rows; an exit 0 alone is never evidence of a pass.
# Artefacts go under build/render-validation per AGENTS.md.
set -eu

cd "$(dirname "$0")/.."

TARGET="win32"
ENGINE=""
while [ $# -gt 0 ]; do
    case "$1" in
    --target) TARGET="$2"; shift 2;;
    --engine) ENGINE="$2"; shift 2;;
    *) shift;;
    esac
done

case "$TARGET" in
win32|win64) ;;
*) echo "unsupported --target $TARGET (win32|win64)" >&2; exit 2;;
esac
export MUGEN_TEST_TARGET="$TARGET"

SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
OUT="build/render-validation/$SHA/$TARGET"
mkdir -p "$OUT"

PASS=0
FAIL=0
SKIP=0
OPEN=""

record() {
    # $1 = id, $2 = status (PASS/FAIL/SKIP/OPEN), $3 = detail
    printf '%-8s %-4s %s\n' "$1" "$2" "$3" | tee -a "$OUT/results.txt"
    case "$2" in
    PASS) PASS=$((PASS + 1)) ;;
    FAIL) FAIL=$((FAIL + 1)) ;;
    SKIP) SKIP=$((SKIP + 1)); OPEN="$OPEN $1" ;;
    OPEN) OPEN="$OPEN $1" ;;
    esac
}

echo "# M9 qualification: $TARGET @ $SHA" | tee "$OUT/results.txt"
echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee -a "$OUT/results.txt"

# Whose compiler, whose kit: the evidence has to say what produced it.
{
    echo "target: $TARGET"
    echo "host gcc: $(gcc -dumpmachine 2>/dev/null || echo unknown) $(gcc -dumpversion 2>/dev/null || echo '')"
    echo "x86devkit: $(/c/x86devkit/bin/gcc -dumpmachine 2>/dev/null || echo absent)"
    echo "w64devkit: $(/c/w64devkit/bin/gcc -dumpmachine 2>/dev/null || echo absent)"
} >"$OUT/toolchain.txt"
cat "$OUT/toolchain.txt" | tee -a "$OUT/results.txt"

run_host() {
    # $1 = id, $2... = command. 0 is PASS, 77 is SKIP (the harness could not
    # run here), anything else is FAIL.
    id="$1"; shift
    rc=0
    "$@" >"$OUT/$id.log" 2>&1 || rc=$?
    case "$rc" in
    0) record "$id" PASS "see $id.log" ;;
    77) record "$id" SKIP "harness skipped, see $id.log" ;;
    *) record "$id" FAIL "exit $rc, see $id.log" ;;
    esac
}

echo "== host gates (no GPU) =="
run_host TX01 bash tools/test_render_regress.sh
run_host PR01 bash tools/test_render_regress.sh
run_host R03 bash tools/test_render_regress.sh
run_host ABI01 bash tools/test_render_host.sh
run_host ABI02 bash tools/test_render_host.sh
run_host SW01 bash tools/test_render_host.sh
run_host R04 bash tools/test_render_host.sh
run_host R05 bash tools/test_render_host.sh
run_host R02 bash tools/test_render_video.sh
run_host GL01 bash tools/test_render_video.sh
run_host ES01-struct bash tools/test_gles_shader.sh
# Portable-module compile gate: the Android input path shares src/pads.c with
# the desktop, and a change that only compiled under -DANDROID_PORT broke the
# desktop SDL2 build. This compiles the shared modules under every backend.
run_host BUILD01 bash tools/test_compile_gates.sh
# GLES blend factors vs the engine equation (BLEND01).
run_host BLEND01 bash tools/test_blend_math.sh
# GLES clip rect must reach the GL renderer (CLIP01).
run_host CLIP01 bash tools/test_clip_gate.sh
# GLES primitive colour/blend vs prim.c (PRIM01).
run_host PRIM01 bash tools/test_prim_math.sh
run_host VT01-caps bash tools/test_vita_caps.sh
# PS Vita bubble icon derived from res/icons/*.ico (VT03-icon).
run_host VT03-icon bash tools/test_vita_icon.sh
# SDL2 desktop backend: the audio paths a -nosound run never reaches
# (SDL2A01). This harness was cited as evidence in SDL2_PLAN.md while it did
# not link at all -- Bgm_MutexPtr moved into globals.c in 122e0ee and nothing
# ran it, so nobody noticed. A gate is the fix.
run_host SDL2A01 bash tools/test_sdl2audio.sh
# CNS limits, hotplug queue and renderer font/rotation lifetime.
run_host REVIEW01 bash tools/test_review_regress.sh
run_host GL-MVP bash tools/test_gl_mvp.sh
run_host CF01-policy bash tools/test_render_policy.sh
if [ -n "$ENGINE" ] && [ -x "$ENGINE" ]; then
    run_host CF01 bash tools/test_render_select.sh "$ENGINE" install
    rm -f install/mugen.log
else
    record CF01 OPEN "needs a built $TARGET SDL2 engine binary (--engine <exe>)"
fi

echo "== gates requiring a device or GPU context =="
record PR02 OPEN "suspend/resume + expose need a live window session (harness: manual)"
record SW02 OPEN "PalFX/shear/rotate/Q golden images need GPU reference captures"
record FT01 OPEN "font palette/alpha comparison needs TTF fixture runs"
record VT01-device OPEN "V1/V2 logic gates pass on host (VT01-caps); device probe needs the console"
record VT02-device OPEN "V3/V4 shader+blend gates pass structurally; pixel parity needs the console"
# The GLES draw path landed (85048b6) and renders on hardware; this runner is
# host-only, so the device half of ES01 stays OPEN here. The evidence that does
# exist is a recorded device run, not a pass from this script.
record ES01-device OPEN "host runner cannot reach a GPU; device run recorded in port/android/README.md (5.3)"
record E2E01 OPEN "menu/select/match/screenshot matrix needs scripted runs"

echo "== layout evidence =="
bash tools/test_abi_layout.sh >"$OUT/layout.log" 2>&1 && record LAYOUT PASS "see layout.log" || record LAYOUT FAIL "see layout.log"

echo "== summary: PASS=$PASS FAIL=$FAIL SKIP=$SKIP OPEN:$OPEN ==" | tee -a "$OUT/results.txt"
[ "$FAIL" = 0 ]
