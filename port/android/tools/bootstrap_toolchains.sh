#!/usr/bin/env bash
# Toolchain bootstrap used by ../build.sh. No sudo, shell-profile edits or
# automatic SDK license acceptance. All default caches/installations are local.
android_shell_path() {
  if command -v cygpath >/dev/null 2>&1; then cygpath -u "$1"; else printf '%s\n' "$1"; fi
}
android_native_path() {
  if command -v cygpath >/dev/null 2>&1; then cygpath -am "$1"; else printf '%s\n' "$1"; fi
}
android_jdk_ok() {
  [ -x "$1/bin/java" ] && [ -x "$1/bin/javac" ] || return 1
  local version
  version=$("$1/bin/java" -version 2>&1) || return 1
  [[ "$version" =~ version[[:space:]]+\"17([.\"]|-) ]]
}
android_fetch() {
  # Metadata is fetched over HTTPS; archive data is additionally checksummed.
  command -v curl >/dev/null 2>&1 ||
    fail "curl is required to download tools (https://curl.se/download.html)"
  case "$1" in https://*) ;; *) fail "Refusing non-HTTPS download: $1";; esac
  if ! curl --fail --location --silent --show-error --proto '=https' \
      --retry 2 --connect-timeout 20 --max-time 600 --output "$2.part" "$1"; then
    rm -f "$2.part"
    fail "Download failed: $1; rerun --setup to retry"
  fi
  mv -f "$2.part" "$2"
}
android_archive() {
  local url=$1 algorithm=$2 checksum=$3 destination=$4
  if [ -f "$destination" ] &&
      "$PYTHON" "$METADATA" verify "$destination" "$algorithm" "$checksum"; then
    return
  fi
  rm -f "$destination"
  android_fetch "$url" "$destination"
  if ! "$PYTHON" "$METADATA" verify "$destination" "$algorithm" "$checksum"; then
    rm -f "$destination"
    fail "Downloaded archive failed its vendor checksum; nothing was extracted"
  fi
}
android_install_jdk() {
  local record url algorithm checksum kind archive stage home
  say "Downloading Temurin JDK 17 (official Adoptium metadata)"
  android_fetch \
    "https://api.adoptium.net/v3/assets/latest/17/hotspot?architecture=$JDK_ARCH&image_type=jdk&os=$JDK_OS&vendor=eclipse" \
    "$DOWNLOADS/jdk-metadata.json"
  record=$("$PYTHON" "$METADATA" jdk "$DOWNLOADS/jdk-metadata.json") ||
    fail "Could not read the JDK download metadata"
  IFS=$'\t' read -r url algorithm checksum kind <<< "$record"
  archive="$DOWNLOADS/${url##*/}"
  android_archive "$url" "$algorithm" "$checksum" "$archive"
  [ ! -e "$TOOLS/jdk-17" ] ||
    fail "Incomplete tool installation at $TOOLS/jdk-17; move it aside before retrying"
  stage=$(mktemp -d "$TOOLS/tmp/jdk.XXXXXX")
  if ! "$PYTHON" "$METADATA" extract "$archive" "$stage"; then
    rm -rf "$stage"; fail "Could not extract the verified JDK"
  fi
  home=$("$PYTHON" "$METADATA" jdk-home "$stage") ||
    { rm -rf "$stage"; fail "JDK archive has no unique Java home"; }
  home=$(android_shell_path "$home")
  if ! android_jdk_ok "$home"; then
    rm -rf "$stage"; fail "Downloaded JDK 17 cannot execute on this host"
  fi
  mv "$home" "$TOOLS/jdk-17"
  rm -rf "$stage"
  JAVA_HOME="$TOOLS/jdk-17"
}
android_install_cli() {
  local record url algorithm checksum kind archive stage target
  say "Downloading Android command-line tools $CMDLINE_VERSION"
  android_fetch "https://dl.google.com/android/repository/repository2-3.xml" \
    "$DOWNLOADS/android-repository.xml"
  record=$("$PYTHON" "$METADATA" sdk "$DOWNLOADS/android-repository.xml" \
    "$SDK_OS" "$CMDLINE_VERSION") || fail "Could not read Google's SDK metadata"
  IFS=$'\t' read -r url algorithm checksum kind <<< "$record"
  archive="$DOWNLOADS/${url##*/}"
  android_archive "$url" "$algorithm" "$checksum" "$archive"
  target="$ANDROID_SDK_ROOT/cmdline-tools/$CMDLINE_VERSION"
  [ ! -e "$target" ] ||
    fail "Incomplete command-line tools at $target; move them aside before retrying"
  stage=$(mktemp -d "$TOOLS/tmp/cmdline.XXXXXX")
  if ! "$PYTHON" "$METADATA" extract "$archive" "$stage"; then
    rm -rf "$stage"; fail "Could not extract the verified command-line tools"
  fi
  if [ ! -f "$stage/cmdline-tools/lib/sdkmanager-classpath.jar" ]; then
    rm -rf "$stage"; fail "Unexpected Android command-line tools archive layout"
  fi
  mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
  mv "$stage/cmdline-tools" "$target"
  rm -rf "$stage"
  SDKMANAGER_JAR="$target/lib/sdkmanager-classpath.jar"
}
android_sdkmanager() {
  # Use the same main class as Google's launcher. This avoids executing a .bat
  # from MSYS and quotes native Windows paths/classpaths as single arguments.
  "$JAVA_HOME/bin/java" \
    "-Djava.io.tmpdir=$(android_native_path "$TMPDIR")" \
    "-Duser.home=$(android_native_path "$ANDROID_USER_HOME")" \
    -classpath "$(android_native_path "$SDKMANAGER_JAR")" \
    com.android.sdklib.tool.sdkmanager.SdkManagerCli \
    "--sdk_root=$(android_native_path "$ANDROID_SDK_ROOT")" "$@"
}
android_prepare_toolchains() {
  local os arch candidate detected ndk_revision
  TOOLS=$(android_shell_path "${MUGEN_ANDROID_TOOLS_DIR:-$OUT/toolchains}")
  mkdir -p "$TOOLS/tmp" "$TOOLS/downloads"
  TOOLS=$(cd "$TOOLS" && pwd)
  DOWNLOADS="$TOOLS/downloads"
  export TMPDIR="$TOOLS/tmp" TMP="$TOOLS/tmp" TEMP="$TOOLS/tmp"
  export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$OUT/gradle-user-home}"
  export ANDROID_USER_HOME="${ANDROID_USER_HOME:-$TOOLS/android-user-home}"
  mkdir -p "$GRADLE_USER_HOME" "$ANDROID_USER_HOME"

  PYTHON="${MUGEN_PYTHON:-}"
  if [ -z "$PYTHON" ]; then
    if command -v python3 >/dev/null 2>&1; then PYTHON=python3; else PYTHON=python; fi
  fi
  "$PYTHON" -c 'import sys; assert sys.version_info >= (3, 8)' 2>/dev/null ||
    fail "Python 3.8+ is required; install from https://www.python.org/downloads/ or set MUGEN_PYTHON"
  METADATA="$HERE/tools/toolchain_metadata.py"

  os=$(uname -s); arch=$(uname -m)
  case "$os/$arch" in
    Linux/x86_64) SDK_OS=linux; JDK_OS=linux; JDK_ARCH=x64; NDK_HOST=linux-x86_64; EXE= ;;
    Darwin/x86_64) SDK_OS=macosx; JDK_OS=mac; JDK_ARCH=x64; NDK_HOST=darwin-x86_64; EXE= ;;
    Darwin/arm64) SDK_OS=macosx; JDK_OS=mac; JDK_ARCH=aarch64; NDK_HOST=darwin-x86_64; EXE= ;;
    MINGW*/x86_64|MSYS*/x86_64|CYGWIN*/x86_64)
      SDK_OS=windows; JDK_OS=windows; JDK_ARCH=x64; NDK_HOST=windows-x86_64; EXE=.exe ;;
    *) fail "Unsupported Android build host: $os/$arch (use Linux x86_64, macOS or Windows x64/MSYS2)" ;;
  esac

  # Match the project rather than a developer's directory label such as r27d.
  NDK_VERSION=$(sed -n 's/^[[:space:]]*ndkVersion[[:space:]]*"\([^"]*\)".*/\1/p' "$HERE/app/build.gradle")
  SDK_PLATFORM=$(sed -n 's/^[[:space:]]*compileSdkVersion[[:space:]]*\([0-9]*\).*/\1/p' "$HERE/app/build.gradle")
  CMAKE_VERSION=$(sed -n 's/^[[:space:]]*version[[:space:]]*"\([0-9.]*\)".*/\1/p' "$HERE/app/build.gradle")
  BUILD_TOOLS_VERSION=$(sed -n 's/^[[:space:]]*buildToolsVersion[[:space:]]*"\([^"]*\)".*/\1/p' "$HERE/app/build.gradle")
  [ -n "$NDK_VERSION" ] && [ -n "$SDK_PLATFORM" ] &&
    [ -n "$CMAKE_VERSION" ] && [ -n "$BUILD_TOOLS_VERSION" ] ||
    fail "Could not read SDK/NDK/CMake/build-tools pins from app/build.gradle"
  CMDLINE_VERSION=19.0

  ANDROID_SDK_ROOT=$(android_shell_path "${ANDROID_SDK_ROOT:-${ANDROID_HOME:-${ANDROID_HOME_DIR:-$TOOLS/sdk}}}")
  ANDROID_HOME="$ANDROID_SDK_ROOT"
  ANDROID_NDK_HOME=$(android_shell_path "${ANDROID_NDK_HOME:-$ANDROID_SDK_ROOT/ndk/$NDK_VERSION}")

  JAVA_HOME=$(android_shell_path "${JAVA_HOME:-}")
  if [ -n "${MUGEN_JAVA_HOME:-}" ]; then
    JAVA_HOME=$(android_shell_path "$MUGEN_JAVA_HOME")
    android_jdk_ok "$JAVA_HOME" ||
      fail "MUGEN_JAVA_HOME must identify a runnable JDK 17: $JAVA_HOME"
  elif ! android_jdk_ok "$JAVA_HOME"; then
    JAVA_HOME=
    for candidate in "$TOOLS/jdk-17" /c/Android/jdk-17; do
      if android_jdk_ok "$candidate"; then JAVA_HOME="$candidate"; break; fi
    done
    if [ -z "$JAVA_HOME" ] && command -v java >/dev/null 2>&1; then
      detected=$(java -XshowSettings:properties -version 2>&1 |
        sed -n 's/^[[:space:]]*java.home = //p') || true
      if [ -n "$detected" ]; then
        detected=$(android_shell_path "$detected")
        if android_jdk_ok "$detected"; then JAVA_HOME="$detected"; fi
      fi
    fi
    if [ -z "$JAVA_HOME" ]; then
      [ "$DOWNLOAD_TOOLS" = 1 ] ||
        fail "JDK 17 missing. Run port/android/build.sh --setup, or set MUGEN_JAVA_HOME"
      android_install_jdk
    fi
  fi
  export JAVA_HOME ANDROID_SDK_ROOT ANDROID_HOME ANDROID_NDK_HOME
  export PATH="$JAVA_HOME/bin:$ANDROID_SDK_ROOT/cmake/$CMAKE_VERSION/bin:$ANDROID_SDK_ROOT/platform-tools:$PATH"

  MISSING_PACKAGES=()
  [ -f "$ANDROID_SDK_ROOT/platforms/android-$SDK_PLATFORM/android.jar" ] ||
    MISSING_PACKAGES+=("platforms;android-$SDK_PLATFORM")
  [ -x "$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS_VERSION/aapt$EXE" ] ||
    MISSING_PACKAGES+=("build-tools;$BUILD_TOOLS_VERSION")
  [ -x "$ANDROID_SDK_ROOT/platform-tools/adb$EXE" ] ||
    MISSING_PACKAGES+=("platform-tools")
  if [ ! -x "$ANDROID_SDK_ROOT/cmake/$CMAKE_VERSION/bin/cmake$EXE" ] ||
      [ ! -x "$ANDROID_SDK_ROOT/cmake/$CMAKE_VERSION/bin/ninja$EXE" ]; then
    MISSING_PACKAGES+=("cmake;$CMAKE_VERSION")
  fi
  if [ ! -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" ] ||
      [ ! -x "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_HOST/bin/clang$EXE" ]; then
    [ "$ANDROID_NDK_HOME" = "$ANDROID_SDK_ROOT/ndk/$NDK_VERSION" ] ||
      fail "Custom ANDROID_NDK_HOME is incomplete: $ANDROID_NDK_HOME; unset it to install the pinned SDK NDK"
    MISSING_PACKAGES+=("ndk;$NDK_VERSION")
  fi
  if [ "${#MISSING_PACKAGES[@]}" -gt 0 ]; then
    [ "$DOWNLOAD_TOOLS" = 1 ] ||
      fail "Missing SDK packages: ${MISSING_PACKAGES[*]}. Run port/android/build.sh --setup (or --download-tools)"
    SDKMANAGER_JAR="$ANDROID_SDK_ROOT/cmdline-tools/$CMDLINE_VERSION/lib/sdkmanager-classpath.jar"
    if [ ! -f "$SDKMANAGER_JAR" ]; then
      # A newer latest/ may require a newer JVM than this project's JDK 17.
      # Reuse it only when its own revision matches the bootstrap pin.
      if [ -f "$ANDROID_SDK_ROOT/cmdline-tools/latest/source.properties" ] &&
          grep -Eq '^Pkg.Revision[[:space:]]*=[[:space:]]*19\.0([[:space:]]|$)' \
            "$ANDROID_SDK_ROOT/cmdline-tools/latest/source.properties" &&
          [ -f "$ANDROID_SDK_ROOT/cmdline-tools/latest/lib/sdkmanager-classpath.jar" ]; then
        SDKMANAGER_JAR="$ANDROID_SDK_ROOT/cmdline-tools/latest/lib/sdkmanager-classpath.jar"
      else
        android_install_cli
      fi
    fi
    say "Installing only missing SDK packages: ${MISSING_PACKAGES[*]}"
    say "sdkmanager may ask you to review and accept Android SDK licenses"
    android_sdkmanager --install "${MISSING_PACKAGES[@]}" ||
      fail "sdkmanager failed; review its error/licenses and rerun --setup"
  fi

  [ -f "$ANDROID_SDK_ROOT/platforms/android-$SDK_PLATFORM/android.jar" ] &&
    [ -x "$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS_VERSION/aapt$EXE" ] &&
    [ -x "$ANDROID_SDK_ROOT/platform-tools/adb$EXE" ] &&
    [ -x "$ANDROID_SDK_ROOT/cmake/$CMAKE_VERSION/bin/cmake$EXE" ] &&
    [ -x "$ANDROID_SDK_ROOT/cmake/$CMAKE_VERSION/bin/ninja$EXE" ] &&
    [ -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" ] &&
    [ -x "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_HOST/bin/clang$EXE" ] ||
    fail "Tool installation is incomplete; check sdkmanager output/licenses and rerun --setup"
  [ -f "$ANDROID_NDK_HOME/source.properties" ] || fail "NDK source.properties missing: $ANDROID_NDK_HOME"
  ndk_revision=$(sed -n 's/^Pkg.Revision[[:space:]]*=[[:space:]]*//p' "$ANDROID_NDK_HOME/source.properties" | tr -d '\r')
  [ "$ndk_revision" = "$NDK_VERSION" ] ||
    fail "NDK revision $ndk_revision does not match Gradle's $NDK_VERSION at $ANDROID_NDK_HOME"

  {
    printf 'export JAVA_HOME=%q\n' "$JAVA_HOME"
    printf 'export ANDROID_SDK_ROOT=%q\n' "$ANDROID_SDK_ROOT"
    printf 'export ANDROID_HOME=%q\n' "$ANDROID_HOME"
    printf 'export ANDROID_NDK_HOME=%q\n' "$ANDROID_NDK_HOME"
    printf 'export GRADLE_USER_HOME=%q\n' "$GRADLE_USER_HOME"
    printf 'export ANDROID_USER_HOME=%q\n' "$ANDROID_USER_HOME"
    printf 'export TMPDIR=%q TMP=%q TEMP=%q\n' "$TMPDIR" "$TMP" "$TEMP"
    printf 'export PATH=%q:$PATH\n' "$JAVA_HOME/bin:$ANDROID_SDK_ROOT/cmake/$CMAKE_VERSION/bin:$ANDROID_SDK_ROOT/platform-tools"
  } > "$TOOLS/env.sh"
  say "Toolchain ready: JDK 17, SDK $SDK_PLATFORM, build-tools $BUILD_TOOLS_VERSION, NDK $NDK_VERSION, CMake $CMAKE_VERSION"
  printf '    SDK: %s\n    NDK: %s\n    JDK: %s\n    Environment: %s/env.sh\n' \
    "$ANDROID_SDK_ROOT" "$ANDROID_NDK_HOME" "$JAVA_HOME" "$TOOLS"
}
