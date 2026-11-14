@echo off
rem ===========================================================================
rem  build.bat -- build the Android arm64-v8a APK from Windows cmd.exe
rem
rem  No MSYS2, Git Bash or WSL -- the MSYS2 script this replaced is gone.
rem  Every step is either a program Windows ships (cmd.exe, where.exe, findstr,
rem  System32 sort.exe) or one the pinned toolchain already carries (the JDK,
rem  the SDK's own CMake and sdkmanager.bat, Gradle's gradlew.bat). No Python
rem  is used or needed: the launcher icon is committed artwork.
rem
rem    port\android\build.bat                   release APK; installs what is missing
rem    port\android\build.bat --debug           debug APK
rem    port\android\build.bat --setup           install tools + native libs, then stop
rem    port\android\build.bat --download-tools  install missing tools, then build
rem    port\android\build.bat --check-tools     validate only; downloads nothing
rem    port\android\build.bat --offline         no downloads; cached Gradle/deps only
rem    port\android\build.bat --clean           clean generated app build/staging
rem    port\android\build.bat --no-assets       package no game data
rem    port\android\build.bat --gles            build the GLES 3.0 renderer, M8
rem    port\android\build.bat --model3d         opt-in glTF renderer, implies --gles
rem
rem  Missing tools are installed automatically. The JDK comes from Adoptium's
rem  official metadata, the Android command-line tools from Google's repository
rem  XML, and the SDK platform/build-tools/platform-tools/CMake/NDK packages
rem  through the SDK's own sdkmanager.bat. Missing SDL2, SDL2_mixer and
rem  SDL2_ttf are cross-compiled by tools\build_sdl2_companions.bat, which
rem  downloads SDL2's release tarball with curl. Nothing is fetched with git.
rem  --check-tools and --offline never download; --setup stops after the tools
rem  and libraries are in place, and --download-tools continues to the APK.
rem
rem  The packaged data/mugen.cfg is port/android/mugen.cfg, the Android defaults,
rem  not the game tree copy. GAME_CFG=path stages a different one.
rem  Output: build\android\mugen-android-arm64-v8a.apk
rem
rem  Environment:
rem    ANDROID_ABI              must stay arm64-v8a; app/build.gradle packages
rem                             that one ABI
rem    JAVA_HOME                a JDK 17; MUGEN_JAVA_HOME wins over it
rem    ANDROID_SDK_ROOT         first choice for the SDK, then ANDROID_HOME,
rem                             then ANDROID_HOME_DIR, then the local caches
rem    ANDROID_NDK_HOME         optional external NDK; must be the pinned one
rem    SDL2_DEPS_DIR            SDL2 prefix, default build\android\sdl2\abi
rem    SDL2_COMPANION_DIR       mixer/ttf prefix, default android-deps\abi
rem    GAME_DATA_DIR            game tree to copy, default install\
rem    GAME_CFG                 platform mugen.cfg, default port\android\mugen.cfg
rem    GRADLE_USER_HOME         default build\android\gradle-user-home
rem    ANDROID_USER_HOME        default build\android\toolchains\android-user-home
rem    MUGEN_ANDROID_TOOLS_DIR  default build\android\toolchains
rem    MUGEN_ACCEPT_LICENSES    default 1: answer sdkmanager's licence prompts
rem                             with y. Set 0 to answer them yourself.
rem
rem  No Python is needed. The launcher icon is the committed artwork in
rem  app\src\main\res\mipmap-* (see "Launcher icon" below).
rem ===========================================================================

setlocal EnableExtensions EnableDelayedExpansion

rem The script's own directory and the repository root, without a trailing
rem backslash: %~dp0 already means "port\android\".
for %%I in ("%~dp0.") do set "PORTDIR=%%~fI"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "APP=%PORTDIR%\app"
set "OUT=%ROOT%\build\android"
rem bootstrap_toolchains.sh's $EXE: the suffix the SDK's tools carry here.
set "EXE=.exe"

rem --------------------------------------------------------------- defaults
set "BUILD_TYPE=release"
set "CLEAN=0"
set "PACK_ASSETS=1"
set "CHECK_TOOLS=0"
set "SETUP_ONLY=0"
set "EXPLICIT_DOWNLOAD=0"
set "OFFLINE=0"
set "GLES=0"
set "MODEL3D=0"
if not defined MUGEN_ACCEPT_LICENSES set "MUGEN_ACCEPT_LICENSES=1"
set "TOOLSDIR=%PORTDIR%\tools"

rem ------------------------------------------------------------------- args
:parse
if "%~1"=="" goto parsed
set "ARG=%~1"
if /I "!ARG!"=="--help" goto usage
if /I "!ARG!"=="-h" goto usage
if /I "!ARG!"=="--debug" ( set "BUILD_TYPE=debug" & shift & goto parse )
if /I "!ARG!"=="--clean" ( set "CLEAN=1" & shift & goto parse )
if /I "!ARG!"=="--no-assets" ( set "PACK_ASSETS=0" & shift & goto parse )
rem --model3d implies --gles, exactly as the MSYS2 build.sh that was removed did: the glTF renderer has no
rem desktop-GL path on Android.
if /I "!ARG!"=="--gles" ( set "GLES=1" & shift & goto parse )
if /I "!ARG!"=="--model3d" ( set "MODEL3D=1" & set "GLES=1" & shift & goto parse )
rem --setup installs the toolchain and the native libraries, then stops before
rem Gradle. --download-tools does the same installation but continues to build.
rem --check-tools validates what is present and never downloads. Nothing here
rem reports a missing tool and exits: the missing pieces are fetched.
if /I "!ARG!"=="--setup" ( set "SETUP_ONLY=1" & set "EXPLICIT_DOWNLOAD=1" & shift & goto parse )
if /I "!ARG!"=="--download-tools" ( set "EXPLICIT_DOWNLOAD=1" & shift & goto parse )
if /I "!ARG!"=="--check-tools" ( set "CHECK_TOOLS=1" & shift & goto parse )
if /I "!ARG!"=="--offline" ( set "OFFLINE=1" & shift & goto parse )
echo error: unknown argument: %~1  ^(--help^)
exit /b 2
:parsed

if "%OFFLINE%"=="1" if "%EXPLICIT_DOWNLOAD%"=="1" (
    echo error: --offline cannot be combined with --setup or --download-tools
    exit /b 2
)
rem Provisioning is the default: a clean checkout should build after one run.
rem --check-tools (validate only) and --offline (never download) turn it off.
set "DOWNLOAD_TOOLS=1"
if "%CHECK_TOOLS%"=="1" set "DOWNLOAD_TOOLS=0"
if "%OFFLINE%"=="1" set "DOWNLOAD_TOOLS=0"
if not defined ANDROID_ABI set "ANDROID_ABI=arm64-v8a"
if /I not "%ANDROID_ABI%"=="arm64-v8a" (
    echo error: app/build.gradle currently packages arm64-v8a only; requested ABI %ANDROID_ABI% is unsupported
    exit /b 2
)

rem ------------------------------------------------------------- toolchains
rem Everything the removed .sh's `source tools/bootstrap_toolchains.sh` +
rem `android_prepare_toolchains` would export.
call :prepare_toolchains
if errorlevel 1 goto fail
if "%CHECK_TOOLS%"=="1" exit /b 0
if "%DOWNLOAD_TOOLS%"=="1" (
    call :provision_native_libs
    if errorlevel 1 goto fail
)
if "%SETUP_ONLY%"=="1" exit /b 0

rem ------------------------------------------- native SDL2 / mixer / ttf libs
rem Port fix: the removed .sh's stage of the same checks runs the same way, but the two
rem prefixes are NOT produced by anything in this script or by --setup. They are
rem a separate cross-compilation of SDL 2.32.10 for arm64-v8a/API 21 plus
rem tools\build_sdl2_companions.bat, so a clone without them has to be told
rem clearly which file is missing instead of failing later inside CMake.
if not defined SDL2_DEPS_DIR set "SDL2_DEPS_DIR=%OUT%\sdl2\%ANDROID_ABI%"
if not defined SDL2_COMPANION_DIR set "SDL2_COMPANION_DIR=%OUT%\android-deps\%ANDROID_ABI%"
for %%I in ("%SDL2_DEPS_DIR%") do set "SDL2_DEPS_DIR=%%~fI"
for %%I in ("%SDL2_COMPANION_DIR%") do set "SDL2_COMPANION_DIR=%%~fI"
set "SDL2_INCLUDE=%SDL2_DEPS_DIR%\include"

if not exist "%SDL2_INCLUDE%\SDL2\SDL.h" (
    echo error: SDL2 headers missing in %SDL2_INCLUDE%\SDL2; build SDL2 or set SDL2_DEPS_DIR
    echo        see port\android\README.md section 2 ^(--setup does not fetch or build SDL2^)
    goto fail
)
if not exist "%SDL2_DEPS_DIR%\lib\libSDL2.so" (
    echo error: libSDL2.so missing in %SDL2_DEPS_DIR%; follow README.md or set SDL2_DEPS_DIR
    echo        the expected file is %SDL2_DEPS_DIR%\lib\libSDL2.so
    goto fail
)
for %%N in (mixer ttf) do if not exist "%SDL2_COMPANION_DIR%\lib\libSDL2_%%N.so" (
    echo error: libSDL2_%%N.so missing in %SDL2_COMPANION_DIR%\lib
    echo        run port\android\tools\build_sdl2_companions.bat with SDL2_DEPS_DIR set,
    echo        or point SDL2_COMPANION_DIR at a prefix that has it
    goto fail
)

if not exist "%PORTDIR%\gradle\wrapper\gradle-wrapper.jar" (
    echo error: Tracked Gradle wrapper JAR missing; restore port\android\gradle\wrapper\gradle-wrapper.jar from git
    goto fail
)
set "GRADLE_LAUNCHER=%PORTDIR%\gradlew.bat"
if not exist "%GRADLE_LAUNCHER%" (
    echo error: port\android\gradlew.bat is missing; restore it from git
    goto fail
)
if "%OFFLINE%"=="1" (
    call :find_gradle_offline
    if errorlevel 1 goto fail
)

if "%CLEAN%"=="1" (
    echo.
    echo ==^> Cleaning generated Android build output
    if exist "%OUT%\stage" rmdir /s /q "%OUT%\stage"
    if exist "%APP%\build" rmdir /s /q "%APP%\build"
    if exist "%APP%\.cxx" rmdir /s /q "%APP%\.cxx"
)

rem --------------------------------------------------- native library staging
echo.
echo ==^> Staging native libraries for %ANDROID_ABI%
set "JNILIBS=%APP%\src\main\jniLibs\%ANDROID_ABI%"
if not exist "%JNILIBS%" mkdir "%JNILIBS%"
copy /y "%SDL2_DEPS_DIR%\lib\libSDL2.so" "%JNILIBS%\" >nul
if errorlevel 1 (
    echo error: cannot copy %SDL2_DEPS_DIR%\lib\libSDL2.so into %JNILIBS%
    goto fail
)
copy /y "%SDL2_COMPANION_DIR%\lib\libSDL2_mixer.so" "%JNILIBS%\" >nul
if errorlevel 1 (
    echo error: cannot copy libSDL2_mixer.so into %JNILIBS%
    goto fail
)
copy /y "%SDL2_COMPANION_DIR%\lib\libSDL2_ttf.so" "%JNILIBS%\" >nul
if errorlevel 1 (
    echo error: cannot copy libSDL2_ttf.so into %JNILIBS%
    goto fail
)
for %%L in (libSDL2.so libSDL2_mixer.so libSDL2_ttf.so) do (
    for %%A in ("%JNILIBS%\%%L") do echo     %%L: %%~zA bytes
)

rem ------------------------------------------------------------ asset staging
rem Recreated every run, including --no-assets: an old payload must not leak into
rem an APK that was requested without assets.
set "ASSETS=%APP%\src\main\assets"
if exist "%ASSETS%" rmdir /s /q "%ASSETS%"
mkdir "%ASSETS%"
if "%PACK_ASSETS%"=="1" (
    call :stage_game_data
    if errorlevel 1 goto fail
) else (
    echo.
    echo ==^> Skipping game data ^(--no-assets^)
)

rem ----------------------------------------------------------- launcher icon
rem No icon step, because this script uses no Python. res/icons/*.ico is the
rem single source of the artwork and tools/iconpack.py (standard library only)
rem derives the five density buckets plus the adaptive foreground from it; the
rem committed copies of exactly that output live in app\src\main\res\mipmap-*
rem and are what Gradle packages. Regenerate them with tools\make_android_icons.sh
rem (or the two iconpack.py subcommands) after changing res\icons, then rebuild.
rem Any stale generated set from an earlier build is removed so it cannot win
rem over the committed one: build.gradle gives the generated directory priority.
echo.
echo ==^> Launcher icon
set "ICON_RES=%APP%\build\generated\res\mugen-icons"
if exist "%ICON_RES%" rmdir /s /q "%ICON_RES%"
echo     committed mipmaps in app\src\main\res

rem ------------------------------------------------------- gradle invocation
echo.
echo ==^> Configuring Gradle project
rem local.properties carries sdk.dir with Java-properties escaping, the same two
rem substitutions the removed .sh's props_escape makes. It is generated, never the
rem tracked gradle.properties: that file is project configuration.
call :props_escape "%ANDROID_SDK_ROOT%"
> "%PORTDIR%\local.properties" echo sdk.dir=!PROPS!
rem jni/src/CMakeLists.txt reads this environment variable before it falls back
rem to the -Psdl2IncludeDir property; the MSYS2 build.sh that was removed exported both.
set "MUGEN_ANDROID_SDL2_INCLUDE=%SDL2_INCLUDE%"

set "GLES_PROP="
if "%GLES%"=="1" set "GLES_PROP=-PmugenGles=ON"
set "OFFLINE_PROP="
if "%OFFLINE%"=="1" set "OFFLINE_PROP=--offline"
set "TASK=assembleRelease"
if /I "%BUILD_TYPE%"=="debug" set "TASK=assembleDebug"

echo.
echo ==^> Building APK ^(%BUILD_TYPE%, %ANDROID_ABI%^)
if "%GLES%"=="1" echo     GLES 3.0 renderer enabled ^(M8, experimental^)
if "%MODEL3D%"=="1" echo     glTF stage models enabled
echo     %GRADLE_LAUNCHER%
echo.

set "GRADLE_RC=0"
pushd "%PORTDIR%"
rem Port fix: the MSYS2 build.sh that was removed ran `bash $GRADLE_LAUNCHER`. On Windows the wrapper is
rem gradlew.bat (or the cached distribution's bin\gradle.bat), which is already
rem a native launcher, so no shell is involved at all.
call "%GRADLE_LAUNCHER%" --no-daemon ^
    "-Dorg.gradle.java.home=%JAVA_HOME%" ^
    "-Djava.io.tmpdir=%TMPDIR%" ^
    "-Duser.home=%ANDROID_USER_HOME%" ^
    "-PmugenNdkPath=%ANDROID_NDK_HOME%" ^
    "-Psdl2IncludeDir=%SDL2_INCLUDE%" ^
    "-Psdl2MixerTtfDir=%SDL2_COMPANION_DIR%" ^
    "-PmugenModel3d=%MODEL3D%" ^
    !GLES_PROP! !OFFLINE_PROP! %TASK%
if errorlevel 1 set "GRADLE_RC=1"
popd
if not "%GRADLE_RC%"=="0" (
    echo.
    echo error: Gradle failed; see its output above
    goto fail
)

rem ------------------------------------------------------------- collect APK
rem Only the requested variant, never a stale debug/release APK found first.
set "APK_DIR=%APP%\build\outputs\apk\%BUILD_TYPE%"
if not exist "%APK_DIR%\" (
    echo error: No %BUILD_TYPE% APK output directory; see Gradle output
    goto fail
)
set "APK="
set "NAPK=0"
for %%A in ("%APK_DIR%\*.apk") do if exist "%%~fA" (
    set /a NAPK+=1
    if not defined APK set "APK=%%~fA"
)
if not defined APK (
    echo error: No %BUILD_TYPE% APK produced; see Gradle output
    goto fail
)
if not "%NAPK%"=="1" (
    echo error: Multiple APKs in %APK_DIR%; select an output explicitly
    goto fail
)
set "DEST=%OUT%\mugen-android-%ANDROID_ABI%.apk"
copy /y "%APK%" "%DEST%" >nul
if errorlevel 1 (
    echo error: cannot copy %APK% to %DEST%
    goto fail
)
for %%D in ("%DEST%") do set "DSIZE=%%~zD"
echo.
echo APK: %DEST% ^(%DSIZE% bytes^)
echo Install: "%ANDROID_SDK_ROOT%\platform-tools\adb%EXE%" install -r "%DEST%"
echo Runtime data: app-specific external files/mugen ^(internal fallback^); see logcat -s mugen
goto :eof

rem ============================================================== toolchains
rem The bootstrap script is a separate file in the MSYS2 build.sh that was removed because it is sourced
rem there; here it is one label, so nothing has to be re-parsed.
:prepare_toolchains
rem ---------------------------------------------------------------- host
rem bootstrap_toolchains.sh picks a host triplet from `uname -s`/`uname -m`.
rem cmd.exe has no uname, and the only supported Windows host is x64 (the NDK
rem ships a windows-x86_64 prebuilt), so the choice is made from the Windows
rem environment. PROCESSOR_ARCHITEW6432 covers a 32-bit cmd on 64-bit Windows.
if /I not "%PROCESSOR_ARCHITECTURE%"=="AMD64" if /I not "%PROCESSOR_ARCHITEW6432%"=="AMD64" (
    echo error: Unsupported Android build host: %PROCESSOR_ARCHITECTURE%/%PROCESSOR_ARCHITEW6432%
    echo        this script builds on Windows x64 only
    exit /b 1
)
set "NDK_HOST=windows-x86_64"

rem -------------------------------------------------------- local caches
if not defined MUGEN_ANDROID_TOOLS_DIR set "MUGEN_ANDROID_TOOLS_DIR=%OUT%\toolchains"
for %%I in ("%MUGEN_ANDROID_TOOLS_DIR%") do set "TOOLS=%%~fI"
if not exist "%TOOLS%\tmp" mkdir "%TOOLS%\tmp"
rem Vendor metadata and verified archives are cached here, as the removed
rem bootstrap_toolchains.sh did, so a retry does not re-download a 200 MB JDK.
if not exist "%TOOLS%\downloads" mkdir "%TOOLS%\downloads"
set "DOWNLOADS=%TOOLS%\downloads"
rem The temp directory is pointed inside the repository on purpose: that is
rem AGENTS.md's first rule, and bootstrap_toolchains.sh exports the same three
rem variables so Gradle, CMake and the NDK keep their scratch files here.
set "TMPDIR=%TOOLS%\tmp"
set "TMP=%TOOLS%\tmp"
set "TEMP=%TOOLS%\tmp"

rem ---------------------------------------------- downloader / unpacker
rem Native Windows tools only: curl.exe and System32's bsdtar ship with
rem Windows 10 1803 and later, so no MSYS2, Python or 7-Zip is involved.
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
if not defined GRADLE_USER_HOME set "GRADLE_USER_HOME=%OUT%\gradle-user-home"
if not defined ANDROID_USER_HOME set "ANDROID_USER_HOME=%TOOLS%\android-user-home"
if not exist "%GRADLE_USER_HOME%" mkdir "%GRADLE_USER_HOME%"
if not exist "%ANDROID_USER_HOME%" mkdir "%ANDROID_USER_HOME%"

rem -------------------------------------------------- Gradle's own pins
rem bootstrap_toolchains.sh reads these four numbers out of app/build.gradle with
rem sed, so what it validates is literally what Gradle asks for. The same four
rem lines are read here, which keeps the two scripts from drifting apart.
set "GRADLE_FILE=%APP%\build.gradle"
if not exist "%GRADLE_FILE%" (
    echo error: %GRADLE_FILE% is missing; restore port\android\app\build.gradle from git
    exit /b 1
)
set "NDK_VERSION="
for /f tokens^=2^ delims^=^" %%V in ('findstr /r /c:"^[ ]*ndkVersion[ ]" "%GRADLE_FILE%"') do if not defined NDK_VERSION set "NDK_VERSION=%%V"
set "BUILD_TOOLS_VERSION="
for /f tokens^=2^ delims^=^" %%V in ('findstr /r /c:"^[ ]*buildToolsVersion[ ]" "%GRADLE_FILE%"') do if not defined BUILD_TOOLS_VERSION set "BUILD_TOOLS_VERSION=%%V"
set "SDK_PLATFORM="
for /f "tokens=2" %%V in ('findstr /r /c:"^[ ]*compileSdkVersion[ ]" "%GRADLE_FILE%"') do if not defined SDK_PLATFORM set "SDK_PLATFORM=%%V"
set "CMAKE_VERSION="
for /f tokens^=2^ delims^=^" %%V in ('findstr /r /c:"^[ ]*version[ ]" "%GRADLE_FILE%"') do if not defined CMAKE_VERSION set "CMAKE_VERSION=%%V"
if not defined NDK_VERSION set "PINS_FAILED=1"
if not defined BUILD_TOOLS_VERSION set "PINS_FAILED=1"
if not defined SDK_PLATFORM set "PINS_FAILED=1"
if not defined CMAKE_VERSION set "PINS_FAILED=1"
if defined PINS_FAILED (
    echo error: Could not read SDK/NDK/CMake/build-tools pins from app\build.gradle
    exit /b 1
)

rem ------------------------------------------------------------- JDK 17
set "JDK="
if defined MUGEN_JAVA_HOME (
    call :jdk_ok "%MUGEN_JAVA_HOME%"
    if "!JDK_OK!"=="1" ( set "JDK=%MUGEN_JAVA_HOME%" ) else (
        echo error: MUGEN_JAVA_HOME must identify a runnable JDK 17: %MUGEN_JAVA_HOME%
        exit /b 1
    )
)
if not defined JDK if defined JAVA_HOME (
    call :jdk_ok "%JAVA_HOME%"
    if "!JDK_OK!"=="1" set "JDK=%JAVA_HOME%"
)
if not defined JDK (
    call :jdk_ok "%TOOLS%\jdk-17"
    if "!JDK_OK!"=="1" set "JDK=%TOOLS%\jdk-17"
)
if not defined JDK (
    call :jdk_ok "C:\Android\jdk-17"
    if "!JDK_OK!"=="1" set "JDK=C:\Android\jdk-17"
)
if not defined JDK (
    rem Port fix: the removed bootstrap_toolchains.sh fetched Temurin from
    rem Adoptium's JSON metadata here. cmd.exe cannot parse that itself, so
    rem tools\toolchain_metadata.ps1 reads it and this label does the download,
    rem SHA-256 check, extraction and verification with curl/Get-FileHash/bsdtar.
    if "%DOWNLOAD_TOOLS%"=="1" (
        call :install_jdk
        if errorlevel 1 exit /b 1
    ) else (
        echo error: JDK 17 missing. Set JAVA_HOME or MUGEN_JAVA_HOME to a JDK 17,
        echo        or install Temurin 17 from
        echo        https://adoptium.net/temurin/releases/?version=17
        echo        ^(--check-tools and --offline never download^)
        exit /b 1
    )
)
for %%I in ("%JDK%") do set "JAVA_HOME=%%~fI"

rem ------------------------------------------------------------- SDK
set "SDKC="
if defined ANDROID_SDK_ROOT set "SDKC=%ANDROID_SDK_ROOT%"
if not defined SDKC if defined ANDROID_HOME set "SDKC=%ANDROID_HOME%"
if not defined SDKC if defined ANDROID_HOME_DIR set "SDKC=%ANDROID_HOME_DIR%"
rem Port fix: the bootstrap's own default is $TOOLS/sdk. The two locations a
rem Windows user has normally already installed to are tried first, so an
rem existing SDK is used rather than provoking a second download.
if not defined SDKC if exist "%TOOLS%\sdk\platforms" set "SDKC=%TOOLS%\sdk"
if not defined SDKC if exist "C:\Android\SDK\platforms" set "SDKC=C:\Android\SDK"
if not defined SDKC (
    if "%DOWNLOAD_TOOLS%"=="1" (
        rem A clean Windows install has no SDK; bootstrap into the local cache.
        set "SDKC=%TOOLS%\sdk"
    ) else (
        echo error: Android SDK not found. Set ANDROID_SDK_ROOT to an installed SDK
        echo        ^(see https://developer.android.com/studio^), for example
        echo        set ANDROID_SDK_ROOT=C:\Android\SDK
        echo        ^(--check-tools and --offline never download^)
        exit /b 1
    )
)
for %%I in ("%SDKC%") do set "ANDROID_SDK_ROOT=%%~fI"
if not exist "%ANDROID_SDK_ROOT%" mkdir "%ANDROID_SDK_ROOT%"
set "ANDROID_HOME=%ANDROID_SDK_ROOT%"

rem ------------------------------------------------------------- NDK
set "NDKC=%ANDROID_SDK_ROOT%\ndk\%NDK_VERSION%"
if defined ANDROID_NDK_HOME set "NDKC=%ANDROID_NDK_HOME%"
for %%I in ("%NDKC%") do set "ANDROID_NDK_HOME=%%~fI"

rem ------------------------------------------------- what the SDK is missing
call :find_missing_packages
if errorlevel 1 exit /b 1
if defined MISSING (
    if "%DOWNLOAD_TOOLS%"=="1" (
        call :install_sdk_packages
        if errorlevel 1 exit /b 1
        call :find_missing_packages
        if errorlevel 1 exit /b 1
    )
)
if defined MISSING (
    echo error: Missing SDK packages:!MISSING!
    echo        run port\android\build.bat --setup to install them
    echo        ^(--check-tools and --offline never download^)
    exit /b 1
)

if not exist "%ANDROID_NDK_HOME%\source.properties" (
    echo error: NDK source.properties missing: %ANDROID_NDK_HOME%
    exit /b 1
)
set "NDK_REV="
for /f "usebackq delims=" %%L in (`findstr /b /c:"Pkg.Revision" "%ANDROID_NDK_HOME%\source.properties"`) do if not defined NDK_REV (
    set "L=%%L"
    set "L=!L: =!"
    set "NDK_REV=!L:~13!"
)
if not "%NDK_REV%"=="%NDK_VERSION%" (
    echo error: NDK revision %NDK_REV% does not match Gradle's %NDK_VERSION% at %ANDROID_NDK_HOME%
    exit /b 1
)

rem the MSYS2 build.sh that was removed exported exactly this PATH addition, so Gradle and the NDK find the
rem pinned CMake/Ninja and adb rather than whatever else is installed.
set "PATH=%JAVA_HOME%\bin;%ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin;%ANDROID_SDK_ROOT%\platform-tools;%PATH%"

rem ------------------------------------------------------- record the environment
rem Port fix: the MSYS2 build.sh that was removed writes build/android/toolchains/env.sh so a later shell
rem can `source` it. MSYS-style `export` lines are useless from cmd.exe, so the
rem same values are written as a callable env.bat instead. env.sh is left alone:
rem it is the removed .sh's file and rewriting it here would only mislead.
call :write_env_bat

if "%CHECK_TOOLS%"=="1" call :report_tools
echo.
echo ==^> Toolchain ready: JDK 17, SDK %SDK_PLATFORM%, build-tools %BUILD_TOOLS_VERSION%, NDK %NDK_VERSION%, CMake %CMAKE_VERSION%
echo     SDK: %ANDROID_SDK_ROOT%
echo     NDK: %ANDROID_NDK_HOME%
echo     JDK: %JAVA_HOME%
echo     Environment: %TOOLS%\env.bat
exit /b 0

:write_env_bat
> "%TOOLS%\env.bat" echo @echo off
>> "%TOOLS%\env.bat" echo rem Generated by port\android\build.bat -- the cmd.exe counterpart
>> "%TOOLS%\env.bat" echo rem of build\android\toolchains\env.sh, which the removed android the MSYS2 build.sh that was removed writes.
>> "%TOOLS%\env.bat" echo set "JAVA_HOME=%JAVA_HOME%"
>> "%TOOLS%\env.bat" echo set "ANDROID_SDK_ROOT=%ANDROID_SDK_ROOT%"
>> "%TOOLS%\env.bat" echo set "ANDROID_HOME=%ANDROID_HOME%"
>> "%TOOLS%\env.bat" echo set "ANDROID_NDK_HOME=%ANDROID_NDK_HOME%"
>> "%TOOLS%\env.bat" echo set "GRADLE_USER_HOME=%GRADLE_USER_HOME%"
>> "%TOOLS%\env.bat" echo set "ANDROID_USER_HOME=%ANDROID_USER_HOME%"
>> "%TOOLS%\env.bat" echo set "TMPDIR=%TMPDIR%"
>> "%TOOLS%\env.bat" echo set "TMP=%TMP%"
>> "%TOOLS%\env.bat" echo set "TEMP=%TEMP%"
>> "%TOOLS%\env.bat" echo set "PATH=%%JAVA_HOME%%\bin;%%ANDROID_SDK_ROOT%%\cmake\%CMAKE_VERSION%\bin;%%ANDROID_SDK_ROOT%%\platform-tools;%%PATH%%"
exit /b 0

rem ------------------------------------------------------------ report
:report_tools
echo.
echo ==^> Tools
echo   java        ok       %JAVA_HOME% ^(%JDK_VERSION%^)
echo   sdk         ok       %ANDROID_SDK_ROOT% ^(platform %SDK_PLATFORM%^)
echo   build-tools ok       %BUILD_TOOLS_VERSION%
echo   ndk         ok       %NDK_REV%
echo   cmake       ok       %CMAKE_VERSION%
if exist "%PORTDIR%\gradlew.bat" (
    echo   gradle      ok       port\android\gradlew.bat
) else (
    echo   gradle      MISSING  port\android\gradlew.bat
)
rem Addition, not a divergence: the MSYS2 build.sh that was removed exits before its library checks, so it
rem never mentions them under --check-tools. They are reported here anyway,
rem because on Windows the two prefixes are the usual reason a build stops, and
rem the exit code is deliberately left alone.
set "SDL_OK=1"
if not exist "%OUT%\sdl2\%ANDROID_ABI%\lib\libSDL2.so" set "SDL_OK=0"
if not exist "%OUT%\android-deps\%ANDROID_ABI%\lib\libSDL2_mixer.so" set "SDL_OK=0"
if not exist "%OUT%\android-deps\%ANDROID_ABI%\lib\libSDL2_ttf.so" set "SDL_OK=0"
if "%SDL_OK%"=="1" (
    echo   sdl2        ok       build\android\sdl2\%ANDROID_ABI%
) else (
    echo   sdl2        MISSING  build\android\sdl2\%ANDROID_ABI% ^(informational, not counted^)
    echo               the native libraries need cross-compiling: README.md section 2
)
exit /b 0

rem ========================================================= provisioning
rem Port fix: the removed bootstrap_toolchains.sh + toolchain_metadata.py did
rem this with Python. Here curl.exe fetches the vendor documents and archives,
rem tools\toolchain_metadata.ps1 reads the metadata, Get-FileHash verifies,
rem System32's bsdtar unpacks, and the SDK's own sdkmanager.bat installs.

rem :fetch -- download DL_URL to DL_DEST through a .part file. The URL is read
rem from a variable, never passed as a call argument: the Adoptium link contains
rem %2B, and `call` performs a second expansion that would eat %2.
:fetch
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

rem :check_hash FILE ALGORITHM EXPECTED -> 0 when they match.
:check_hash
set "GOT_HASH="
for /f "usebackq delims=" %%H in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-FileHash -LiteralPath '%~1' -Algorithm %~2).Hash.ToLower()"`) do set "GOT_HASH=%%H"
if not defined GOT_HASH exit /b 1
if /I not "%GOT_HASH%"=="%~3" exit /b 1
exit /b 0

rem :install_jdk -- Temurin 17 from Adoptium's official metadata, into TOOLS\jdk-17.
:install_jdk
echo.
echo ==^> Downloading Temurin JDK 17 ^(Adoptium^)
set "DL_URL=https://api.adoptium.net/v3/assets/latest/17/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse"
set "DL_DEST=%DOWNLOADS%\jdk-metadata.json"
call :fetch
if errorlevel 1 exit /b 1

set "DL_URL="
set "DL_HASH="
for /f "usebackq tokens=1-2 delims=|" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLSDIR%\toolchain_metadata.ps1" jdk "%DL_DEST%"`) do (
    set "DL_URL=%%A"
    set "DL_HASH=%%B"
)
if not defined DL_URL (
    echo error: could not read the JDK download metadata
    exit /b 1
)

set "ARCHIVE=%DOWNLOADS%\temurin17.zip"
if exist "%ARCHIVE%" (
    call :check_hash "%ARCHIVE%" sha256 "%DL_HASH%"
    if not errorlevel 1 goto jdk_extract
    echo     cached JDK archive failed its checksum; downloading it again
    del /q "%ARCHIVE%"
)
set "DL_DEST=%ARCHIVE%"
call :fetch
if errorlevel 1 exit /b 1
call :check_hash "%ARCHIVE%" sha256 "%DL_HASH%"
if errorlevel 1 (
    del /q "%ARCHIVE%"
    echo error: JDK archive failed its Adoptium SHA-256; nothing was extracted
    exit /b 1
)

:jdk_extract
set "STAGE=%TOOLS%\tmp\jdk-stage"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
"%TAR%" -xf "%ARCHIVE%" -C "%STAGE%"
if errorlevel 1 (
    rmdir /s /q "%STAGE%"
    echo error: cannot extract the verified JDK archive
    exit /b 1
)
set "JDK_HOME="
for /f "usebackq delims=" %%H in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLSDIR%\toolchain_metadata.ps1" jdk-home "%STAGE%"`) do set "JDK_HOME=%%H"
if not defined JDK_HOME (
    rmdir /s /q "%STAGE%"
    echo error: JDK archive has no unique Java home
    exit /b 1
)
call :jdk_ok "%JDK_HOME%"
if not "!JDK_OK!"=="1" (
    rmdir /s /q "%STAGE%"
    echo error: downloaded JDK 17 cannot execute on this host
    exit /b 1
)
if exist "%TOOLS%\jdk-17" (
    rmdir /s /q "%STAGE%"
    echo error: Incomplete JDK at %TOOLS%\jdk-17; move it aside before retrying
    exit /b 1
)
move "%JDK_HOME%" "%TOOLS%\jdk-17" >nul
if errorlevel 1 (
    rmdir /s /q "%STAGE%"
    echo error: cannot move the downloaded JDK into %TOOLS%\jdk-17
    exit /b 1
)
rmdir /s /q "%STAGE%"
set "JDK=%TOOLS%\jdk-17"
exit /b 0

rem :install_cmdline_tools -- Google's command-line tools, into SDK\cmdline-tools\latest.
rem Needed before sdkmanager.bat exists; the version is the bootstrap's 19.0.
:install_cmdline_tools
echo.
echo ==^> Downloading Android command-line tools 19.0
set "DL_URL=https://dl.google.com/android/repository/repository2-3.xml"
set "DL_DEST=%DOWNLOADS%\android-repository.xml"
call :fetch
if errorlevel 1 exit /b 1

set "DL_URL="
set "DL_ALG="
set "DL_HASH="
for /f "usebackq tokens=1-3 delims=|" %%A in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLSDIR%\toolchain_metadata.ps1" sdk "%DL_DEST%" windows 19.0`) do (
    set "DL_URL=%%A"
    set "DL_ALG=%%B"
    set "DL_HASH=%%C"
)
if not defined DL_URL (
    echo error: could not read Google's SDK metadata
    exit /b 1
)

set "ARCHIVE=%DOWNLOADS%\commandlinetools-win.zip"
if exist "%ARCHIVE%" (
    call :check_hash "%ARCHIVE%" "%DL_ALG%" "%DL_HASH%"
    if not errorlevel 1 goto cli_extract
    echo     cached command-line tools archive failed its checksum; downloading again
    del /q "%ARCHIVE%"
)
set "DL_DEST=%ARCHIVE%"
call :fetch
if errorlevel 1 exit /b 1
call :check_hash "%ARCHIVE%" "%DL_ALG%" "%DL_HASH%"
if errorlevel 1 (
    del /q "%ARCHIVE%"
    echo error: command-line tools archive failed Google's checksum; nothing was extracted
    exit /b 1
)

:cli_extract
set "STAGE=%TOOLS%\tmp\cmdline-stage"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
"%TAR%" -xf "%ARCHIVE%" -C "%STAGE%"
if errorlevel 1 (
    rmdir /s /q "%STAGE%"
    echo error: cannot extract the verified command-line tools archive
    exit /b 1
)
if not exist "%STAGE%\cmdline-tools\bin\sdkmanager.bat" (
    rmdir /s /q "%STAGE%"
    echo error: unexpected Android command-line tools archive layout
    exit /b 1
)
if exist "%ANDROID_SDK_ROOT%\cmdline-tools\latest" (
    rmdir /s /q "%STAGE%"
    echo error: %ANDROID_SDK_ROOT%\cmdline-tools\latest already exists; move it aside
    exit /b 1
)
if not exist "%ANDROID_SDK_ROOT%\cmdline-tools" mkdir "%ANDROID_SDK_ROOT%\cmdline-tools"
move "%STAGE%\cmdline-tools" "%ANDROID_SDK_ROOT%\cmdline-tools\latest" >nul
if errorlevel 1 (
    rmdir /s /q "%STAGE%"
    echo error: cannot move the command-line tools into place
    exit /b 1
)
rmdir /s /q "%STAGE%"
exit /b 0

rem :find_missing_packages -- sets MISSING to the sdkmanager package names that
rem are absent. A custom incomplete ANDROID_NDK_HOME is refused, not overwritten.
:find_missing_packages
set "MISSING="
if not exist "%ANDROID_SDK_ROOT%\platforms\android-%SDK_PLATFORM%\android.jar" set "MISSING=!MISSING! platforms;android-%SDK_PLATFORM%"
if not exist "%ANDROID_SDK_ROOT%\build-tools\%BUILD_TOOLS_VERSION%\aapt%EXE%" set "MISSING=!MISSING! build-tools;%BUILD_TOOLS_VERSION%"
if not exist "%ANDROID_SDK_ROOT%\platform-tools\adb%EXE%" set "MISSING=!MISSING! platform-tools"
set "CMAKE_OK=1"
if not exist "%ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin\cmake%EXE%" set "CMAKE_OK=0"
if not exist "%ANDROID_SDK_ROOT%\cmake\%CMAKE_VERSION%\bin\ninja%EXE%" set "CMAKE_OK=0"
if "%CMAKE_OK%"=="0" set "MISSING=!MISSING! cmake;%CMAKE_VERSION%"
set "NDK_BROKEN=0"
if not exist "%ANDROID_NDK_HOME%\build\cmake\android.toolchain.cmake" set "NDK_BROKEN=1"
if not exist "%ANDROID_NDK_HOME%\toolchains\llvm\prebuilt\%NDK_HOST%\bin\clang%EXE%" set "NDK_BROKEN=1"
if "%NDK_BROKEN%"=="1" (
    if /I not "%ANDROID_NDK_HOME%"=="%ANDROID_SDK_ROOT%\ndk\%NDK_VERSION%" (
        echo error: Custom ANDROID_NDK_HOME is incomplete: %ANDROID_NDK_HOME%;
        echo        unset it to install the pinned SDK NDK %NDK_VERSION%
        exit /b 1
    )
    set "MISSING=!MISSING! ndk;%NDK_VERSION%"
)
exit /b 0

rem :install_sdk_packages -- fetch the command-line tools if needed, then let
rem sdkmanager install exactly the missing packages. The licence prompts are
rem answered with y unless MUGEN_ACCEPT_LICENSES=0.
:install_sdk_packages
set "SDKM=%ANDROID_SDK_ROOT%\cmdline-tools\latest\bin\sdkmanager.bat"
if not exist "%SDKM%" (
    call :install_cmdline_tools
    if errorlevel 1 exit /b 1
)
set "PATH=%JAVA_HOME%\bin;%PATH%"
echo.
echo ==^> Installing missing SDK packages:!MISSING!
set "YES=%TOOLS%\tmp\yes.txt"
( for /l %%Y in (1,1,60) do @echo y ) > "%YES%"
if "%MUGEN_ACCEPT_LICENSES%"=="1" (
    echo     accepting the SDK licences on your behalf ^(set MUGEN_ACCEPT_LICENSES=0 to answer them^)
    call "%SDKM%" --sdk_root="%ANDROID_SDK_ROOT%" --licenses < "%YES%"
    if errorlevel 1 (
        echo error: sdkmanager --licenses failed; review its output and rerun
        exit /b 1
    )
)
call "%SDKM%" --sdk_root="%ANDROID_SDK_ROOT%" --install!MISSING!
if errorlevel 1 (
    echo error: sdkmanager failed; review its output and rerun port\android\build.bat --setup
    exit /b 1
)
exit /b 0

rem :provision_native_libs -- SDL2 + SDL2_mixer/SDL2_ttf through one script.
rem build_sdl2_companions.bat builds SDL2 first when the prefix is absent.
:provision_native_libs
if not exist "%TOOLSDIR%\build_sdl2_companions.bat" (
    echo error: %TOOLSDIR%\build_sdl2_companions.bat is missing; restore it from git
    exit /b 1
)
call "%TOOLSDIR%\build_sdl2_companions.bat"
if errorlevel 1 exit /b 1
exit /b 0

rem :jdk_ok PATH -> JDK_OK=1 when PATH is a JDK 17 that runs
rem android_jdk_ok requires bin/java and bin/javac and a `java -version` naming
rem 17. Requiring javac too is what rules out a bare JRE, and javac's own version
rem line is a cleaner thing to read than java's quoted one.
:jdk_ok
set "JDK_OK=0"
set "JDK_VERSION="
set "JO=%~1"
if not defined JO exit /b 0
for %%I in ("%JO%") do set "JO=%%~fI"
if not exist "%JO%\bin\java.exe" exit /b 0
if not exist "%JO%\bin\javac.exe" exit /b 0
set "JV="
for /f tokens^=2 %%V in ('"%JO%\bin\javac.exe" -version 2^>^&1') do if not defined JV set "JV=%%V"
if not "!JV:~0,3!"=="17." exit /b 0
set "JDK_OK=1"
set "JDK_VERSION=!JV!"
exit /b 0

rem ------------------------------------------------------ offline gradle
rem the MSYS2 build.sh that was removed looked for a complete distribution in the wrapper cache and calls it
rem directly, so the wrapper cannot start a download when the URL-hash directory
rem differs. On Windows the same tree holds bin\gradle.bat beside bin\gradle.
:find_gradle_offline
set "DISTS=%GRADLE_USER_HOME%\wrapper\dists\gradle-8.1.1-bin"
set "FOUND="
if exist "%DISTS%" for /d %%D in ("%DISTS%\*") do (
    if not defined FOUND if exist "%%~fD\gradle-8.1.1-bin.zip.ok" if exist "%%~fD\gradle-8.1.1\bin\gradle.bat" set "FOUND=%%~fD\gradle-8.1.1\bin\gradle.bat"
)
if not defined FOUND (
    echo error: Offline Gradle 8.1.1 distribution is not cached or incomplete; run an online build once
    echo        looked in %DISTS%
    exit /b 1
)
set "GRADLE_LAUNCHER=!FOUND!"
exit /b 0

rem ------------------------------------------------------- game data staging
:stage_game_data
if defined GAME_DATA_DIR ( set "GAME_DATA_EXPLICIT=1" ) else (
    set "GAME_DATA_EXPLICIT=0"
    set "GAME_DATA_DIR=%ROOT%\install"
)
for %%I in ("%GAME_DATA_DIR%") do set "GAME_DATA_DIR=%%~fI"
if "!GAME_DATA_DIR:~-1!"=="\" set "GAME_DATA_DIR=!GAME_DATA_DIR:~0,-1!"
if not exist "%GAME_DATA_DIR%\data\mugen.cfg" if "%GAME_DATA_EXPLICIT%"=="0" (
    echo.
    echo ==^> No game tree in %GAME_DATA_DIR%
    rem Port fix: the MSYS2 build.sh that was removed ran tools/fetch_game_data.sh here. That script is
    rem bash, curl and unzip, so it cannot run from cmd.exe; the tree is one
    rem release asset, so it is named instead of the downloader being reproduced.
    echo     the default tree is absent and this script cannot fetch it ^(bash^);
    echo     download the mugen_elecbyte_assets release zip into
    echo     "%ROOT%\install", or set GAME_DATA_DIR to a prepared game tree.
)
echo.
echo ==^> Staging game data from %GAME_DATA_DIR%
if not exist "%GAME_DATA_DIR%\data\mugen.cfg" (
    echo error: data/mugen.cfg missing in GAME_DATA_DIR=%GAME_DATA_DIR%; supply game data or use --no-assets
    exit /b 1
)
if not exist "%OUT%" mkdir "%OUT%"
set "LIST=%OUT%\asset-list.txt"
break > "%LIST%"
set "COUNT=0"
pushd "%GAME_DATA_DIR%"
rem Port fix: the MSYS2 build.sh that was removed built the manifest with `find . -type f` plus four
rem `! -name` tests and one `! -path`; cmd.exe has no find, so `dir /b /s /a-d`
rem supplies the same recursive list of files and the tests are applied one by
rem one. Extension tests stand in for `-name '*.exe'`: on a name, ending in .exe
rem and having the extension .exe are the same thing.
for /f "usebackq delims=" %%F in (`dir /b /s /a-d`) do (
    set "REL=%%F"
    set "REL=!REL:%GAME_DATA_DIR%\=!"
    set "NAME=%%~nxF"
    set "EXT=%%~xF"
    set "SKIP=0"
    if /I "!EXT!"==".exe" set "SKIP=1"
    if /I "!EXT!"==".dll" set "SKIP=1"
    if /I "!NAME!"=="mugen.log" set "SKIP=1"
    if /I "!NAME!"=="stdout.txt" set "SKIP=1"
    if /I "!NAME!"=="stderr.txt" set "SKIP=1"
    if /I "!REL:~0,19!"=="Elecbyte.MUGEN.libs" set "SKIP=1"
    if "!SKIP!"=="0" (
        set "RD=%%~dpF"
        set "RD=!RD:%GAME_DATA_DIR%\=!"
        if "!RD:~-1!"=="\" set "RD=!RD:~0,-1!"
        if not "!RD!"=="" if not exist "%ASSETS%\!RD!" mkdir "%ASSETS%\!RD!"
        copy /y "%%F" "%ASSETS%\!REL!" >nul
        rem The manifest keeps the removed .sh's forward slashes, so the staged list is
        rem the same text either script produces.
        set "LREL=!REL:\=/!"
        echo(!LREL!>>"%LIST%"
        set /a COUNT+=1
    )
)
popd
echo     !COUNT! file^(s^)

rem The GLES shader dialect is project code, not game data: staged into data\gles
rem so src/gles_render.c finds it, and added to the manifest so the list covers
rem what the APK carries.
echo.
echo ==^> Staging GLES shaders from res\shaders\gles
if not exist "%ROOT%\res\shaders\gles" (
    echo error: res/shaders/gles is missing; the GLES backend cannot load shaders
    exit /b 1
)
if not exist "%ASSETS%\data\gles" mkdir "%ASSETS%\data\gles"
set "SHCOUNT=0"
for %%S in ("%ROOT%\res\shaders\gles\*") do if not exist "%%~fS\" (
    copy /y "%%~fS" "%ASSETS%\data\gles\" >nul
    echo data/gles/%%~nxS>>"%LIST%"
    set /a SHCOUNT+=1
)
echo     !SHCOUNT! GLES shader^(s^)

rem Platform config: port\android\mugen.cfg holds the Android defaults, so the
rem APK ships settings that suit the device rather than a desktop's. GAME_CFG
rem stages a different file instead.
if defined GAME_CFG (
    for %%I in ("%GAME_CFG%") do set "GAME_CFG=%%~fI"
) else (
    set "GAME_CFG=%PORTDIR%\mugen.cfg"
)
if not exist "%GAME_CFG%" (
    rem Name the expected file, not just the path: a caller who deleted the
    rem template needs to know what to restore.
    echo error: platform mugen.cfg not found at %GAME_CFG%
    echo        set GAME_CFG=path, or restore port\android\mugen.cfg
    exit /b 1
)
if not exist "%ASSETS%\data" mkdir "%ASSETS%\data"
copy /y "%GAME_CFG%" "%ASSETS%\data\mugen.cfg" >nul
echo     data/mugen.cfg ^<- %GAME_CFG%
rem The platform overlay, merged over data/mugen.cfg per key at start-up
rem (src/configmerge.c). It carries only the keys whose right value depends on
rem the hardware, so a game tree's own settings survive everywhere else.
if exist "%PORTDIR%\mugen.android.cfg" (
    copy /y "%PORTDIR%\mugen.android.cfg" "%ASSETS%\data\mugen.android.cfg" >nul
    echo     data/mugen.android.cfg ^<- port\android\mugen.android.cfg
)

rem the MSYS2 build.sh that was removed finishes with `sort -o` on the manifest. Port fix: sort.exe and
rem find.exe are named explicitly, because Git for Windows puts a GNU sort and a
rem GNU find on PATH whose options are incompatible (/O versus -o, and find is
rem not findstr at all).
"%SystemRoot%\System32\sort.exe" "%LIST%" /O "%LIST%.sorted"
if errorlevel 1 (
    echo error: cannot sort %LIST%
    exit /b 1
)
move /y "%LIST%.sorted" "%LIST%" >nul
exit /b 0

rem ------------------------------------------------------------ properties
rem local.properties is a Java properties file: a backslash and a colon inside a
rem value both need escaping, the two substitutions props_escape makes.
:props_escape
set "PROPS=%~1"
set "PROPS=!PROPS:\=\\!"
set "PROPS=!PROPS::=\:!"
exit /b 0

rem ----------------------------------------------------------------- errors
:fail
exit /b 1

rem ===================================================================== usage
:usage
rem The header block at the top of this file, minus its "rem" prefixes: the same
rem help text the MSYS2 build.sh that was removed printed, so the two cannot drift apart.
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
