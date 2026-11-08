#!/usr/bin/env bash
# VT03-icon: the PS Vita's shell artwork comes from res/icons/*.ico.
#
# port/psvita/cmake/Icon.cmake derives all three sce_sys/ images from the .ico
# the Windows build embeds: the 128x128 bubble icon, the 840x500 LiveArea
# background and the 280x158 start-up image. The module is written to run in
# CMake script mode, so this gate exercises the real file with `cmake -P` and
# needs no VitaSDK -- which is why it exists: the Vita build itself cannot run on
# a machine without the SDK, and "the icon block looks right" is not a check.
#
# Asserts:
#   1. all three PNGs are produced with the sizes the Vita wants (128x128,
#      840x500, 280x158), all RGBA;
#   2. they carry the artwork rather than a blank canvas: the icon is transparent
#      at the corners and opaque in the middle, the background is opaque
#      everywhere with the artwork over its own average colour, and the start-up
#      image is transparent outside the artwork;
#   3. the committed assets under sce_sys/ are left alone (the build writes into
#      the build tree, so a git checkout stays clean);
#   4. with no Python the module falls back to those committed assets and still
#      sets each variable to a file that exists;
#   5. a missing .ico falls back the same way.
#
# Note on (3): the committed assets are the same derivation, so the generated
# files are byte-identical to them by design. This gate therefore cannot tell
# "converted" from "copied" by comparison -- it checks the pixels and the module's
# own status message instead, which is what the earlier byte-comparison stood in
# for when the committed icon was still the VitaSDK sample.
#
# Usage: bash tools/test_vita_icon.sh
# Exit 0 on success, 1 on the first failed assertion.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/vita-icon"
MODULE="$ROOT/port/psvita/cmake/Icon.cmake"
SCE_SYS="$ROOT/port/psvita/sce_sys"
ICO="$ROOT/res/icons/1.ico"
SAMPLES="$SCE_SYS/icon0.png $SCE_SYS/livearea/contents/bg.png $SCE_SYS/livearea/contents/startup.png"

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
    echo "FAIL: no working Python; this gate decodes the PNGs it checks"
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

# cksum of every committed asset, so "not touched" covers all three.
before=""
for f in $SAMPLES; do
    before="$before$(cksum < "$f")
"
done

# --- 1, 2, 3: the conversions ----------------------------------------------
out="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
             -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/sce_sys" -P "$MODULE" 2>&1)"
status=$?
printf '%s\n' "$out" | sed 's/^/     /'

if [ $status -ne 0 ]; then
    bad "Icon.cmake exited $status"
else
    ok "Icon.cmake ran"
fi

for name in icon0.png livearea/contents/bg.png livearea/contents/startup.png; do
    if [ -f "$BUILD/sce_sys/$name" ]; then
        ok "generated $name"
    else
        bad "no $name at $BUILD/sce_sys/$name"
    fi
done

# The module has to say the artwork came from the .ico, not that it fell back.
for label in icon0.png bg.png startup.png; do
    if printf '%s\n' "$out" | grep -q "$label comes from .*1.ico"; then
        ok "$label was derived from res/icons/1.ico"
    else
        bad "$label: no 'came from the .ico' status message"
    fi
done

summary="$("$PY" -B - "$ICO" "$BUILD/sce_sys/icon0.png" \
                       "$BUILD/sce_sys/livearea/contents/bg.png" \
                       "$BUILD/sce_sys/livearea/contents/startup.png" <<'PY'
import struct
import sys
import zlib

sys.path.insert(0, "tools")
import iconpack

ico, icon_png, bg_png, startup_png = sys.argv[1:5]


def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s: not a png" % path)
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
    return w, h, depth, ctype, rows


def pixel(rows, x, y):
    return tuple(rows[y][x * 4:x * 4 + 4])


src_w, src_h, src = iconpack.decode_dib(iconpack.read_ico(ico)[0][3])
if not any(p[3] > 0 for row in src for p in row):
    raise SystemExit("the source icon is empty")

problems = []
notes = []

# --- the bubble icon: 128x128, artwork with transparent corners --------------
w, h, depth, ctype, rows = load(icon_png)
if (w, h, depth, ctype) != (128, 128, 8, 6):
    problems.append("icon0.png header is %dx%d depth %d type %d" % (w, h, depth, ctype))
for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
    if pixel(rows, x, y)[3] != 0:
        problems.append("icon0.png corner %d,%d is not transparent" % (x, y))
if pixel(rows, w // 2, h // 2)[3] != 255:
    problems.append("icon0.png centre is not opaque")
opaque = sum(1 for y in range(h) for x in range(w) if pixel(rows, x, y)[3] > 0)
notes.append("icon0 %dx%d, %.0f%% opaque" % (w, h, 100.0 * opaque / (w * h)))

# --- the LiveArea background: 840x500, opaque field with the artwork ---------
w, h, depth, ctype, rows = load(bg_png)
if (w, h, depth, ctype) != (840, 500, 8, 6):
    problems.append("bg.png header is %dx%d depth %d type %d" % (w, h, depth, ctype))
corner = pixel(rows, 0, 0)
if corner[3] != 255:
    problems.append("bg.png is not opaque at the corner (alpha %d)" % corner[3])
if corner[:3] in ((0, 0, 0), (255, 255, 255)):
    problems.append("bg.png background is %s, so it did not come from the artwork's colour"
                    % ("black" if corner[:3] == (0, 0, 0) else "white"))
centre = pixel(rows, w // 2, h // 2)
if centre[:3] == corner[:3]:
    problems.append("bg.png has no artwork in the middle (same colour as the background)")
# The logo must stay inside the middle half: nothing of it in the far corners.
if pixel(rows, 5, 5)[:3] != corner[:3]:
    problems.append("bg.png artwork bleeds into the top-left corner")
notes.append("bg %dx%d, background #%02x%02x%02x" % (w, h, corner[0], corner[1], corner[2]))

# --- the start-up image: 280x158, transparent outside the artwork -----------
w, h, depth, ctype, rows = load(startup_png)
if (w, h, depth, ctype) != (280, 158, 8, 6):
    problems.append("startup.png header is %dx%d depth %d type %d" % (w, h, depth, ctype))
for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
    if pixel(rows, x, y)[3] != 0:
        problems.append("startup.png corner %d,%d is not transparent" % (x, y))
if pixel(rows, w // 2, h // 2)[3] != 255:
    problems.append("startup.png centre is not opaque")
notes.append("startup %dx%d" % (w, h))

print("; ".join(notes) + ("" if not problems else " | " + "; ".join(problems)))
raise SystemExit(1 if problems else 0)
PY
)"
if [ $? -eq 0 ]; then
    ok "artwork: $summary"
else
    bad "artwork: $summary"
fi

# --- 3: the committed assets are not touched --------------------------------
after=""
for f in $SAMPLES; do
    after="$after$(cksum < "$f")
"
done
if [ "$before" = "$after" ]; then
    ok "the committed sce_sys/ assets were not touched"
else
    bad "the build modified a committed file under port/psvita/sce_sys/"
fi

# --- 4: no Python ----------------------------------------------------------
out2="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
              -DMUGEN_VITA_ICON_ICO="$ICO" -DMUGEN_VITA_ICON_SIZE=128 \
              -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/nopy" -DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON \
              -P "$MODULE" 2>&1)"
printf '%s\n' "$out2" | sed 's/^/     /'
if printf '%s\n' "$out2" | grep -q 'committed sample'; then
    ok "no Python3: falls back to the committed assets"
else
    bad "no Python3: no fallback message"
fi
# A fallback that names a file which does not exist would fail at packing time,
# far from here; ask cmake to print the three paths and check them. The probe is
# a real file in the build tree: `cmake -P /dev/stdin` is not portable, and
# include() needs the module's absolute path anyway.
cat > "$BUILD/probe.cmake" <<'CM'
include("${MUGEN_VITA_MODULE}")
foreach(v MUGEN_VITA_ICON_PNG MUGEN_VITA_BG_PNG MUGEN_VITA_STARTUP_PNG)
  message("${v}=${${v}}")
endforeach()
CM
paths="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
               -DMUGEN_VITA_MODULE="$MODULE" \
               -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/nopy2" -DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON \
               -P "$BUILD/probe.cmake" 2>&1)"
missing=0
for v in MUGEN_VITA_ICON_PNG MUGEN_VITA_BG_PNG MUGEN_VITA_STARTUP_PNG; do
    p="$(printf '%s\n' "$paths" | sed -n "s/^$v=//p")"
    if [ -z "$p" ] || [ ! -f "$p" ]; then
        bad "no Python3: $v is '${p:-<unset>}', which is not a file"
        missing=1
    fi
done
[ $missing -eq 0 ] && ok "no Python3: all three paths point at existing files"

# --- 5: missing .ico -------------------------------------------------------
out3="$(cmake -DMUGEN_ROOT="$ROOT" -DMUGEN_VITA_SCE_SYS="$SCE_SYS" \
              -DMUGEN_VITA_ICON_ICO="$BUILD/absent.ico" -DMUGEN_VITA_ICON_SIZE=128 \
              -DMUGEN_VITA_ICON_OUT_DIR="$BUILD/missing" -P "$MODULE" 2>&1)"
printf '%s\n' "$out3" | sed 's/^/     /'
if printf '%s\n' "$out3" | grep -q 'committed sample'; then
    ok "missing .ico: falls back to the committed assets"
else
    bad "missing .ico: no fallback message"
fi

rm -rf "$BUILD"

if [ $fail -ne 0 ]; then
    echo "vita-icon: FAILURES"
    exit 1
fi
echo "vita-icon: all checks passed"
