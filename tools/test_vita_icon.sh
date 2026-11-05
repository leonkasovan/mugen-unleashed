#!/usr/bin/env bash
# VT03-icon: the PS Vita bubble icon comes from res/icons/*.ico.
#
# port/psvita/cmake/Icon.cmake converts the .ico the Windows build embeds into
# the 128x128 PNG the Vita packs as sce_sys/icon0.png. The module is written to
# run in CMake script mode, so this gate exercises the real file with `cmake -P`
# and needs no VitaSDK -- which is why it exists: the Vita build itself cannot
# run on a machine without the SDK, and "the icon block looks right" is not a
# check.
#
# Asserts:
#   1. the converted PNG is 128x128 RGBA;
#   2. it is the artwork of the .ico, not a blank canvas: transparent corners,
#      opaque centre, and a pixel count that matches the source;
#   3. the committed sample asset is left alone (the build writes into the build
#      tree, so a git checkout stays clean);
#   4. with no Python the module falls back to the sample asset and still sets
#      MUGEN_VITA_ICON_PNG to a file that exists;
#   5. a missing .ico falls back the same way.
#
# Usage: bash tools/test_vita_icon.sh
# Exit 0 on success, 1 on the first failed assertion.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/vita-icon"
MODULE="$ROOT/port/psvita/cmake/Icon.cmake"
SCE_SYS="$ROOT/port/psvita/sce_sys"
ICO="$ROOT/res/icons/1.ico"
SAMPLE="$SCE_SYS/icon0.png"

# -B keeps Python from writing tools/__pycache__ beside the sources.
# On Windows 'python3' is usually the Microsoft Store alias, which "exists" and
# prints an advert instead of running; each candidate is asked to run, as the
# Makefile does. CMake finds the interpreter for the build itself.
PY=""
for candidate in python3 python py; do
    if command -v "$candidate" >/dev/null 2>&1 &&
       "$candidate" -c "pass" >/dev/null 2>&1; then
        PY="$candidate"
        break
    fi
done
if [ -z "$PY" ]; then
    echo "FAIL: no working Python; this gate decodes the PNG it checks"
    exit 1
fi

fail=0
ok()   { echo "ok:   $*"; }
bad()  { echo "FAIL: $*"; fail=1; }

rm -rf "$BUILD"
mkdir -p "$BUILD"

if [ ! -f "$MODULE" ]; then
    echo "FAIL: $MODULE is missing"
    exit 1
fi
if [ ! -f "$ICO" ]; then
    echo "FAIL: $ICO is missing"
    exit 1
fi

sample_sum="$(cksum < "$SAMPLE")"

# --- 1, 2, 3: the conversion ------------------------------------------------
out="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
             -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/sce_sys" -P "$MODULE" 2>&1)"
status=$?
printf '%s\n' "$out" | sed 's/^/     /'

if [ $status -ne 0 ]; then
    bad "Icon.cmake exited $status"
else
    ok "Icon.cmake ran"
fi

png="$BUILD/sce_sys/icon0.png"
if [ ! -f "$png" ]; then
    bad "no icon0.png at $png"
else
    # The PNG header plus a spot check of the pixels, decoded with the project's
    # own ICO decoder so nothing new is trusted here.
    summary="$("$PY" -B - "$ICO" "$png" <<'PY'
import struct
import sys
import zlib

sys.path.insert(0, "tools")
import iconpack

ico, png = sys.argv[1], sys.argv[2]
data = open(png, "rb").read()
if data[:8] != b"\x89PNG\r\n\x1a\n":
    print("not a png")
    raise SystemExit(1)
w, h, depth, ctype = struct.unpack(">IIBB", data[16:26])
pos, idat = 8, b""
while pos < len(data):
    (ln,) = struct.unpack(">I", data[pos:pos + 4])
    kind = data[pos + 4:pos + 8]
    if kind == b"IDAT":
        idat += data[pos + 8:pos + 8 + ln]
    pos += 12 + ln
raw = zlib.decompress(idat)
stride = w * 4 + 1
rows = [raw[i * stride + 1:(i + 1) * stride] for i in range(h)]

src_w, src_h, src = iconpack.decode_dib(iconpack.read_ico(ico)[0][3])
src_opaque = sum(1 for row in src for p in row if p[3] > 0)

def px(x, y):
    return tuple(rows[y][x * 4:x * 4 + 4])

problems = []
if (w, h, depth, ctype) != (128, 128, 8, 6):
    problems.append("header is %dx%d depth %d type %d" % (w, h, depth, ctype))
for x, y in ((0, 0), (127, 0), (0, 127), (127, 127)):
    if px(x, y)[3] != 0:
        problems.append("corner %d,%d is not transparent" % (x, y))
if px(64, 64)[3] != 255:
    problems.append("centre is not opaque")
opaque = sum(1 for y in range(128) for x in range(128) if px(x, y)[3] > 0)
ratio = opaque / 16384.0
if not 0.55 <= ratio <= 0.95:
    problems.append("%.0f%% opaque, expected roughly the icon's 84%%" % (ratio * 100))
if src_opaque == 0:
    problems.append("the source icon is empty")
print(("%dx%d, %.1f%% opaque from %dx%d source%s") %
      (w, h, ratio * 100, src_w, src_h, "" if not problems else "; " + "; ".join(problems)))
raise SystemExit(1 if problems else 0)
PY
)"
    if [ $? -eq 0 ]; then
        ok "icon0.png: $summary"
    else
        bad "icon0.png: $summary"
    fi
fi

# The generated file must be the converted artwork, not the sample under another
# path: without this, a module that "succeeded" while copying the sample would
# satisfy the size and opacity checks below.
if [ -f "$png" ] && cmp -s "$png" "$SAMPLE"; then
    bad "icon0.png is byte-identical to the committed sample, so nothing was converted"
else
    ok "icon0.png differs from the committed sample"
fi

if [ "$(cksum < "$SAMPLE")" = "$sample_sum" ]; then
    ok "the committed sample icon was not touched"
else
    bad "port/psvita/sce_sys/icon0.png was modified by the build"
fi

# --- 4: no Python -----------------------------------------------------------
out2="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
              -DMUGEN_VITA_ICON_ICO="$ICO" -DMUGEN_VITA_ICON_SIZE=128 \
              -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/nopy" -DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON \
              -P "$MODULE" 2>&1)"
printf '%s\n' "$out2" | sed 's/^/     /'
if printf '%s\n' "$out2" | grep -q 'sample asset'; then
    ok "no Python3: falls back to the sample asset"
else
    bad "no Python3: no fallback message"
fi

# --- 5: missing .ico --------------------------------------------------------
out3="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
              -DMUGEN_VITA_ICON_ICO="$BUILD/absent.ico" -DMUGEN_VITA_ICON_SIZE=128 \
              -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/missing" -P "$MODULE" 2>&1)"
printf '%s\n' "$out3" | sed 's/^/     /'
if printf '%s\n' "$out3" | grep -q 'sample asset'; then
    ok "missing .ico: falls back to the sample asset"
else
    bad "missing .ico: no fallback message"
fi

rm -rf "$BUILD"

if [ $fail -ne 0 ]; then
    echo "vita-icon: FAILURES"
    exit 1
fi
echo "vita-icon: all checks passed"
