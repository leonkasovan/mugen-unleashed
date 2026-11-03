#!/bin/sh
# M9 qualification runner (RENDERING_PLAN.md section 13).
#
# Maps each test ID to the harness that implements it and stores evidence
# under build/render-validation/<sha>/<target>/<mode>/ (plan section 13.3).
# Test IDs without an implemented harness are reported as OPEN with the
# reason -- never as pass (plan section 13.4).
#
# Usage: tools/test_qualify.sh [--target win32|win64] [--engine <exe>]
#   --target win32 (default): host gates + Win32 SDL2 E2E log comparison.
#   --target win64: Win64 layout + E2E log comparison (needs w64devkit).
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

SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
OUT="build/render-validation/$SHA/$TARGET"
mkdir -p "$OUT"

PASS=0
FAIL=0
OPEN=""

record() {
    # $1 = id, $2 = status (PASS/FAIL/OPEN), $3 = detail
    printf '%-8s %-4s %s\n' "$1" "$2" "$3" | tee -a "$OUT/results.txt"
    case "$2" in
    PASS) PASS=$((PASS + 1)) ;;
    FAIL) FAIL=$((FAIL + 1)) ;;
    OPEN) OPEN="$OPEN $1" ;;
    esac
}

echo "# M9 qualification: $TARGET @ $SHA" | tee "$OUT/results.txt"
echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee -a "$OUT/results.txt"

run_host() {
    # $1 = id, $2... = command
    id="$1"; shift
    if "$@" >"$OUT/$id.log" 2>&1; then
        record "$id" PASS "see $id.log"
    else
        record "$id" FAIL "see $id.log"
    fi
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
run_host VT01-caps bash tools/test_vita_caps.sh
run_host GL-MVP bash tools/test_gl_mvp.sh
run_host CF01-policy bash tools/test_render_policy.sh
if [ -n "$ENGINE" ] && [ -x "$ENGINE" ]; then
    run_host CF01 bash tools/test_render_select.sh "$ENGINE" install
    rm -f install/mugen.log
else
    record CF01 OPEN "needs a built Win32 SDL2 engine binary (--engine <exe>)"
fi

echo "== gates requiring a device or GPU context =="
record PR02 OPEN "suspend/resume + expose need a live window session (harness: manual)"
record SW02 OPEN "PalFX/shear/rotate/Q golden images need GPU reference captures"
record FT01 OPEN "font palette/alpha comparison needs TTF fixture runs"
record VT01-device OPEN "V1/V2 logic gates pass on host (VT01-caps); device probe needs the console"
record VT02-device OPEN "V3/V4 shader+blend gates pass structurally; pixel parity needs the console"
record ES01-device OPEN "needs GLES device (M8 draw path not yet landed)"
record E2E01 OPEN "menu/select/match/screenshot matrix needs scripted runs"

echo "== layout evidence =="
bash tools/test_abi_layout.sh >"$OUT/layout.log" 2>&1 && record LAYOUT PASS "see layout.log" || record LAYOUT FAIL "see layout.log"

echo "== summary: PASS=$PASS FAIL=$FAIL OPEN:$OPEN ==" | tee -a "$OUT/results.txt"
[ "$FAIL" = 0 ]
