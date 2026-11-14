@echo off
rem ===========================================================================
rem  build_sdl2_companions.bat -- cross-compile SDL2_ttf and SDL2_mixer for
rem  Android, from Windows cmd.exe.
rem
rem  This is the cmd.exe replacement for the removed
rem  tools/build_sdl2_companions.sh, which went with the MSYS2 entry points. It
rem  does the same work: download the pinned upstream releases, build them
rem  against the NDK and the already-built SDL2 prefix, and install
rem  libSDL2_ttf.so / libSDL2_mixer.so into build\android\android-deps\ABI,
rem  which port\android\build.bat expects.
rem
rem  SDL2-era versions are used, not 1.2: SDL_mixer 2's API is a superset of
rem  1.2's for everything this engine calls, and it links against the SDL2 the
rem  port already uses. The one incompatibility (Mix_LoadMUS_RW gained a
rem  freesrc argument) is handled at the call site in src/bgmplugins.c.
rem
rem    port\android\tools\build_sdl2_companions.bat
rem    port\android\tools\build_sdl2_companions.bat --help
rem
rem  Environment (the same names and defaults the .sh read):
rem    ANDROID_ABI              default arm64-v8a
rem    ANDROID_PLATFORM         default android-21
rem    ANDROID_SDK_ROOT         SDK root, then ANDROID_HOME, ANDROID_HOME_DIR,
rem                             build\android\toolchains\sdk, C:\Android\SDK
rem    ANDROID_NDK_HOME         default SDK\ndk, version pinned in app\build.gradle
rem    SDL2_DEPS_DIR            SDL2 prefix, default build\android\sdl2\ABI
rem    MUGEN_ANDROID_TOOLS_DIR  cache root, default build\android\toolchains
rem    SDL2_VER                 SDL2 release, default 2.32.10
rem    SDL2TTF_VER              default release-2.20.2
rem    SDL2MIXER_VER            default release-2.6.3
rem    FREETYPE_VER             default 2.12.1
rem
rem  If the SDL2 prefix is absent, SDL2's own release tarball is downloaded
rem  from GitHub with curl (never git), cross-compiled, and installed there
rem  first. Everything then builds against it. Nothing is installed outside the
rem  repository: sources, archives and outputs all live under build\android\.
rem
rem  Divergences from the .sh, because cmd.exe has no unzip, tar or sed:
rem    * archives are unpacked with System32's bsdtar (tar.exe), which reads
rem      .zip and .tar.gz; the .sh used unzip/tar.
rem    * the one FreeType edit the .sh made with sed is a PowerShell one-liner,
rem      and it stays best-effort (the .sh used `|| true`).
rem    * the build tools come from the SDK's pinned cmake+ninja package rather
rem      than whatever is on PATH, so the same CMake version is used as Gradle's.
rem ===========================================================================

setlocal EnableExtensions EnableDelayedExpansion

rem The script's own directory (port\android\tools), its parent (port\android)
rem and the repository root, without a trailing backslash.
for %%I in ("%~dp0.") do set "TOOLSDIR=%%~fI"
for %%I in ("%~dp0..") do set "ANDROIDDIR=%%~fI"
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "APP=%ANDROIDDIR%\app"
set "OUT=%ROOT%\build\android"

rem ------------------------------------------------------------------- args
:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
echo error: unknown argument: %~1  ^(--help^)
exit /b 2
:parsed

rem ------------------------------------------------------------- defaults
if not defined ANDROID_ABI set "ANDROID_ABI=arm64-v8a"
if not defined ANDROID_PLATFORM set "ANDROID_PLATFORM=android-21"
if not defined SDL2_VER set "SDL2_VER=2.32.10"
if not defined SDL2TTF_VER set "SDL2TTF_VER=release-2.20.2"
if not defined SDL2MIXER_VER set "SDL2MIXER_VER=release-2.6.3"
if not defined FREETYPE_VER set "FREETYPE_VER=2.12.1"
if defined MUGEN_ANDROID_TOOLS_DIR ( set "TOOLS=%MUGEN_ANDROID_TOOLS_DIR%" ) else (
    set "TOOLS=%OUT%\toolchains"
)
for %%I in ("%TOOLS%") do set "TOOLS=%%~fI"

if not defined SDL2_DEPS_DIR set "SDL2_DEPS_DIR=%OUT%\sdl2\%ANDROID_ABI%"
for %%I in ("%SDL2_DEPS_DIR%") do set "SDL2_DEPS_DIR=%%~fI"
if defined SDL2_COMPANION_DIR ( set "PREFIX=%SDL2_COMPANION_DIR%" ) else (
    set "PREFIX=%OUT%\android-deps\%ANDROID_ABI%"
)
for %%I in ("%PREFIX%") do set "PREFIX=%%~fI"
set "WORK=%OUT%\android-deps-src"

rem --------------------------------------------------------- host tools
rem AGENTS.md's first rule: the archives, the sources and every scratch file
rem stay inside the repository. The .sh relied on the env.sh exports; here the
rem same directory is set directly, so this script also works on its own.
if not exist "%TOOLS%\tmp" mkdir "%TOOLS%\tmp"
set "TMPDIR=%TOOLS%\tmp"
set "TMP=%TOOLS%\tmp"
set "TEMP=%TOOLS%\tmp"

if not exist "%WORK%" mkdir "%WORK%"
if not exist "%PREFIX%" mkdir "%PREFIX%"

set "CURL=%SystemRoot%\System32\curl.exe"
if exist "%CURL%" goto have_curl
set "CURL=curl.exe"
where curl.exe >nul 2>&1
if errorlevel 1 (
    echo error: curl.exe not found; it ships with Windows 10 1803 and later
    exit /b 1
)
:have_curl
set "TAR=%SystemRoot%\System32\tar.exe"
if not exist "%TAR%" (
    echo error: %TAR% not found; System32's bsdtar ships with Windows 10 1803
    exit /b 1
)

rem ------------------------------------------------------- SDK / NDK / CMake
if not defined ANDROID_SDK_ROOT if defined ANDROID_HOME set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
if not defined ANDROID_SDK_ROOT if defined ANDROID_HOME_DIR set "ANDROID_SDK_ROOT=%ANDROID_HOME_DIR%"
rem Same search order as port\android\build.bat: an SDK a Windows user already
rem installed is used before the local cache.
if not defined ANDROID_SDK_ROOT if exist "%OUT%\toolchains\sdk\platforms" set "ANDROID_SDK_ROOT=%OUT%\toolchains\sdk"
if not defined ANDROID_SDK_ROOT if exist "C:\Android\SDK\platforms" set "ANDROID_SDK_ROOT=C:\Android\SDK"
if not defined ANDROID_SDK_ROOT (
    echo error: Android SDK not found. Set ANDROID_SDK_ROOT to an installed SDK
    echo        ^(see https://developer.android.com/studio^), for example
    echo        set ANDROID_SDK_ROOT=C:\Android\SDK
    exit /b 1
)
for %%I in ("%ANDROID_SDK_ROOT%") do set "ANDROID_SDK_ROOT=%%~fI"

rem The pins are read out of app\build.gradle, the same four lines Gradle reads,
rem so a version bump cannot leave this script building against another one.
set "GRADLE_FILE=%APP%\build.gradle"
if not exist "%GRADLE_FILE%" (
    echo error: %GRADLE_FILE% is missing; restore port\android\app\build.gradle from git
    exit /b 1
)
set "NDK_VERSION="
for /f tokens^=2^ delims^=^" %%V in ('findstr /r /c:"^[ ]*ndkVersion[ ]" "%GRADLE_FILE%"') do if not defined NDK_VERSION set "NDK_VERSION=%%V"
set "CMAKE_VERSION="
for /f tokens^=2^ delims^=^" %%V in ('findstr /r /c:"^[ ]*version[ ]" "%GRADLE_FILE%"') do if not defined CMAKE_VERSION set "CMAKE_VERSION=%%V"
if not defined NDK_VERSION (
    echo error: Could not read ndkVersion from %GRADLE_FILE%
    exit /b 1
)
if not defined CMAKE_VERSION (
    echo error: Could not read the CMake version from %GRADLE_FILE%
    exit /b 1
)

set "CMAKE=%ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo error: CMake %CMAKE_VERSION% missing from %ANDROID_SDK_ROOT%\cmake
    echo        install it with:
    echo          "%ANDROID_SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat" --sdk_root="%ANDROID_SDK_ROOT%" --install cmake;%CMAKE_VERSION%
    echo        ^(port\android\build.bat --setup reports the same thing^)
    exit /b 1
)
if not exist "%ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin\ninja.exe" (
    echo error: ninja missing from %ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin
    echo        reinstall cmake;%CMAKE_VERSION% with the SDK's sdkmanager.bat
    exit /b 1
)
rem The SDK's cmake package carries its ninja beside cmake.exe, so putting that
rem directory first is what makes `-G Ninja` resolve the pinned one rather than
rem a CMake or ninja someone else installed.
set "PATH=%ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin;%PATH%"

if not defined ANDROID_NDK_HOME set "ANDROID_NDK_HOME=%ANDROID_SDK_ROOT%\ndk\%NDK_VERSION%"
for %%I in ("%ANDROID_NDK_HOME%") do set "ANDROID_NDK_HOME=%%~fI"
set "TC=%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake"
if not exist "%TC%" (
    echo error: NDK toolchain file not found: %TC%
    echo        set ANDROID_NDK_HOME to NDK %NDK_VERSION%, or install it with
    echo          "%ANDROID_SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat" --sdk_root="%ANDROID_SDK_ROOT%" --install ndk;%NDK_VERSION%
    exit /b 1
)
if not exist "%ANDROID_NDK_HOME%\toolchains\llvm\prebuilt\windows-x86_64\bin\clang.exe" (
    echo error: NDK is incomplete: %ANDROID_NDK_HOME%
    echo        reinstall ndk;%NDK_VERSION% and make sure source.properties matches
    exit /b 1
)

rem --------------------------------------------------------- SDL2 prefix
rem Build SDL2 from its release tarball when the prefix is absent. curl fetches
rem the archive and the recipe is the README's (shared, no static, no tests).
if exist "%SDL2_DEPS_DIR%\lib\libSDL2.so" goto sdl2_ready
echo.
echo ==^> SDL2 %SDL2_VER%
set "SDL2_SRC=%WORK%\SDL2-%SDL2_VER%"
if exist "%SDL2_SRC%" goto sdl2_source_ready
set "DL_URL=https://github.com/libsdl-org/SDL/releases/download/release-%SDL2_VER%/SDL2-%SDL2_VER%.tar.gz"
set "DL_DEST=%WORK%\SDL2-%SDL2_VER%.tar.gz"
call :download
if errorlevel 1 exit /b 1
"%TAR%" -xf "%DL_DEST%" -C "%WORK%"
if errorlevel 1 goto fail
if exist "%DL_DEST%" del /q "%DL_DEST%"
:sdl2_source_ready
if not exist "%SDL2_DEPS_DIR%" mkdir "%SDL2_DEPS_DIR%"
echo.
echo ==^> Configuring SDL2
"%CMAKE%" -S "%SDL2_SRC%" -B "%WORK%\build-sdl2" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%TC%" ^
    -DANDROID_ABI="%ANDROID_ABI%" ^
    -DANDROID_PLATFORM="%ANDROID_PLATFORM%" ^
    -DCMAKE_INSTALL_PREFIX="%SDL2_DEPS_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
if errorlevel 1 goto fail
echo.
echo ==^> Building SDL2
"%CMAKE%" --build "%WORK%\build-sdl2" --parallel 8
if errorlevel 1 goto fail
"%CMAKE%" --install "%WORK%\build-sdl2"
if errorlevel 1 goto fail
:sdl2_ready
if not exist "%SDL2_DEPS_DIR%\lib\libSDL2.so" (
    echo error: SDL2 build did not produce %SDL2_DEPS_DIR%\lib\libSDL2.so
    exit /b 1
)
if not exist "%SDL2_DEPS_DIR%\lib\cmake\SDL2\SDL2Config.cmake" (
    echo error: SDL2 CMake package not found in %SDL2_DEPS_DIR%\lib\cmake\SDL2
    echo        this script configures the companions with -DSDL2_DIR; the prefix
    echo        must be an installed SDL2 tree, not just its libSDL2.so
    exit /b 1
)
set "SDL2_FWD=%SDL2_DEPS_DIR:\=/%"

rem =========================================================================
rem  SDL2_ttf (+ the FreeType it vendors)
rem =========================================================================
echo.
echo ==^> SDL2_ttf %SDL2TTF_VER%
set "TTF_SRC=%WORK%\SDL_ttf-%SDL2TTF_VER%"
if exist "%TTF_SRC%" goto ttf_source_ready
set "DL_URL=https://github.com/libsdl-org/SDL_ttf/archive/refs/tags/%SDL2TTF_VER%.zip"
set "DL_DEST=%WORK%\ttf.zip"
call :download
if errorlevel 1 goto fail
"%TAR%" -xf "%WORK%\ttf.zip" -C "%WORK%"
if errorlevel 1 goto fail
if exist "%WORK%\ttf.zip" del /q "%WORK%\ttf.zip"
:ttf_source_ready

rem SDL2_ttf vendors FreeType through a download script that is not present in
rem the release tarball, so it is fetched directly and unpacked into
rem external\freetype. The .sh copied the contents of a staged directory; bsdtar
rem can strip the archive's top-level directory instead.
if exist "%TTF_SRC%\external\freetype\CMakeLists.txt" goto freetype_ready
echo     FreeType %FREETYPE_VER%
set "DL_URL=https://download.savannah.gnu.org/releases/freetype/freetype-%FREETYPE_VER%.tar.gz"
set "DL_DEST=%WORK%\ft.tar.gz"
call :download
if errorlevel 1 goto fail
if exist "%TTF_SRC%\external\freetype" rmdir /s /q "%TTF_SRC%\external\freetype"
mkdir "%TTF_SRC%\external\freetype"
"%TAR%" -xf "%WORK%\ft.tar.gz" -C "%TTF_SRC%\external\freetype" --strip-components 1
if errorlevel 1 goto fail
if exist "%WORK%\ft.tar.gz" del /q "%WORK%\ft.tar.gz"
:freetype_ready

rem FreeType 2.12 still declares cmake_minimum_required(VERSION 3.0), which
rem CMake 3.27+ rejects outright; the .sh widened the minimum with sed. cmd.exe
rem has no sed, so PowerShell does the single substitution. Best-effort, like
rem the .sh's `|| true`: with the SDK's pinned CMake 3.22.1 it changes nothing.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p='%TTF_SRC%\external\freetype\CMakeLists.txt'; if (Test-Path -LiteralPath $p) { $t=Get-Content -LiteralPath $p -Raw; $t=$t -replace 'cmake_minimum_required\(VERSION 3\.[0-4]\)','cmake_minimum_required(VERSION 3.5...3.27)'; [IO.File]::WriteAllText($p,$t) }" >nul 2>&1

echo.
echo ==^> Configuring SDL2_ttf
"%CMAKE%" -S "%TTF_SRC%" -B "%WORK%\build-ttf" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%TC%" ^
    -DANDROID_ABI="%ANDROID_ABI%" ^
    -DANDROID_PLATFORM="%ANDROID_PLATFORM%" ^
    -DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DSDL2_DIR="%SDL2_FWD%/lib/cmake/SDL2" ^
    -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF
if errorlevel 1 goto fail
echo.
echo ==^> Building SDL2_ttf
"%CMAKE%" --build "%WORK%\build-ttf" --parallel 8
if errorlevel 1 goto fail
"%CMAKE%" --install "%WORK%\build-ttf"
if errorlevel 1 goto fail

rem =========================================================================
rem  SDL2_mixer
rem
rem  OGG is decoded by stb_vorbis and MP3 by dr_mp3, both of which SDL_mixer
rem  vendors, so no libogg/libvorbis/libmpg123 cross-build is needed.
rem =========================================================================
echo.
echo ==^> SDL2_mixer %SDL2MIXER_VER%
set "MIX_SRC=%WORK%\SDL_mixer-%SDL2MIXER_VER%"
if exist "%MIX_SRC%" goto mixer_source_ready
set "DL_URL=https://github.com/libsdl-org/SDL_mixer/archive/refs/tags/%SDL2MIXER_VER%.zip"
set "DL_DEST=%WORK%\mixer.zip"
call :download
if errorlevel 1 goto fail
"%TAR%" -xf "%WORK%\mixer.zip" -C "%WORK%"
if errorlevel 1 goto fail
if exist "%WORK%\mixer.zip" del /q "%WORK%\mixer.zip"
:mixer_source_ready

echo.
echo ==^> Configuring SDL2_mixer
"%CMAKE%" -S "%MIX_SRC%" -B "%WORK%\build-mixer" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%TC%" ^
    -DANDROID_ABI="%ANDROID_ABI%" ^
    -DANDROID_PLATFORM="%ANDROID_PLATFORM%" ^
    -DCMAKE_INSTALL_PREFIX="%PREFIX%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DSDL2_DIR="%SDL2_FWD%/lib/cmake/SDL2" ^
    -DSDL2MIXER_VENDORED=ON -DSDL2MIXER_SAMPLES=OFF ^
    -DSDL2MIXER_VORBIS=STB -DSDL2MIXER_MP3=DRMP3 ^
    -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_FLAC=OFF -DSDL2MIXER_MIDI=OFF -DSDL2MIXER_MOD=OFF
if errorlevel 1 goto fail
echo.
echo ==^> Building SDL2_mixer
"%CMAKE%" --build "%WORK%\build-mixer" --parallel 8
if errorlevel 1 goto fail
"%CMAKE%" --install "%WORK%\build-mixer"
if errorlevel 1 goto fail

rem --------------------------------------------------------------- verdict
for %%L in (libSDL2_ttf.so libSDL2_mixer.so) do call :require_so "%%L"
if errorlevel 1 goto fail

echo.
echo ==^> Done
dir /b "%PREFIX%\lib\*.so"
echo.
echo Companion prefix: %PREFIX%
echo port\android\build.bat uses it when SDL2_COMPANION_DIR is unset.
exit /b 0

rem ------------------------------------------------------------- download
rem :download -- skip when DL_DEST already exists, otherwise fetch DL_URL to a
rem .part file so an interrupted download is never mistaken for a complete one.
rem The URL is read from a variable rather than a call argument: `call` performs
rem a second expansion, and a percent-escaped URL would be mangled by it.
:download
if exist "%DL_DEST%" exit /b 0
for %%F in ("%DL_DEST%") do echo     downloading %%~nxF
"%CURL%" --fail --location --silent --show-error --proto "=https" ^
    --retry 2 --connect-timeout 20 --max-time 1800 ^
    --output "%DL_DEST%.part" "%DL_URL%"
if errorlevel 1 (
    if exist "%DL_DEST%.part" del /q "%DL_DEST%.part"
    echo error: download failed
    exit /b 1
)
move /y "%DL_DEST%.part" "%DL_DEST%" >nul
exit /b 0

rem :require_so NAME -- fail when the install step did not produce PREFIX\lib\NAME.
:require_so
if exist "%PREFIX%\lib\%~1" exit /b 0
echo error: %~1 was not installed into %PREFIX%\lib
exit /b 1

:fail
exit /b 1

rem ===================================================================== usage
:usage
rem The header block at the top of this file, minus its "rem" prefixes, the same
rem way port\android\build.bat prints its own.
set /a BANNERS=0
for /f "usebackq delims=" %%L in (`findstr /n /r "^rem" "%~f0"`) do (
    set "L=%%L"
    set "L=!L:*:=!"
    if !BANNERS! LSS 2 (
        echo(!L:~3!
        echo !L! | findstr /b /c:"rem ===" >nul && set /a BANNERS+=1
    )
)
exit /b 0
