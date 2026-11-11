#!/bin/sh
# Run the SDL2 gamepad key menu and cfg persistence with virtual pads.
# TEST_PLATFORM=desktop also exercises the Windows/Linux SDL2 compile path.
# Supports native Linux and MinGW via CC/CFLAGS and the selected pkg-config.
set -eu
cd "$(dirname "$0")/.."

CC="${CC:-cc}"
PKG_CONFIG="${PKG_CONFIG:-pkg-config}"
OUT="${OUT:-$PWD/build/tmp/gamepad-config-test}"
case "${TEST_PLATFORM:-android}" in
    android) PLATFORM_DEFS="-DANDROID_PORT -DMUGEN_ANDROID_SW"; PLATFORM_SRCS="" ;;
    vita) PLATFORM_DEFS="-DMUGEN_SDL2 -DMUGEN_VITA"; PLATFORM_SRCS="src/sdl2compat.c port/psvita/src/vita_input.c" ;;
    desktop) PLATFORM_DEFS="-DMUGEN_SDL2"; PLATFORM_SRCS="src/sdl2compat.c" ;;
    *) echo "TEST_PLATFORM must be android, desktop or vita" >&2; exit 2 ;;
esac
mkdir -p "$OUT/tmp"
export TMPDIR="$OUT/tmp"

SDL_CFLAGS="${SDL_CFLAGS:-$("$PKG_CONFIG" --cflags sdl2 SDL2_ttf SDL2_mixer)}"
SDL_LIBS="${SDL_LIBS:-$("$PKG_CONFIG" --libs sdl2 zlib | sed 's/-lSDL2main//g; s/-mwindows//g')}"
FLAGS="${CFLAGS:-} -std=gnu11 -O1 -g -UNDEBUG -Werror=implicit-function-declaration"
FLAGS="$FLAGS $PLATFORM_DEFS -DSDL_MAIN_HANDLED -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
FLAGS="$FLAGS -Isrc -Iport/psvita/src -Iport/android/app/src/main/jni/src -Ithird_party/glad-2.1 -Ithird_party/minizip-0.15"
FLAGS="$FLAGS -Ithird_party/libpng-1.2.38 -Ithird_party/lua-5.1.4/src -Ithird_party/libresample/include $SDL_CFLAGS"
case "$($CC -dumpmachine)" in *mingw*) FLAGS="$FLAGS -flto=auto" ;; esac
SAN=""
if [ "${SANITIZE:-0}" = 1 ]; then SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
# Keep unrelated globals uninstrumented: ASan's global registration otherwise
# retains the complete engine's dispatch tables despite section collection.
$CC $FLAGS -c src/globals.c -o "$OUT/globals.o"
# Flags are intentionally split so CC can carry a compiler/target wrapper.
# shellcheck disable=SC2086
$CC $FLAGS $SAN \
    $PLATFORM_SRCS tools/test_gamepad_config.c src/config.c src/configmerge.c src/title.c src/ini.c src/stream.c \
    src/file.c src/util.c src/compat_linux.c src/pads.c src/sdl2keys.c src/gameloop.c "$OUT/globals.o" \
    third_party/minizip-0.15/unzip.c -Wl,--gc-sections \
    ${LDFLAGS:-} $SDL_LIBS -lm -o "$OUT/test_gamepad_config"

python3 - "$OUT" <<'PY'
from pathlib import Path
import sys

out = Path(sys.argv[1])
base = """; keep this comment
[Options]
motif = data/system.def

[Input]
p1.joystick.type = 1
p2.joystick.type = 1

[P1 Keys]
A = 44

[P2 Keys]
A = 102

[P1 Joystick]
Jump = 9
Crouch = 10
Left = 11
Right = 12
A = 13
B = 14
C = 20
X = 15
Y = 16
Z = 18
Start = 24

[P2 Joystick]
Jump = 41
Crouch = 42
Left = 43
Right = 44
A = 46
B = 47
C = 52
X = 45
Y = 48
Z = 50
Start = 54
"""
partial_groups = """
[P1 Gamepad]
A = 5 ; keep this inline comment

[P2 Gamepad]
A = 5
"""
for scenario in ("lf", "crlf", "mixed", "missing", "missing-p1", "missing-p2", "late-raw", "menu-actions"):
    groups = "" if scenario == "missing" else partial_groups
    if scenario == "missing-p1":
        groups = "\n[P2 Gamepad]\nA = 5\n"
    elif scenario == "missing-p2":
        groups = "\n[P1 Gamepad]\nA = 5 ; keep this inline comment\n"
    text = base + "\n[Other]\nKeep = 123\n" + groups
    data = text.encode()
    if scenario == "crlf":
        data = b"\xef\xbb\xbf" + data.replace(b"\n", b"\r\n")
    cfg = out / scenario / "data" / "mugen.cfg"
    cfg.parent.mkdir(parents=True, exist_ok=True)
    cfg.write_bytes(data)
PY

for scenario in lf crlf mixed missing missing-p1 missing-p2 late-raw menu-actions; do
    (cd "$OUT/$scenario" && "$OUT/test_gamepad_config" "$scenario")
done
# A second process reads the saved mappings without any in-memory tables.
(cd "$OUT/lf" && "$OUT/test_gamepad_config" reload)

python3 - "$OUT" <<'PY'
from pathlib import Path
import sys

out = Path(sys.argv[1])
for scenario in ("lf", "crlf", "mixed", "missing", "missing-p1", "missing-p2", "late-raw", "menu-actions"):
    data = (out / scenario / "data" / "mugen.cfg").read_bytes()
    assert data.count(b"[P1 Gamepad]") == 1
    assert data.count(b"[P2 Gamepad]") == 1
    if scenario == "crlf":
        assert data.startswith(b"\xef\xbb\xbf")
        assert b"\n" not in data.replace(b"\r\n", b"")
    else:
        assert b"\r" not in data
print("gamepad cfg: unique sections, BOM and line endings preserved")
PY
