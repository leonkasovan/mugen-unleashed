#!/usr/bin/env bash
# =============================================================================
# build_sdl2_companions.sh — cross-compile SDL2_mixer and SDL2_ttf for Android.
#
# The engine calls into both (sound.c, bgm.c, font.c, motif.c), and this
# repository does not vendor them: third_party/SDL_mixer-1.2.12 and
# SDL_ttf-2.0.11 are placeholder directories, because the desktop builds take
# them from the system through pkg-config (see the Makefile's STATIC_PKGS).
# Android has no such packages, so they are built here.
#
# SDL2-era versions are used, not 1.2: SDL_mixer 2's API is a superset of 1.2's
# for everything this engine calls, and it links against the SDL2 that the port
# already uses. The one incompatibility (Mix_LoadMUS_RW gained a freesrc
# argument) is handled at the call site in src/bgmplugins.c.
#
# Result is installed into build/android/android-deps/<abi>/, which build.sh expects.
#
#   ./tools/build_sdl2_companions.sh            # arm64-v8a
#   ANDROID_ABI=armeabi-v7a ./tools/build_sdl2_companions.sh
# =============================================================================
set -euo pipefail

ANDROID_HOME_DIR="${ANDROID_HOME_DIR:-/c/Android/SDK}"
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_HOME_DIR}/ndk/r27d}"
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-21}"

# SDL2 itself, from the reference build on this machine.
SDL2_DEPS_DIR="${SDL2_DEPS_DIR:-/c/Projects/ikemen-develop-update/build/android_arm64/android-deps/${ANDROID_ABI}}"

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
# Everything this script produces lives under build/android/, so one directory
# holds the whole Android build and can be deleted as a unit.
ANDROID_BUILD="$ROOT/build/android"
WORK="$ANDROID_BUILD/android-deps-src"
PREFIX="$ANDROID_BUILD/android-deps/${ANDROID_ABI}"
TC="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

SDL2TTF_VER="${SDL2TTF_VER:-release-2.20.2}"
SDL2MIXER_VER="${SDL2MIXER_VER:-release-2.6.3}"
FREETYPE_VER="${FREETYPE_VER:-2.12.1}"

say()  { printf '\n==> %s\n' "$*"; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

[ -f "$TC" ] || fail "NDK toolchain file not found: $TC"
[ -f "$SDL2_DEPS_DIR/lib/libSDL2.so" ] || fail "libSDL2.so not found in $SDL2_DEPS_DIR"
mkdir -p "$WORK" "$PREFIX"

download() {  # url dest
  if [ ! -e "$2" ]; then
    say "downloading $(basename "$2")"
    curl -sSL -o "$2" "$1" || fail "download failed: $1"
  fi
}

# ---- SDL2_ttf (+ the FreeType it needs) ------------------------------------

say "SDL2_ttf $SDL2TTF_VER"
TTF_SRC="$WORK/SDL_ttf-$SDL2TTF_VER"
if [ ! -d "$TTF_SRC" ]; then
  download "https://github.com/libsdl-org/SDL_ttf/archive/refs/tags/${SDL2TTF_VER}.zip" "$WORK/ttf.zip"
  ( cd "$WORK" && unzip -q ttf.zip && rm ttf.zip )
fi

# SDL2_ttf vendors FreeType through a download script that is not present in the
# release tarball, so it is fetched directly.
if [ ! -f "$TTF_SRC/external/freetype/CMakeLists.txt" ]; then
  say "FreeType $FREETYPE_VER"
  download "https://download.savannah.gnu.org/releases/freetype/freetype-${FREETYPE_VER}.tar.gz" "$WORK/ft.tar.gz"
  rm -rf "$TTF_SRC/external/freetype"
  mkdir -p "$TTF_SRC/external/freetype"
  ( cd "$WORK" && tar -xzf ft.tar.gz && \
    cp -a "freetype-${FREETYPE_VER}/." "$TTF_SRC/external/freetype/" && \
    rm -rf "freetype-${FREETYPE_VER}" ft.tar.gz )
fi

# FreeType 2.12 still declares cmake_minimum_required(VERSION 3.0), which CMake
# 3.27+ rejects outright. Widening the minimum is the documented migration.
sed -i 's/cmake_minimum_required(VERSION 3\.[0-4])/cmake_minimum_required(VERSION 3.5...3.27)/' \
  "$TTF_SRC/external/freetype/CMakeLists.txt" 2>/dev/null || true

cmake -S "$TTF_SRC" -B "$WORK/build-ttf" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI="$ANDROID_ABI" -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
  -DSDL2_DIR="$SDL2_DEPS_DIR/lib/cmake/SDL2" \
  -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF
cmake --build "$WORK/build-ttf" -j 8
cmake --install "$WORK/build-ttf"

# ---- SDL2_mixer ------------------------------------------------------------
#
# OGG is decoded by stb_vorbis and MP3 by dr_mp3, both of which SDL_mixer
# vendors, so no libogg/libvorbis/libmpg123 cross-build is needed. That keeps
# the dependency set to what the engine actually plays: WAV, OGG and MP3.

say "SDL2_mixer $SDL2MIXER_VER"
MIX_SRC="$WORK/SDL_mixer-$SDL2MIXER_VER"
if [ ! -d "$MIX_SRC" ]; then
  download "https://github.com/libsdl-org/SDL_mixer/archive/refs/tags/${SDL2MIXER_VER}.zip" "$WORK/mixer.zip"
  ( cd "$WORK" && unzip -q mixer.zip && rm mixer.zip )
fi

cmake -S "$MIX_SRC" -B "$WORK/build-mixer" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI="$ANDROID_ABI" -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
  -DSDL2_DIR="$SDL2_DEPS_DIR/lib/cmake/SDL2" \
  -DSDL2MIXER_VENDORED=ON -DSDL2MIXER_SAMPLES=OFF \
  -DSDL2MIXER_VORBIS=STB -DSDL2MIXER_MP3=DRMP3 \
  -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI=OFF -DSDL2MIXER_MOD=OFF
cmake --build "$WORK/build-mixer" -j 8
cmake --install "$WORK/build-mixer"

say "Done"
ls -1 "$PREFIX/lib"/*.so
