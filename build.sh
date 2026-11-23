#!/bin/sh
# build.sh - build mugen-unleashed on Linux from nothing
#
#   ./build.sh              verify the toolchain and packages, fetch the game
#                           data when absent, then build and install
#   ./build.sh --build      the same thing, spelled out
#   ./build.sh --setup      verify and fetch only, build nothing
#   ./build.sh --check      only verify what is installed, install nothing
#   ./build.sh --debug      unoptimised build with debug info, in its own tree;
#                           the output gains a -debug suffix
#   ./build.sh --jobs N     parallel jobs (default: the CPU count)
#   ./build.sh --offline    never download (a cached game-data archive is used)
#   ./build.sh --help
#
# The Linux counterpart of build.bat, and named the same way: no arguments is
# "do the whole job". There is nothing to install beyond the distribution's
# development packages -- SDL2, SDL2_mixer, SDL2_ttf, zlib and libpng are
# linked as the system's shared objects, and Lua, glad, minizip, lz4 and
# libresample are built from third_party/ -- so this script verifies them
# instead of downloading a kit and a dependency tree.
#
# What it does:
#
#   1. checks cc/gcc, make, pkg-config, ar and strip, and the pkg-config
#      packages the target links (sdl2 SDL2_mixer SDL2_ttf zlib libpng, plus
#      GL/GLU for the desktop renderer)
#   2. fetches the default M.U.G.E.N 1.1b1 tree into install/ when there is
#      none (tools/fetch_game_data.sh; a failed download only warns, because
#      the engine still builds without game data)
#   3. runs `make -j<jobs>` and `make install`, then reports what it built
#
# Downloads are cached in build/downloads and unpacking happens in
# build/tmp, so the folders under build/ follow the project rule: downloads
# are network caches, tmp is scratch. Nothing is written outside build/,
# install/ and the source tree.
#
# Overrides (environment variables):
#   CC            compiler                  (default gcc)
#   JOBS          parallel jobs             (default: nproc)
#   BUILD         build directory           (default build/<target>-sdl2,
#                                            build/<target>-sdl2-debug with --debug)
#   MUGEN_GLES    GLES renderer, 0 or 1     (linux-arm64 defaults to 1)
#   MUGEN_MODEL3D glTF models, 0 or 1       (default follows the renderer)
#   MUGEN_TARGET  the target, if not the host's
#
# See docs/BUILD-LINUX.md.
set -eu

# Resolve the script before changing directory, so a relative invocation from
# another directory still reads the right header for --help.
SELF=$(cd "$(dirname "$0")" && pwd)/$(basename "$0")
ROOT=$(dirname "$SELF")
cd "$ROOT"

MODE=build
DEBUG_BUILD=0
OFFLINE=0
JOBS=${JOBS:-}

say()  { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '    WARNING: %s\n' "$*" >&2; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

usage() {
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$SELF"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --build)   MODE=build ;;
        --setup)   MODE=setup ;;
        --check)   MODE=check ;;
        --debug)   DEBUG_BUILD=1 ;;
        --offline) OFFLINE=1 ;;
        --jobs)    [ $# -ge 2 ] || fail "--jobs requires a number"
                   JOBS=$2
                   shift ;;
        --jobs=*)  JOBS=${1#*=} ;;
        --help|-h) usage; exit 0 ;;
        *) fail "unknown argument: $1 (--help)" ;;
    esac
    shift
done

# This script is the native Linux entry point. Windows has build.bat, and a
# cross build owns its own wiring (port/arm64/build.bat, or the Makefile's
# CROSS_COMPILE=/XSYSROOT= by hand) because it needs a foreign toolchain and a
# dependency tree that this script neither installs nor verifies.
UNAME_S=$(uname -s 2>/dev/null || echo unknown)
if [ "$UNAME_S" != "Linux" ]; then
    fail "build.sh builds Linux natively; on $UNAME_S use build.bat (Windows) or a platform port"
fi
if [ -n "${CROSS_COMPILE:-}" ]; then
    fail "build.sh is the native Linux entry point; run make directly with CROSS_COMPILE=/XSYSROOT= (docs/BUILD-LINUX.md)"
fi

# ------------------------------------------------------------- toolchain --
# The compiler's own triple decides the target, exactly as the Makefile does.
# uname -m never decides the architecture: the compiler wins on disagreement.
CC=${CC:-gcc}
command -v "$CC" >/dev/null 2>&1 ||
    fail "no '$CC' in PATH; install build-essential (docs/BUILD-LINUX.md)"

TRIPLE=$("$CC" -dumpmachine 2>/dev/null || true)
if [ -z "$TRIPLE" ]; then
    TRIPLE=$("$CC" -v 2>&1 </dev/null | sed -n 's/^Target: *//p' | head -n 1 || true)
fi
[ -n "$TRIPLE" ] || fail "cannot determine the target triple of '$CC'"

if [ -n "${MUGEN_TARGET:-}" ]; then
    TARGET_NAME=$MUGEN_TARGET
else
    case "$TRIPLE" in
        x86_64-*)  TARGET_NAME=linux-x64 ;;
        aarch64-*) TARGET_NAME=linux-arm64 ;;
        i686-*)    TARGET_NAME=linux-x86 ;;
        *) fail "unsupported Linux triple \"$TRIPLE\"; supported: x86_64-, i686-, aarch64-" ;;
    esac
fi
case "$TARGET_NAME" in
    linux-x64|linux-arm64|linux-x86) ;;
    *) fail "MUGEN_TARGET=$TARGET_NAME is not a Linux target (linux-x64, linux-arm64, linux-x86)" ;;
esac
case "$TARGET_NAME" in
    *64) PTRBITS=64 ;;
    *)   PTRBITS=32 ;;
esac

# The renderer default is the Makefile's: GLES on linux-arm64 (the handhelds
# expose OpenGL ES only), desktop GL everywhere else.
if [ -n "${MUGEN_GLES:-}" ]; then
    GLES=$MUGEN_GLES
elif [ "$TARGET_NAME" = linux-arm64 ]; then
    GLES=1
else
    GLES=0
fi
case "$GLES" in
    0|1) ;;
    *) fail "MUGEN_GLES must be 0 or 1, not \"$GLES\"" ;;
esac

if [ -n "${BUILD:-}" ]; then
    BUILD_DIR=$BUILD
elif [ "$DEBUG_BUILD" = 1 ]; then
    # The config stamp does not record DEBUG=, so -O0 -g3 gets its own tree:
    # a shared one would leave both optimisation levels under one stamp.
    BUILD_DIR=build/$TARGET_NAME-sdl2-debug
else
    BUILD_DIR=build/$TARGET_NAME-sdl2
fi
if [ "$DEBUG_BUILD" = 1 ]; then
    BIN_SUFFIX=-debug
else
    BIN_SUFFIX=
fi
INSTALL_NAME=mugen-$TARGET_NAME$BIN_SUFFIX
BIN=$BUILD_DIR/$INSTALL_NAME
INSTALLED=install/$INSTALL_NAME

if [ -z "$JOBS" ]; then
    JOBS=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
fi
case "$JOBS" in
    ''|*[!0-9]*) fail "--jobs must be a positive number, not \"$JOBS\"" ;;
esac

for t in make ar strip; do
    command -v "$t" >/dev/null 2>&1 ||
        fail "$t not found in PATH; install build-essential (docs/BUILD-LINUX.md)"
done
command -v pkg-config >/dev/null 2>&1 ||
    fail "pkg-config not found; install pkg-config (docs/BUILD-LINUX.md)"

if [ "$GLES" = 1 ]; then
    RENDERER='OpenGL ES 3.0'
else
    RENDERER='desktop OpenGL'
fi
if [ "$DEBUG_BUILD" = 1 ]; then
    DEBUG_NOTE=' (debug)'
else
    DEBUG_NOTE=
fi

printf '\n'
printf 'mugen-unleashed - Linux native build\n'
printf '  compiler  %s (%s)\n' "$("$CC" --version 2>/dev/null | head -n 1)" "$TRIPLE"
printf '  target    %s (%s-bit)\n' "$TARGET_NAME" "$PTRBITS"
printf '  renderer  %s\n' "$RENDERER"
printf '  build     %s%s\n' "$BUILD_DIR" "$DEBUG_NOTE"
printf '  install   install/%s\n' "$INSTALL_NAME"
printf '  jobs      %s\n' "$JOBS"

# --------------------------------------------------------------- packages --
# The same five the Makefile's check-pkgconfig asks for, so a missing -dev
# package is named here with the command that fixes it instead of surfacing as
# undefined Mix_*/TTF_*/SDL_* symbols at the link. GL/GLU are not pkg-config
# packages in the Makefile -- it links -lGL -lGLU directly -- so they are
# probed through the compiler's own header search.
PKGS='sdl2 SDL2_mixer SDL2_ttf zlib libpng'
MISSING=
for p in $PKGS; do
    pkg-config --exists "$p" 2>/dev/null || MISSING="$MISSING $p"
done
if [ -n "$MISSING" ]; then
    printf '\nERROR: pkg-config cannot find:%s\n' "$MISSING" >&2
    printf '       install the development packages (docs/BUILD-LINUX.md):\n' >&2
    printf '         sudo apt-get install build-essential pkg-config libsdl2-dev \\\n' >&2
    printf '             libsdl2-mixer-dev libsdl2-ttf-dev zlib1g-dev libpng-dev libglu1-mesa-dev\n' >&2
    exit 1
fi
if [ "$GLES" != 1 ]; then
    if ! printf '#include <GL/gl.h>\n#include <GL/glu.h>\n' |
            "$CC" -x c -fsyntax-only - >/dev/null 2>&1; then
        printf '\nERROR: GL/GLU headers not found (needed by the desktop renderer)\n' >&2
        printf '       install libglu1-mesa-dev (Debian/Ubuntu) or mesa-libGLU-devel (Fedora),\n' >&2
        printf '       or build with MUGEN_GLES=1\n' >&2
        exit 1
    fi
fi
SDLVER=$(pkg-config --modversion sdl2 2>/dev/null || true)

# -------------------------------------------------------------- game data --
# install/ is the game tree the binary runs inside. A failure only warns: the
# engine builds and the binary reports the missing tree at start-up.
say 'game data'
if [ -f install/data/mugen.cfg ]; then
    info "present: $ROOT/install"
elif [ "$MODE" = check ]; then
    info "MISSING - run ./build.sh to fetch the default assets"
else
    if [ "$OFFLINE" = 1 ]; then
        if sh tools/fetch_game_data.sh --offline --dest install; then
            info "installed: $ROOT/install"
        else
            warn 'could not fetch the default game data (--offline and no cached archive)'
        fi
    else
        if sh tools/fetch_game_data.sh --dest install; then
            info "installed: $ROOT/install"
        else
            warn 'could not fetch the default game data'
        fi
    fi
    if [ ! -f install/data/mugen.cfg ]; then
        warn 'the built binary will exit at start until install/ has a tree'
        warn '(see tools/fetch_game_data.sh --help)'
    fi
fi

# ------------------------------------------------------------------- check --
say 'check'
info "packages: $PKGS"
if [ -n "$SDLVER" ]; then
    info "SDL2 $SDLVER"
fi
info "build directory: $BUILD_DIR"
info 'ok'

if [ "$MODE" = check ]; then
    say 'check complete'
    info 'nothing was built.'
    info 'run ./build.sh to fetch what is missing and build.'
    exit 0
fi
if [ "$MODE" = setup ]; then
    say 'setup complete'
    info 'nothing was built (--setup).'
    info 'run ./build.sh to build and install.'
    exit 0
fi

# ------------------------------------------------------------------- build --
# BUILD= is handed over repository-relative on purpose: gcc writes the path it
# was given into the generated .d files, and an absolute POSIX spelling makes
# every header dependency name a target make never matches.
run_make() {
    if [ "$DEBUG_BUILD" = 1 ]; then
        make -j"$JOBS" DEBUG=1 BUILD="$BUILD_DIR" "$@"
    else
        make -j"$JOBS" BUILD="$BUILD_DIR" "$@"
    fi
}

say "build $TARGET_NAME"
info "make -j$JOBS"
if ! run_make; then
    fail "the $TARGET_NAME build failed"
fi
[ -f "$BIN" ] || fail "make reported success but $BIN is missing"

# ----------------------------------------------------------------- install --
# The Makefile's install target strips the binary and copies it into install/.
# No configuration is copied: the base mugen.cfg and the platform overlay are
# embedded in the executable and written into data/ on first run when absent.
say 'install'
if ! run_make install; then
    fail 'make install failed'
fi
[ -f "$INSTALLED" ] || fail "make install reported success but $INSTALLED is missing"

# ----------------------------------------------------------------- summary --
file_size() {
    wc -c < "$1" | tr -d '[:space:]'
}

say 'done'
printf '  built      %s (%s bytes)\n' "$BIN" "$(file_size "$BIN")"
printf '  installed  %s (%s bytes)\n' "$INSTALLED" "$(file_size "$INSTALLED")"
if [ -f install/data/mugen.cfg ]; then
    printf '  data       %s\n' "$ROOT/install"
else
    printf '  data       MISSING - the binary exits at start until install/ has a tree\n'
fi
printf '\nRun it from the game directory:\n'
printf '  cd install\n'
printf '  ./%s\n' "$INSTALL_NAME"
