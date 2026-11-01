#!/usr/bin/env bash
# =============================================================================
# build.sh — build mugen-unleashed for Android and produce an installable APK.
#
# Modelled on ikemen-develop-update/tools/generate_android_via_native.sh, which
# is the proven pipeline on this machine: NDK r27d, SDK platform 34, build-tools
# 30.0.3, JDK 17, Gradle 8.1.1, and SDL2 2.32.10 staged into jniLibs.
#
# Unlike the reference script this does NOT download or install anything: the
# toolchain and a prebuilt libSDL2.so are already present. Point the script at
# them with the environment variables below.
#
#   ./build.sh                 # arm64-v8a release APK
#   ./build.sh --debug         # debug APK
#   ./build.sh --clean         # wipe staging first
#   ./build.sh --no-assets     # build the APK without game data (fast iteration)
#
# Result: build/mugen-unleashed-arm64-v8a.apk
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"  # repository root

# ---- configuration (override via environment) -------------------------------

ANDROID_HOME_DIR="${ANDROID_HOME_DIR:-/c/Android/SDK}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME_DIR}}"
ANDROID_HOME="${ANDROID_HOME:-${ANDROID_HOME_DIR}}"
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_HOME_DIR}/ndk/r27d}"
# Gradle 8 requires Java 17, and this machine's JAVA_HOME may point at an older
# JDK (it does -- Java 8). The launcher JVM is chosen from JAVA_HOME, so an
# older one fails with a misleading "No matching variant of
# com.android.tools.build:gradle" error rather than a Java version message.
#
# Precedence: MUGEN_JAVA_HOME, then the JDK the reference build installs, then
# whatever JAVA_HOME already says.
JDK17_DEFAULT=/c/Android/jdk-17
if [ -n "${MUGEN_JAVA_HOME:-}" ]; then
  JAVA_HOME="$MUGEN_JAVA_HOME"
elif [ -x "$JDK17_DEFAULT/bin/java" ]; then
  JAVA_HOME="$JDK17_DEFAULT"
fi
export ANDROID_SDK_ROOT ANDROID_HOME ANDROID_NDK_HOME JAVA_HOME

ANDROID_ABI="${ANDROID_ABI:-arm64-v8a}"

# Where libSDL2.so and its headers come from. The reference build already
# cross-compiled SDL2 2.32.10 for this ABI; reuse it rather than rebuild.
SDL2_DEPS_DIR="${SDL2_DEPS_DIR:-/c/Projects/ikemen-develop-update/build/android_arm64/android-deps/${ANDROID_ABI}}"

# libSDL2_mixer.so / libSDL2_ttf.so, cross-compiled by tools/build_sdl2_companions.sh
# (they are not vendored in this repository and Android has no system copies).
SDL2_COMPANION_DIR="${SDL2_COMPANION_DIR:-$ROOT/build/android/android-deps/${ANDROID_ABI}}"

# A real M.U.G.E.N 1.1 installation to pack as the game data.
GAME_DATA_DIR="${GAME_DATA_DIR:-/c/Projects/mugen-unleashed/install}"

BUILD_TYPE=release
CLEAN=0
PACK_ASSETS=1
for arg in "$@"; do
  case "$arg" in
    --debug)      BUILD_TYPE=debug ;;
    --clean)      CLEAN=1 ;;
    --no-assets)  PACK_ASSETS=0 ;;
    --help|-h)    awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

APP="$HERE/app"
OUT="$ROOT/build/android"
STAGE="$OUT/stage"

say()  { printf '\n==> %s\n' "$*"; }
fail() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

# ---- 0. sanity check the toolchain -----------------------------------------

say "Checking toolchain"
[ -d "$ANDROID_NDK_HOME" ]      || fail "NDK not found: $ANDROID_NDK_HOME"
[ -d "$ANDROID_SDK_ROOT" ]      || fail "SDK not found: $ANDROID_SDK_ROOT"
[ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/java" ] || \
  fail "JDK not found: ${JAVA_HOME:-<unset>} (Gradle 8 needs Java 17; set MUGEN_JAVA_HOME)"
[ -f "$SDL2_DEPS_DIR/lib/libSDL2.so" ] || fail "libSDL2.so not found in $SDL2_DEPS_DIR (set SDL2_DEPS_DIR)"
[ -f "$SDL2_COMPANION_DIR/lib/libSDL2_mixer.so" ] || \
  fail "libSDL2_mixer.so not found in $SDL2_COMPANION_DIR -- run tools/build_sdl2_companions.sh first"
[ -f "$SDL2_COMPANION_DIR/lib/libSDL2_ttf.so" ] || \
  fail "libSDL2_ttf.so not found in $SDL2_COMPANION_DIR -- run tools/build_sdl2_companions.sh first"
echo "    NDK:  $ANDROID_NDK_HOME"
JAVA_VERSION="$("$JAVA_HOME/bin/java" -version 2>&1 | head -1)"
echo "    JDK:  $JAVA_VERSION"
case "$JAVA_VERSION" in
  *\"1.8*|*\"1.7*|*\"11*)
    fail "Gradle 8 requires Java 17 or newer, found: $JAVA_VERSION
       Set MUGEN_JAVA_HOME to a JDK 17+ directory." ;;
esac
echo "    SDL2: $SDL2_DEPS_DIR"

# ---- 1. stage native libraries ---------------------------------------------

say "Staging native libraries for $ANDROID_ABI"
JNILIBS="$APP/src/main/jniLibs/$ANDROID_ABI"
mkdir -p "$JNILIBS"
cp -f "$SDL2_DEPS_DIR/lib/libSDL2.so" "$JNILIBS/"
cp -f "$SDL2_COMPANION_DIR/lib/libSDL2_mixer.so" "$JNILIBS/"
cp -f "$SDL2_COMPANION_DIR/lib/libSDL2_ttf.so" "$JNILIBS/"
for so in libSDL2.so libSDL2_mixer.so libSDL2_ttf.so; do
  echo "    $so: $(stat -c%s "$JNILIBS/$so") bytes"
done

# Headers for the CMake build (SDL2's own include dir, not the NDK's).
SDL2_INCLUDE="$SDL2_DEPS_DIR/include"
[ -d "$SDL2_INCLUDE/SDL2" ] || fail "SDL2 headers not found in $SDL2_INCLUDE/SDL2"

# ---- 2. stage game assets ---------------------------------------------------

mkdir -p "$OUT"
ASSETS="$APP/src/main/assets"
if [ "$CLEAN" = "1" ]; then
  say "Cleaning staging directories"
  rm -rf "$ASSETS" "$OUT/stage" "$APP/build" "$APP/.cxx"
fi
mkdir -p "$ASSETS"

if [ "$PACK_ASSETS" = "1" ]; then
  say "Staging game data from $GAME_DATA_DIR"
  [ -d "$GAME_DATA_DIR" ] || fail "game data not found: $GAME_DATA_DIR (set GAME_DATA_DIR, or pass --no-assets)"

  # Only what the engine actually opens at runtime. Deliberately excluded:
  #   *.exe, Elecbyte.MUGEN.libs (the original's own DLLs -- this build is
  #   self-contained), and mugen.log.
  ( cd "$GAME_DATA_DIR" && find . -type f \
      ! -name '*.exe' ! -name '*.dll' ! -name 'mugen.log' \
      ! -name 'stdout.txt' ! -name 'stderr.txt' \
      ! -path './Elecbyte.MUGEN.libs/*' \
      | sed 's|^\./||' | sort ) > "$OUT/asset-list.txt"

  COUNT=0
  while IFS= read -r rel; do
    mkdir -p "$ASSETS/$(dirname "$rel")"
    cp -f "$GAME_DATA_DIR/$rel" "$ASSETS/$rel"
    COUNT=$((COUNT + 1))
  done < "$OUT/asset-list.txt"
  echo "    $COUNT file(s), $(du -sh "$ASSETS" | cut -f1)"

  # The engine reads data/mugen.cfg (that is where a M.U.G.E.N 1.1 installation
  # keeps it), so check the real path rather than the root.
  [ -f "$ASSETS/data/mugen.cfg" ] || fail "data/mugen.cfg missing from the staged assets"
else
  say "Skipping game data (--no-assets)"
fi

# ---- 3. configure the APK package name and ABI ------------------------------

say "Configuring Gradle project"
# Gradle 8 needs Java 17. JAVA_HOME is exported above but the Gradle daemon and
# the Android plugin resolve the JDK through this property, so it is written
# explicitly -- without it the launched JVM can be an older java on PATH and the
# Android Gradle Plugin fails with a confusing variant-compatibility error.
printf 'org.gradle.java.home=%s\n' \
  "$(cygpath -w "$JAVA_HOME" 2>/dev/null || echo "$JAVA_HOME")" > "$HERE/gradle.properties.override"

# local.properties is a Java properties file: backslashes must be escaped and
# ':' in a Windows path must be too, or the Android plugin rejects the value.
props_escape() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/:/\\:/g'
}
{
  printf 'sdk.dir=%s\n' "$(props_escape "$(cygpath -w "$ANDROID_SDK_ROOT" 2>/dev/null || echo "$ANDROID_SDK_ROOT")")"
  # ndk.dir is NOT written: AGP 8 deprecates it (CXX5106) in favour of the
  # android.ndkVersion in app/build.gradle, which is already pinned there and
  # resolves through ANDROID_NDK_HOME / the SDK's ndk/ directory.
} > "$HERE/local.properties"

# Gradle 8 needs Java 17. This must be passed on the command line, not written
# into local.properties: org.gradle.java.home there is read after the launcher
# JVM has already started, so an older java on PATH would still be used and the
# build fails with a misleading plugin-variant error. -D selects the JVM.
GRADLE_JAVA_HOME="$(cygpath -w "$JAVA_HOME" 2>/dev/null || echo "$JAVA_HOME")"

# The CMake build reaches the engine tree by a relative path, and needs the SDL2
# include directory from the staging area, so pass it explicitly.
export MUGEN_ANDROID_SDL2_INCLUDE="$SDL2_INCLUDE"

# ---- 4. run Gradle ---------------------------------------------------------

say "Building APK ($BUILD_TYPE, $ANDROID_ABI)"
if [ "$BUILD_TYPE" = "debug" ]; then
  TASK=assembleDebug
else
  TASK=assembleRelease
fi

( cd "$HERE" && ./gradlew --no-daemon \
    "-Dorg.gradle.java.home=$GRADLE_JAVA_HOME" \
    -Psdl2IncludeDir="$SDL2_INCLUDE" \
    -Psdl2MixerTtfDir="$SDL2_COMPANION_DIR" \
    "$TASK" )

# ---- 5. collect the result -------------------------------------------------

say "Collecting APK"
mkdir -p "$OUT"
FOUND=$(find "$APP/build/outputs/apk" -name '*.apk' -type f | head -1)
[ -n "$FOUND" ] || fail "no APK produced; see the Gradle output above"
DEST="$OUT/mugen-unleashed-${ANDROID_ABI}.apk"
cp -f "$FOUND" "$DEST"
echo "    $DEST ($(stat -c%s "$DEST") bytes)"
echo
echo "Install with:"
echo "    $ANDROID_SDK_ROOT/platform-tools/adb.exe install -r $(cygpath -w "$DEST" 2>/dev/null || echo "$DEST")"
echo
echo "Runtime data is extracted on first launch to"
echo "    /sdcard/Android/data/org.mugen_unleashed.mugen/files/mugen"
