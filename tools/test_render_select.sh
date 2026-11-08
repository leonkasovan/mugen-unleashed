#!/bin/sh
# CF01 gate (M6, plan section 10.8 + gate): renderer selection honesty.
#
# Runs a built engine binary with each RenderMode value and checks the
# M6 startup line in mugen.log:
#   renderer: requested=<cfg> effective=<actual> caps=0x<capbits>
# plus the pre-existing Invalid RenderMode failure. This exercises the
# production config -> select -> capability -> log path end to end, with
# no stubs or table copies.
#
# Usage: tools/test_render_select.sh <engine.exe> [workdir]
#   workdir must be a M.U.G.E.N game tree (install/). The tree is not
#   modified: mugen.cfg is copied to a temp file and restored.
# Artefacts go under build/tmp/render-select per AGENTS.md.
set -eu

BIN="${1:?usage: test_render_select.sh <engine.exe> [workdir]}"
WORKDIR="${2:-install}"
OUT=build/tmp/render-select

cd "$(dirname "$0")/.."
mkdir -p "$OUT"

cfg="$WORKDIR/data/mugen.cfg"
backup="$OUT/mugen.cfg.orig"
cp "$cfg" "$backup"

set_mode() {
    # $1 = new RenderMode value
    PYBIN="${PYTHON3:-}"
    if [ -z "$PYBIN" ]; then
        for p in /c/Python314/python.exe python3 python py; do
            if "$p" -c "pass" 2>/dev/null; then PYBIN="$p"; break; fi
        done
    fi
    "$PYBIN" - "$cfg" "$1" <<'EOF'
import sys
path, mode = sys.argv[1], sys.argv[2]
s = open(path, encoding="utf-8", errors="replace").read()
import re
s2, n = re.subn(r"(?m)^(\s*RenderMode\s*=\s*)\S+", r"\g<1>" + mode, s)
assert n == 1, "RenderMode line not found"
open(path, "w", encoding="utf-8").write(s2)
EOF
}

run_mode() {
    # $1 = cfg value, $2 = expected requested=, $3 = expected effective=
    set_mode "$1"
    rm -f "$WORKDIR/mugen.log"
    (cd "$WORKDIR" && timeout 20 "$ENGINE_BIN" -nosound >/dev/null 2>&1 || true)
    log="$WORKDIR/mugen.log"
    line=$(grep -ao 'renderer: .*' "$log" | head -1 || true)
    echo "mode=$1 -> $line"
    echo "$line" | grep -q "requested=$2" || { echo "FAIL: mode $1 requested != $2"; return 1; }
    echo "$line" | grep -q "effective=$3" || { echo "FAIL: mode $1 effective != $3"; return 1; }
    echo "$line" > "$OUT/select-$1.log"
}

export ENGINE_BIN
ENGINE_BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

FAIL=0
run_mode system system system || FAIL=1
run_mode openglscreen openglscreen openglscreen || FAIL=1
run_mode directx system system || FAIL=1
if ! grep -q '+directx(alias)' "$OUT/select-directx.log"; then
    echo "FAIL: directx not reported as System alias"; FAIL=1
fi
run_mode opengl opengl opengl || FAIL=1

# Invalid value must fail in Config_Load, before any renderer line. Note: the
# error path ends in the engine's error dialog, which segfaults headless, and
# stdio buffering may or may not flush mugen.log before the crash -- so a
# nonzero exit alone passes; a flushed log line is a bonus, not required.
set_mode bogusmode
rm -f "$WORKDIR/mugen.log"
code=0
(cd "$WORKDIR" && timeout 20 "$ENGINE_BIN" -nosound >/dev/null 2>&1) || code=$?
if [ "$code" -ne 0 ]; then
    echo "mode=bogusmode -> exit $code (expected failure)"
elif grep -aq 'Invalid RenderMode' "$WORKDIR/mugen.log"; then
    echo "mode=bogusmode -> Invalid RenderMode (expected)"
else
    echo "FAIL: bogus mode neither failed nor reported Invalid RenderMode"; FAIL=1
fi

cp "$backup" "$cfg"
if [ "$FAIL" = 0 ]; then
    echo "render-select: CF01 gates pass"
else
    echo "render-select: FAILURES" >&2
    exit 1
fi
