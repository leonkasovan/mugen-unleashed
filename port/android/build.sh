#!/usr/bin/env bash
# Build the Android arm64-v8a APK with the project's pinned toolchain.
#
#   port/android/build.sh                  release APK, existing tools
#   port/android/build.sh --debug          debug APK
#   port/android/build.sh --setup          download missing tools, then stop
#   port/android/build.sh --download-tools  download missing tools, then build
#   port/android/build.sh --check-tools     validate tools without building
#   port/android/build.sh --offline         require cached tools/Gradle/deps
#   port/android/build.sh --clean           clean generated app build/staging
#   port/android/build.sh --no-assets       package no game data
#
# Downloads require curl and Python 3.8+. SDK licenses are shown by sdkmanager;
# this script does not accept them automatically. See README.md for SDL2 libs.
# Output: build/android/mugen-unleashed-arm64-v8a.apk
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
APP="$HERE/app"
OUT="$ROOT/build/android"
BUILD_TYPE=release
CLEAN=0
PACK_ASSETS=1
DOWNLOAD_TOOLS=0
CHECK_TOOLS=0
OFFLINE=0
say()  { printf '\n==> %s\n' "$*"; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
usage() {
  awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"
}
for arg in "$@"; do
  case "$arg" in
    --debug) BUILD_TYPE=debug ;;
    --clean) CLEAN=1 ;;
    --no-assets) PACK_ASSETS=0 ;;
    --setup) DOWNLOAD_TOOLS=1; CHECK_TOOLS=1 ;;
    --download-tools) DOWNLOAD_TOOLS=1 ;;
    --check-tools) CHECK_TOOLS=1 ;;
    --offline) OFFLINE=1 ;;
    --help|-h) usage; exit 0 ;;
    *) printf 'unknown argument: %s\n' "$arg" >&2; exit 2 ;;
  esac
done
if [ "$OFFLINE" = 1 ] && [ "$DOWNLOAD_TOOLS" = 1 ]; then
  fail "--offline cannot be combined with --setup or --download-tools"
fi
ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"
[ "$ANDROID_ABI" = arm64-v8a ] ||
  fail "app/build.gradle currently packages arm64-v8a only; requested ABI $ANDROID_ABI is unsupported"

# Downloads and caches are inside the repository by default. Existing SDK/JDK
# overrides are honored; sdkmanager installs side-by-side pinned SDK packages.
# shellcheck source=tools/bootstrap_toolchains.sh
source "$HERE/tools/bootstrap_toolchains.sh"
android_prepare_toolchains
if [ "$CHECK_TOOLS" = 1 ]; then exit 0; fi

SDL2_DEPS_DIR=$(android_shell_path "${SDL2_DEPS_DIR:-$OUT/sdl2/$ANDROID_ABI}")
SDL2_COMPANION_DIR=$(android_shell_path "${SDL2_COMPANION_DIR:-$OUT/android-deps/$ANDROID_ABI}")
GAME_DATA_DIR=$(android_shell_path "${GAME_DATA_DIR:-$ROOT/install}")
SDL2_INCLUDE="$SDL2_DEPS_DIR/include"
[ -f "$SDL2_INCLUDE/SDL2/SDL.h" ] ||
  fail "SDL2 headers missing in $SDL2_INCLUDE/SDL2; build SDL2 or set SDL2_DEPS_DIR (see README.md)"
[ -f "$SDL2_DEPS_DIR/lib/libSDL2.so" ] ||
  fail "libSDL2.so missing in $SDL2_DEPS_DIR; follow README.md or set SDL2_DEPS_DIR"
for name in mixer ttf; do
  [ -f "$SDL2_COMPANION_DIR/lib/libSDL2_$name.so" ] ||
    fail "libSDL2_$name.so missing. Source $TOOLS/env.sh and run port/android/tools/build_sdl2_companions.sh (set SDL2_DEPS_DIR)"
done
[ -f "$HERE/gradle/wrapper/gradle-wrapper.jar" ] ||
  fail "Tracked Gradle wrapper JAR missing; restore port/android/gradle/wrapper/gradle-wrapper.jar from git"
GRADLE_LAUNCHER="$HERE/gradlew"
if [ "$OFFLINE" = 1 ]; then
  CACHE="$GRADLE_USER_HOME/wrapper/dists/gradle-8.1.1-bin"
  if [ -d "$CACHE" ]; then
    while IFS= read -r candidate; do
      dist="${candidate%/bin/gradle}"
      [ -f "$dist/lib/gradle-launcher-8.1.1.jar" ] &&
        [ -f "${dist%/*}/gradle-8.1.1-bin.zip.ok" ] || continue
      GRADLE_LAUNCHER="$candidate"
      break
    done < <(find "$CACHE" -type f -path '*/gradle-8.1.1/bin/gradle')
  fi
  [ "$GRADLE_LAUNCHER" != "$HERE/gradlew" ] ||
    fail "Offline Gradle 8.1.1 distribution is not cached or incomplete; run an online build once"
  # Invoke the complete pinned distribution directly, so the wrapper cannot
  # attempt a download when the URL-hash cache directory differs.
fi

if [ "$CLEAN" = 1 ]; then
  say "Cleaning generated Android build output"
  rm -rf "$OUT/stage" "$APP/build" "$APP/.cxx"
fi
say "Staging native libraries for $ANDROID_ABI"
JNILIBS="$APP/src/main/jniLibs/$ANDROID_ABI"
mkdir -p "$JNILIBS"
cp -f "$SDL2_DEPS_DIR/lib/libSDL2.so" "$JNILIBS/"
cp -f "$SDL2_COMPANION_DIR/lib/libSDL2_mixer.so" "$JNILIBS/"
cp -f "$SDL2_COMPANION_DIR/lib/libSDL2_ttf.so" "$JNILIBS/"
file_size() { wc -c < "$1" | tr -d '[:space:]'; }
for so in libSDL2.so libSDL2_mixer.so libSDL2_ttf.so; do
  printf '    %s: %s bytes\n' "$so" "$(file_size "$JNILIBS/$so")"
done

# Recreate generated staging every run, including --no-assets: an old payload
# must not leak into an APK requested without assets.
ASSETS="$APP/src/main/assets"
rm -rf "$ASSETS"
mkdir -p "$ASSETS"
if [ "$PACK_ASSETS" = 1 ]; then
  say "Staging game data from $GAME_DATA_DIR"
  [ -f "$GAME_DATA_DIR/data/mugen.cfg" ] ||
    fail "data/mugen.cfg missing in GAME_DATA_DIR=$GAME_DATA_DIR; supply game data or use --no-assets"
  ( cd "$GAME_DATA_DIR" && find . -type f \
      ! -name '*.exe' ! -name '*.dll' ! -name 'mugen.log' \
      ! -name 'stdout.txt' ! -name 'stderr.txt' \
      ! -path './Elecbyte.MUGEN.libs/*' |
      sed 's|^\./||' | sort ) > "$OUT/asset-list.txt"
  COUNT=0
  while IFS= read -r rel; do
    mkdir -p "$ASSETS/$(dirname "$rel")"
    cp -f "$GAME_DATA_DIR/$rel" "$ASSETS/$rel"
    COUNT=$((COUNT + 1))
  done < "$OUT/asset-list.txt"
  printf '    %s file(s)\n' "$COUNT"
else
  say "Skipping game data (--no-assets)"
fi

say "Configuring Gradle project"
# Use forward slashes for Java/native Windows paths, and escape Java-properties
# metacharacters. Do not rewrite the tracked gradle.properties.override file.
props_escape() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/:/\\:/g'
}
printf 'sdk.dir=%s\n' "$(props_escape "$(android_native_path "$ANDROID_SDK_ROOT")")" \
  > "$HERE/local.properties"
export MUGEN_ANDROID_SDL2_INCLUDE="$SDL2_INCLUDE"
GRADLE_ARGS=(
  --no-daemon
  "-Dorg.gradle.java.home=$(android_native_path "$JAVA_HOME")"
  "-Djava.io.tmpdir=$(android_native_path "$TMPDIR")"
  "-Duser.home=$(android_native_path "$ANDROID_USER_HOME")"
  "-PmugenNdkPath=$(android_native_path "$ANDROID_NDK_HOME")"
  "-Psdl2IncludeDir=$(android_native_path "$SDL2_INCLUDE")"
  "-Psdl2MixerTtfDir=$(android_native_path "$SDL2_COMPANION_DIR")"
)
if [ "$OFFLINE" = 1 ]; then GRADLE_ARGS+=(--offline); fi
if [ "$BUILD_TYPE" = debug ]; then TASK=assembleDebug; else TASK=assembleRelease; fi

say "Building APK ($BUILD_TYPE, $ANDROID_ABI)"
( cd "$HERE" && bash "$GRADLE_LAUNCHER" "${GRADLE_ARGS[@]}" "$TASK" )

# Restrict collection to the requested variant, never a stale debug/release APK
# found first in a recursive traversal.
APK_DIR="$APP/build/outputs/apk/$BUILD_TYPE"
[ -d "$APK_DIR" ] || fail "No $BUILD_TYPE APK output directory; see Gradle output"
FOUND=$(find "$APK_DIR" -maxdepth 1 -type f -name '*.apk')
[ -n "$FOUND" ] || fail "No $BUILD_TYPE APK produced; see Gradle output"
case "$FOUND" in *$'\n'*) fail "Multiple APKs in $APK_DIR; select an output explicitly";; esac
DEST="$OUT/mugen-unleashed-$ANDROID_ABI.apk"
cp -f "$FOUND" "$DEST"
printf '\nAPK: %s (%s bytes)\n' "$DEST" "$(file_size "$DEST")"
printf 'Install: "%s/platform-tools/adb%s" install -r "%s"\n' \
  "$ANDROID_SDK_ROOT" "$EXE" "$(android_native_path "$DEST")"
printf 'Runtime data: app-specific external files/mugen (internal fallback); see logcat -s mugen\n'
