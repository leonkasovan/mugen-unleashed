#!/usr/bin/env bash
# Build mugen-unleashed for the PlayStation Vita from a Windows host (MSYS2 /
# Git Bash), installing VitaSDK first when it is not already there.
#
#   port/psvita/build.sh                install what is missing, then build
#   port/psvita/build.sh --setup        install VitaSDK and its packages, stop
#   port/psvita/build.sh --check-tools  report what is present, build nothing
#   port/psvita/build.sh --clean        remove this port's build output
#   port/psvita/build.sh --offline      fail instead of downloading anything
#   port/psvita/build.sh --jobs N       parallel build jobs (default: CPU count)
#   port/psvita/build.sh --debug        Debug build (default: Release)
#   port/psvita/build.sh --data DIR     pack this M.U.G.E.N tree into the VPK
#   port/psvita/build.sh --no-data      pack no game data (engine only)
#   port/psvita/build.sh --titleid ID   nine-character title id (MSDK00001)
#   port/psvita/build.sh --env          print the exports, build nothing
#
# What it sets up, and where:
#
#   1. VitaSDK in VITASDK (default C:/vitasdk, 600 MB installed). The archive is
#      the project's own release asset, cached in build/downloads/vitasdk.zip and
#      verified against a SHA-256 pinned below, exactly like
#      build/sysroots/arm64-sysroot.zip in port/arm64/build.sh. It is a full SDK
#      with the packages already in it (sdl, sdl_gfx, sdl_mixer, sdl_ttf, zlib,
#      libpng, vitaGL and vitaGL's own dependencies), so a clean machine needs
#      one download and no vdpm at all. An SDK that is already installed --
#      C:/vitasdk from setup_psvita_dev.bat included -- is used as it is.
#
#   2. The environment the build needs, for the duration of this script:
#        VITASDK=C:/vitasdk
#        PATH=%VITASDK%\bin;%PATH%
#        CMAKE_TOOLCHAIN_FILE=%VITASDK%\share\vita.toolchain.cmake
#      Child processes only: a script cannot change its caller's environment.
#      `--env` prints the three lines to eval in your own shell.
#
#   3. The VitaSDK packages port/psvita/CMakeLists.txt requires. The bundled SDK
#      already carries them; anything genuinely missing is installed with vdpm
#      (which needs the network, so --offline refuses instead).
#
#   4. The build itself, with CMake into build/psvita (BUILD=<dir> overrides):
#        build/psvita/mugen-psvita.self   the runnable module
#        build/psvita/mugen-psvita.vpk    installable homebrew (VitaShell)
#
# The VPK packs the game tree at DATA_DIR, which defaults to install/ when that
# is a M.U.G.E.N installation; --no-data builds an engine-only VPK and the
# console then needs the data copied by hand. See port/psvita/README.md.
#
# Nothing here writes outside the repository except VITASDK itself: downloads
# are cached in build/downloads, staging happens in build/tmp, and the SDK is
# installed at the path the caller named (C:/vitasdk by default) because that is
# where a Vita toolchain has to live to be usable by other tools too.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DOWNLOADS="$ROOT/build/downloads"
SCRATCH="$ROOT/build/tmp"

# ---------------------------------------------------------------- VitaSDK
#
# VITASDK_DIR is where the SDK gets installed; C:/vitasdk is what VitaSDK's own
# documentation and this port's README use, and what setup_psvita_dev.bat
# exports. An absolute Windows path keeps the toolchain file, the compiler's
# response files and CMake all in agreement -- an MSYS /c/... path is silently
# rewritten by some of those tools and not by others.
#
# An exported VITASDK is honoured only when a working SDK is really there, since
# an MSYS2 shell may export VITASDK at a path that holds nothing; installing
# 600 MB somewhere other than the documented C:/vitasdk would be worse than
# ignoring the variable. VITASDK_DIR always wins when given.
VITASDK_DIR="${VITASDK_DIR:-}"

# The pinned archive. The SHA-256 is the asset's own published digest, checked
# every time the cached copy is used; VITASDK_URL / VITASDK_SHA256 override both
# for a fork or a re-published asset (a changed archive must not silently become
# the SDK that gets linked).
VITASDK_URL="${VITASDK_URL:-https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/vitasdk.zip}"
VITASDK_FILE="vitasdk.zip"
VITASDK_SHA256="${VITASDK_SHA256:-12410f2edaedb5de51bfba8a66c4c807dd587ee540311d846544daa556fb4b7d}"

# The VitaSDK packages the port links against, as "vdpm package:artifact the
# CMake configure actually needs". Checked by artifact, not by vdpm's own
# bookkeeping, because the SDK may have been installed by hand or by this
# project's archive and then it has no package database to ask.
VITA_PACKAGES=(
    "sdl:arm-vita-eabi/include/SDL/SDL.h"
    "sdl:arm-vita-eabi/lib/libSDL.a"
    "sdl_gfx:arm-vita-eabi/lib/libSDL_gfx.a"
    "sdl_mixer:arm-vita-eabi/lib/libSDL_mixer.a"
    "sdl_ttf:arm-vita-eabi/lib/libSDL_ttf.a"
    "zlib:arm-vita-eabi/lib/libz.a"
    "libpng:arm-vita-eabi/lib/libpng.a"
    "vitaGL:arm-vita-eabi/lib/libvitaGL.a"
    "vitaGL:arm-vita-eabi/lib/libvitashark.a"
    "vitaGL:arm-vita-eabi/lib/libSceShaccCgExt.a"
    "vitaGL:arm-vita-eabi/lib/libmathneon.a"
)

BUILD_TYPE=Release
JOBS=""
CLEAN=0
OFFLINE=0
DO_SETUP=0
CHECK_TOOLS=0
PRINT_ENV=0
DATA_SET=0
DATA_DIR=""

say()  { printf '\n==> %s\n' "$*"; }
info() { printf '    %s\n' "$*"; }
fail() { printf '\nerror: %s\n' "$*" >&2; exit 1; }
usage() {
  awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --setup) DO_SETUP=1 ;;
        --check-tools) CHECK_TOOLS=1 ;;
        --env) PRINT_ENV=1 ;;
        --clean) CLEAN=1 ;;
        --offline) OFFLINE=1 ;;
        --debug) BUILD_TYPE=Debug ;;
        --jobs) JOBS="${2:-}"; shift ;;
        --jobs=*) JOBS="${1#*=}" ;;
        --data) DATA_DIR="${2:-}"; DATA_SET=1; shift ;;
        --data=*) DATA_DIR="${1#*=}"; DATA_SET=1 ;;
        --no-data) DATA_DIR=""; DATA_SET=1 ;;
        --titleid) TITLEID="${2:-}"; shift ;;
        --titleid=*) TITLEID="${1#*=}" ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown argument: %s (--help)\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [ "$OFFLINE" = 1 ] && [ "$DO_SETUP" = 1 ]; then
    fail "--offline cannot be combined with --setup"
fi

# ---------------------------------------------------------------- platform
#
# The pinned archive is a Windows-hosted VitaSDK: bin/ is full of .exe files and
# the compiler is arm-vita-eabi-gcc.exe. A Linux or macOS host needs a natively
# installed VitaSDK (its own installer, and vdpm for the packages) and then the
# manual CMake commands in port/psvita/README.md; this script deliberately does
# not pretend to handle that case.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*|Windows_NT) ;;
    Linux)
        if grep -qi microsoft /proc/version 2>/dev/null; then
            fail "run this from Windows (MSYS2/Git Bash), not from inside WSL:
       the downloaded VitaSDK is the Windows-hosted build and its compiler is
       arm-vita-eabi-gcc.exe. On Linux, install VitaSDK natively and use the
       cmake commands in port/psvita/README.md."
        fi
        fail "this script installs the Windows-hosted VitaSDK; on Linux install
       VitaSDK natively (https://vitasdk.org) and use the cmake commands in
       port/psvita/README.md." ;;
    *)  fail "unsupported host $(uname -s); this port's script targets Windows (MSYS2/Git Bash)" ;;
esac

shell_path() {                 # POSIX form of a user-supplied path
    if [ -z "$1" ]; then printf '\n'; return 0; fi
    if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else printf '%s\n' "$1"; fi
}
win_path() {                   # Windows form, for CMake and the compiler
    if [ -z "$1" ]; then printf '\n'; return 0; fi
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

if [ -z "$VITASDK_DIR" ]; then
    if [ -n "${VITASDK:-}" ] && [ -x "$(shell_path "$VITASDK")/bin/arm-vita-eabi-gcc.exe" ]; then
        VITASDK_DIR="$VITASDK"
    else
        VITASDK_DIR="C:/vitasdk"
    fi
fi

VITASDK=$(shell_path "$VITASDK_DIR")
VITASDK_WIN=$(win_path "$VITASDK_DIR")
TOOLCHAIN_FILE_WIN="$VITASDK_WIN/share/vita.toolchain.cmake"
CC_BIN="$VITASDK/bin/arm-vita-eabi-gcc.exe"
# BUILD is the repository's own convention for "where output goes" (the
# Makefile accepts BUILD=build/<dir>); a relative value is taken from the
# repository root, and CMake writes into its own generator files there.
BUILD_DIR="${BUILD:-build/psvita}"
case "$BUILD_DIR" in
    [A-Za-z]:*|/*) OUT=$(shell_path "$BUILD_DIR") ;;
    *)             OUT=$(shell_path "$ROOT/$BUILD_DIR") ;;
 esac

# The environment steps 2 of the header describes. Exported for every child
# process from here on -- cmake, ninja/make, the compiler driver, the
# arm-vita-eabi-pkg-config wrapper (which reads $VITASDK) and vdpm.
export VITASDK
export CMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE_WIN"
case ":$PATH:" in
    *":$VITASDK/bin:"*) ;;
    *) PATH="$VITASDK/bin:$PATH"; export PATH ;;
esac

if [ "$PRINT_ENV" = 1 ]; then
    printf 'export VITASDK=%s\n' "$VITASDK_WIN"
    printf 'export PATH=%s/bin:$PATH\n' "$VITASDK_WIN"
    printf 'export CMAKE_TOOLCHAIN_FILE=%s\n' "$TOOLCHAIN_FILE_WIN"
    exit 0
fi

# --------------------------------------------------------------- downloads
fetch() {                      # fetch <url> <dest>
    command -v curl >/dev/null 2>&1 ||
        fail "curl is required to download VitaSDK (https://curl.se/download.html)"
    case "$1" in https://*) ;; *) fail "refusing non-HTTPS download: $1" ;; esac
    if ! curl --fail --location --silent --show-error --proto '=https' \
            --retry 2 --connect-timeout 20 --max-time 3600 \
            --output "$2.part" "$1"; then
        rm -f "$2.part"
        fail "download failed: $1; rerun to retry"
    fi
    mv -f "$2.part" "$2"
}

hash_of() {                    # hash_of <sha256|md5> <file> -> hex digest
    local alg="$1" file="$2"
    if command -v "${alg}sum" >/dev/null 2>&1; then
        "${alg}sum" "$file" | awk '{print $1}'
    elif [ "$alg" = sha256 ] && command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst "-$alg" "$file" | awk '{print $NF}'
    else
        fail "no verifier for $alg (install ${alg}sum or openssl)"
    fi
}

# ------------------------------------------------------------------ VitaSDK
#
# "Installed" means the two things the build cannot work without: the compiler
# and the toolchain file. A directory that merely exists is not enough -- a
# half-extracted SDK is the failure that breaks every later run while looking
# like success.
vitasdk_present() {
    [ -x "$CC_BIN" ] && [ -f "$VITASDK/share/vita.toolchain.cmake" ] &&
        [ -f "$VITASDK/share/vita.cmake" ]
}

install_vitasdk() {
    local archive stage stageroot dest backup marker
    say "VitaSDK ($VITASDK_FILE, sha256)"

    if vitasdk_present; then
        info "already installed in $VITASDK"
        info "($("$CC_BIN" --version 2>/dev/null | head -1))"
        return 0
    fi
    [ "$OFFLINE" = 1 ] && fail "--offline: no VitaSDK at $VITASDK
       (expected $CC_BIN and share/vita.toolchain.cmake)"

    mkdir -p "$DOWNLOADS"
    archive="$DOWNLOADS/$VITASDK_FILE"

    if [ -f "$archive" ] && [ "$(hash_of sha256 "$archive")" = "$VITASDK_SHA256" ]; then
        info "cached download verified (sha256)"
    else
        rm -f "$archive"
        info "downloading $VITASDK_FILE (about 196 MB, 600 MB unpacked)"
        info "$VITASDK_URL"
        fetch "$VITASDK_URL" "$archive"
        if [ "$(hash_of sha256 "$archive")" != "$VITASDK_SHA256" ]; then
            rm -f "$archive"
            fail "$VITASDK_FILE failed its SHA-256; nothing was extracted
       expected $VITASDK_SHA256
       (a re-published asset needs VITASDK_SHA256=<new hash>)"
        fi
        info "sha256 verified"
    fi

    command -v unzip >/dev/null 2>&1 || fail "unzip is required (MSYS2: pacman -S unzip)"
    stage="$SCRATCH/stage-vitasdk"
    stageroot="$stage"
    rm -rf "$stage" && mkdir -p "$stage"
    info "unpacking"
    unzip -q -o "$archive" -d "$stage" || { rm -rf "$stage"; fail "unzip failed"; }

    # The asset is packed with one top directory (vitasdk/), but that is not its
    # contract: whatever single directory appears is unwrapped, so VITASDK ends
    # up holding bin/, arm-vita-eabi/ and share/ directly.
    if [ ! -d "$stage/bin" ]; then
        set -- "$(find "$stage" -mindepth 1 -maxdepth 1 -type d | head -1)"
        [ -n "${1:-}" ] && [ -d "$1/bin" ] ||
            { rm -rf "$stageroot"; fail "unexpected archive layout (no bin/ found)"; }
        stage="$1"
    fi
    [ -x "$stage/bin/arm-vita-eabi-gcc.exe" ] || {
        rm -rf "$stageroot"
        fail "the archive did not produce bin/arm-vita-eabi-gcc.exe"
    }
    [ -f "$stage/share/vita.toolchain.cmake" ] || {
        rm -rf "$stageroot"
        fail "the archive did not produce share/vita.toolchain.cmake"
    }

    # Install as a transaction beside the destination, then swap: a mistake or
    # an interrupted copy leaves the SDK that was there untouched.
    dest="${VITASDK}.mugen-new"
    backup="${VITASDK}.mugen-old"
    [ ! -e "$dest" ] && [ ! -e "$backup" ] || {
        rm -rf "$stageroot"
        fail "unfinished VitaSDK transaction at $dest or $backup; nothing was changed"
    }
    # An existing non-empty directory is only replaced when it is ours (carries
    # the marker below), so a hand-made SDK is never silently wiped.
    if [ -d "$VITASDK" ] && [ -n "$(find "$VITASDK" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
        if ! compgen -G "$VITASDK/.mugen-vitasdk-*" >/dev/null && ! vitasdk_present; then
            rm -rf "$stageroot"
            fail "$VITASDK exists but is not a VitaSDK; move it aside or set VITASDK_DIR=<other dir>"
        fi
    fi
    mkdir -p "$dest"
    info "installing into $VITASDK"
    cp -a "$stage/." "$dest/" || { rm -rf "$dest" "$stageroot"; fail "copy failed; nothing was changed"; }
    marker=".mugen-vitasdk-$(printf '%s' "$VITASDK_SHA256" | cut -c1-12)"
    printf '%s\n' "$VITASDK_SHA256" > "$dest/$marker"
    rm -rf "$stageroot"
    if [ -e "$VITASDK" ]; then
        mv "$VITASDK" "$backup" || { rm -rf "$dest"; fail "cannot back up the existing $VITASDK"; }
    fi
    if ! mv "$dest" "$VITASDK"; then
        [ ! -e "$backup" ] || mv "$backup" "$VITASDK"
        fail "cannot install the SDK; the previous directory was restored"
    fi
    rm -rf "$backup"
    info "VitaSDK $("$CC_BIN" --version 2>/dev/null | head -1)"
}

# --------------------------------------------------------------- packages
#
# Artifacts, not vdpm's package list: the CMake configure links these files, so
# these files are the requirement. vitaGL is listed with its whole dependency
# chain (vitaShaRK, SceShaccCgExt, mathneon) because SDL 1.2's OpenGL driver
# cannot initialise without them and the install of one pulls in all four.
missing_artifacts() {
    local entry pkg file missing=""
    for entry in "${VITA_PACKAGES[@]}"; do
        pkg="${entry%%:*}"
        file="${entry#*:}"
        [ -f "$VITASDK/$file" ] && continue
        # libpng is packaged as libpng.a or libpng16.a depending on the release.
        if [ "$file" = "arm-vita-eabi/lib/libpng.a" ] && [ -f "$VITASDK/arm-vita-eabi/lib/libpng16.a" ]; then
            continue
        fi
        case " $missing " in *" $pkg "*) ;; *) missing="$missing $pkg" ;; esac
    done
    printf '%s\n' "${missing# }"
}

install_packages() {
    local missing="$1" vdpm
    say "VitaSDK packages"
    if [ -z "$missing" ]; then
        info "all present in $VITASDK"
        return 0
    fi
    info "missing:$missing"
    [ "$OFFLINE" = 1 ] && fail "--offline: these packages are not in $VITASDK:$missing
       (reinstall from the pinned archive, or drop --offline)"

    vdpm="$VITASDK/bin/vdpm.exe"
    [ -x "$vdpm" ] || vdpm=$(command -v vdpm 2>/dev/null || true)
    [ -n "$vdpm" ] && [ -x "$vdpm" ] ||
        fail "vdpm not found in $VITASDK/bin; the SDK is incomplete"

    info "running: vdpm install$missing"
    # vdpm installs from the network and is the only step here that does; a
    # failure is reported with the manual command rather than swallowed, because
    # the build cannot link without what it was asked to fetch.
    # shellcheck disable=SC2086  # $missing is a list of package names
    if ! "$vdpm" install $missing; then
        fail "vdpm install$missing failed
       install them by hand, then rerun:
         $VITASDK_WIN/bin/vdpm.exe install$missing"
    fi
    local still
    still=$(missing_artifacts)
    [ -z "$still" ] || fail "vdpm reported success but these are still missing:$still"
    info "installed:$missing"
}

# ----------------------------------------------------------------- checks
check_vitasdk() {
    if ! vitasdk_present; then
        printf '  VitaSDK    MISSING  (%s)\n' "$VITASDK"
        printf '             run: port/psvita/build.sh --setup\n'
        return 1
    fi
    printf '  VitaSDK    ok       %s\n' "$("$CC_BIN" --version 2>/dev/null | head -1)"
    printf '             %s\n' "$VITASDK"
    printf '             toolchain file %s\n' "$TOOLCHAIN_FILE_WIN"
    return 0
}

check_packages() {
    local missing
    missing=$(missing_artifacts)
    if [ -n "$missing" ]; then
        printf '  packages   MISSING %s\n' "$missing"
        printf '             run: port/psvita/build.sh   (it runs vdpm install)\n'
        return 1
    fi
    printf '  packages   ok       sdl sdl_gfx sdl_mixer sdl_ttf zlib libpng vitaGL\n'
    return 0
}

check_host_tools() {
    local rc=0
    if command -v cmake >/dev/null 2>&1; then
        printf '  %-10s ok       %s\n' cmake "$(cmake --version 2>/dev/null | head -1)"
    else
        printf '  %-10s MISSING  (MSYS2: pacman -S cmake)\n' cmake
        rc=1
    fi
    if command -v ninja >/dev/null 2>&1; then
        printf '  %-10s ok       %s\n' ninja "$(ninja --version 2>/dev/null)"
    elif command -v make >/dev/null 2>&1; then
        printf '  %-10s ok       %s (no Ninja; CMake will use Unix Makefiles)\n' \
               make "$(make --version 2>/dev/null | head -1)"
    else
        printf '  %-10s MISSING  (install ninja or make)\n' "ninja/make"
        rc=1
    fi
    # VitaSDK's arm-vita-eabi-pkg-config is a bash wrapper around the *host*
    # pkg-config, which the CMakeLists needs for its pkg_check_modules calls.
    if command -v pkg-config >/dev/null 2>&1; then
        printf '  %-10s ok       %s\n' pkg-config "$(pkg-config --version 2>/dev/null)"
    else
        printf '  %-10s MISSING  (MSYS2: pacman -S pkgconf)\n' pkg-config
        rc=1
    fi
    return $rc
}

# ---------------------------------------------------------------- reality
if [ "$CLEAN" = 1 ]; then
    say "Cleaning $OUT"
    rm -rf "$OUT"
fi

# --check-tools reports and never touches the network, so it must not install the
# SDK it is being asked about. Every other mode downloads what is missing: that
# is what --offline exists to forbid, and making a first run a two-step ritual
# ("configure first, then really build") serves nobody.
if [ "$CHECK_TOOLS" = 1 ]; then
    say "Tools"
    rc=0
    check_vitasdk || rc=1
    check_packages || rc=1
    check_host_tools || rc=1
    if [ $rc -ne 0 ]; then
        say "Missing pieces"
        info "port/psvita/build.sh --setup   installs VitaSDK and its packages"
    fi
    exit $rc
fi

if [ "$DO_SETUP" = 1 ] || ! vitasdk_present; then
    install_vitasdk
fi
install_packages "$(missing_artifacts)"

if [ "$DO_SETUP" = 1 ]; then
    say "Setup complete; nothing was built"
    info "VitaSDK  $VITASDK_WIN"
    info "build with: port/psvita/build.sh"
    exit 0
fi

# ---------------------------------------------------------------- game data
#
# A VPK with no data cannot start, so the default is to pack the project's own
# M.U.G.E.N tree (install/, the same one make install and the Android build
# use) when it looks like one. DATA_DIR= / --data / --no-data override it.
if [ "$DATA_SET" = 0 ] && [ -f "$ROOT/install/data/system.def" ]; then
    DATA_DIR="$ROOT/install"
fi

# ------------------------------------------------------------------ build
[ -n "$JOBS" ] || JOBS=$(nproc 2>/dev/null || echo 4)
DATA_WIN=$(win_path "$DATA_DIR")

# Generator: Ninja when available (fast, and what the README shows). "MinGW
# Makefiles" is deliberately not a fallback -- it refuses to configure when
# sh.exe is on PATH, which it always is in the shell this script must run in.
# Unix Makefiles works with MSYS/w64devkit make.
if [ -n "${CMAKE_GENERATOR:-}" ]; then
    GENERATOR="$CMAKE_GENERATOR"
elif command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
elif command -v make >/dev/null 2>&1; then
    GENERATOR="Unix Makefiles"
else
    fail "neither ninja nor make found; install one (MSYS2: pacman -S ninja)"
fi

# A build directory configured against a different VitaSDK cannot be reused --
# CMake stores the toolchain file path in its cache and refuses a changed one.
# Clearing the cache is the only repair, and it has to be said out loud.
if [ -f "$OUT/CMakeCache.txt" ]; then
    prev=$(sed -n 's/^CMAKE_TOOLCHAIN_FILE:[A-Z]*=//p' "$OUT/CMakeCache.txt" | head -1)
    if [ -n "$prev" ] && [ "$prev" != "$TOOLCHAIN_FILE_WIN" ]; then
        info "note: $OUT was configured for $prev"
        info "      clearing its CMake cache for $TOOLCHAIN_FILE_WIN"
        rm -f "$OUT/CMakeCache.txt"
        rm -rf "$OUT/CMakeFiles"
    fi
fi

say "Configuring mugen-unleashed for the PS Vita"
info "VitaSDK   $VITASDK_WIN"
info "toolchain $TOOLCHAIN_FILE_WIN"
info "build     $BUILD_TYPE, $GENERATOR"
info "output    $OUT"
if [ -n "$DATA_DIR" ]; then
    info "game data $DATA_DIR (packed into the VPK)"
else
    info "game data none (engine-only VPK; see --data)"
fi

cmake_args=(
    -S "$HERE"
    -B "$OUT"
    -G "$GENERATOR"
    "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE_WIN"
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    "-DMUGEN_VITA_DATA_DIR=$DATA_WIN"
)
if [ -n "${TITLEID:-}" ]; then
    cmake_args+=("-DMUGEN_VITA_TITLEID=$TITLEID")
fi

cmake "${cmake_args[@]}"

say "Building"
info "jobs      $JOBS"
cmake --build "$OUT" --parallel "$JOBS"

self="$OUT/mugen-psvita.self"
vpk="$OUT/mugen-psvita.vpk"
[ -f "$self" ] || fail "the build reported success but $self is missing"
[ -f "$vpk" ] || fail "the build reported success but $vpk is missing"

size_of() { wc -c < "$1" | tr -d '[:space:]'; }
human()   { awk -v b="$1" 'BEGIN { printf "%.0f KB", b/1024 }'; }

say "Done"
info "$self  $(human "$(size_of "$self")")"
info "$vpk  $(human "$(size_of "$vpk")")"
info ""
info "Copy the VPK to the console and install it with VitaShell:"
info "  ux0:/  (USB or FTP mode), then VitaShell -> select mugen-psvita.vpk -> Install"
info ""
info "The SELF cannot run on this machine; see port/psvita/README.md for"
info "diagnosing a failed start on the console."
