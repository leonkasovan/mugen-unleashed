@echo off
rem ===========================================================================
rem  build.bat -- build mugen-unleashed for the PlayStation Vita from cmd.exe
rem
rem  Same job as the removed psvita the MSYS2 build.sh that was removed, without MSYS2, Git Bash or WSL: the only
rem  tools used are the ones Windows ships (curl.exe, PowerShell for the
rem  checksums, the System32 bsdtar for the archives) plus a devkit's own
rem  cmake/ninja/make/pkg-config. Where the MSYS2 build.sh that was removed needs a POSIX shell this file
rem  uses cmd.exe builtins instead; nothing here calls uname, cygpath or sh.
rem
rem    port\psvita\build.bat                install what is missing, then build
rem    port\psvita\build.bat --setup        install VitaSDK and its packages
rem    port\psvita\build.bat --check-tools  report what is present, build nothing
rem    port\psvita\build.bat --clean        remove this port's build output
rem    port\psvita\build.bat --offline      fail instead of downloading anything
rem    port\psvita\build.bat --jobs N       parallel build jobs (default: CPU count)
rem    port\psvita\build.bat --model3d      basic glTF profile (on by default)
rem    port\psvita\build.bat --no-model3d   compile model support out; the profile
rem                                         is restricted and hardware
rem                                         qualification is still open
rem    port\psvita\build.bat --debug        Debug build (default: Release)
rem    port\psvita\build.bat --data DIR     pack this M.U.G.E.N tree into the VPK
rem    port\psvita\build.bat --no-data      pack no game data (engine only)
rem    port\psvita\build.bat --titleid ID   nine-character title id (MUGEN0001)
rem    port\psvita\build.bat --shader-compiler FILE  bundle this libshacccg.suprx
rem    port\psvita\build.bat --no-shader-compiler    bundle no compiler
rem    port\psvita\build.bat --render-cache / --no-render-cache  draw-state A/B
rem    port\psvita\build.bat --shader-cache / --no-shader-cache  disk cache
rem    port\psvita\build.bat --gl-selftest  startup GL smoke + frame-60 capture
rem    port\psvita\build.bat --env          print the exports, build nothing
rem
rem  Environment:
rem    VITASDK_DIR              where VitaSDK lives (default C:\vitasdk)
rem    VITASDK                  honoured only when a working SDK is there
rem    BUILD                    output directory (default build\psvita)
rem    VITASDK_URL / VITASDK_SHA256          re-published SDK archive
rem    VITA_LIBS_URL / VITA_LIBS_SHA256      re-published compiler asset
rem    CMAKE_GENERATOR          override the generator (default Ninja)
rem    MUGEN_WIN_KIT            kit to take cmake/ninja/pkg-config from
rem                             (autodetected: C:\w64devkit, C:\x86devkit,
rem                              then PATH)
rem
rem  Output: build\psvita\mugen-psvita.self   the runnable module
rem          build\psvita\mugen-psvita.vpk    installable homebrew (VitaShell)
rem
rem  The Vita's configuration is port\psvita\mugen.psvita.cfg, packed as
rem  data/mugen.psvita.cfg and merged over the game's data/mugen.cfg at
rem  start-up. RenderMode, Precache, GameWidth/GameHeight and the video size
rem  are read from that file; the flags that used to write them
rem  (--renderer, --precache, --resolution, --game-resolution,
rem  --output-resolution) fail with a message naming the file rather than being
rem  silently ignored.
rem
rem  Nothing here writes outside the repository except VITASDK itself, which is
rem  not our artefact and cannot work anywhere else: downloads are cached in
rem  build\downloads, staging happens in build\tmp, and the SDK is installed at
rem  the path the caller named because that is where a Vita toolchain has to
rem  live to be usable by other tools too.
rem ===========================================================================

setlocal EnableExtensions EnableDelayedExpansion

rem The port's own directory and the repository root, from anywhere, with no
rem trailing backslash. "%~dp0." makes the .. resolution work even when the
rem script is invoked through a relative path.
for %%I in ("%~dp0.") do set "PORTDIR=%%~fI"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"
set "DOWNLOADS=%ROOT%\build\downloads"
set "SCRATCH=%ROOT%\build\tmp"

rem ------------------------------------------------------------ pinned assets
rem The SHA-256 values are the assets' own published digests, checked every time
rem a cached copy is used; the URL/HASH variables override both for a fork or a
rem re-published asset (a changed archive must not silently become the SDK that
rem gets linked).
set "VITASDK_FILE=vitasdk.zip"
set "VITASDK_URL=https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/vitasdk.zip"
set "VITASDK_SHA256=12410f2edaedb5de51bfba8a66c4c807dd587ee540311d846544daa556fb4b7d"
if defined VITASDK_URL_OVERRIDE set "VITASDK_URL=%VITASDK_URL_OVERRIDE%"

rem The vitaGL shader compiler that goes into the VPK as module/libshacccg.suprx.
rem It is a decrypted Sony module, so it is a release asset of this project.
set "VITA_LIBS_FILE=lib-for-vita.zip"
set "VITA_LIBS_URL=https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/lib-for-vita.zip"
set "VITA_LIBS_SHA256=0d1124a94e5e22ac0544414b7450b6b0ec5ad96e9b5dccf601bd60675ee8ec3e"

rem The VitaSDK packages the port links against, as "vdpm package:artifact the
rem CMake configure actually needs". Checked by artifact, not by vdpm's own
rem bookkeeping, because the SDK may have been installed by hand and then it has
rem no package database to ask. Space-separated here because cmd.exe has no
rem arrays; no entry contains a space.
set "VITA_PACKAGES=sdl2:arm-vita-eabi/include/SDL2/SDL.h sdl2:arm-vita-eabi/lib/libSDL2.a sdl2_mixer:arm-vita-eabi/lib/libSDL2_mixer.a sdl2_ttf:arm-vita-eabi/lib/libSDL2_ttf.a mpg123:arm-vita-eabi/lib/libmpg123.a zlib:arm-vita-eabi/lib/libz.a libpng:arm-vita-eabi/lib/libpng.a vitaGL:arm-vita-eabi/lib/libvitaGL.a vitaGL:arm-vita-eabi/lib/libvitashark.a vitaGL:arm-vita-eabi/lib/libSceShaccCgExt.a vitaGL:arm-vita-eabi/lib/libmathneon.a"

rem ------------------------------------------------------------------ defaults
set "BUILD_TYPE=Release"
set "BUILD_OUT=%BUILD%"
if not defined BUILD_OUT set "BUILD_OUT=build\psvita"
set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=4"
set "CLEAN=0"
set "OFFLINE=0"
set "DO_SETUP=0"
set "CHECK_TOOLS=0"
set "PRINT_ENV=0"
set "DATA_SET=0"
set "DATA_DIR="
set "TITLEID=MUGEN0001"
set "MODEL3D=ON"
set "GL_SELFTEST=OFF"
set "RENDER_CACHE="
set "SHADER_CACHE="
rem Empty until :install_vita_libs resolves it: the default input is the release
rem asset it unpacks into build\downloads. An explicit --shader-compiler /
rem --no-shader-compiler sets SHADER_COMPILER_SET, which keeps that resolution
rem out of it.
set "SHADER_COMPILER="
set "SHADER_COMPILER_SET=0"

rem ---------------------------------------------------------------------- args
rem A command line is parsed here rather than by a POSIX getopts loop, and the
rem value forms of the flags (--jobs=8, --data=DIR, ...) are handled explicitly
rem because cmd.exe has no "case pattern".
:parse
if "%~1"=="" goto parsed
set "ARG=%~1"
set "ARGNAME=!ARG!"
for /f "tokens=1 delims==" %%N in ("!ARG!") do set "ARGNAME=%%N"

if /I "!ARG!"=="--help"              goto usage
if /I "!ARG!"=="-h"                  goto usage
if /I "!ARG!"=="--setup"             ( set "DO_SETUP=1" & shift & goto parse )
if /I "!ARG!"=="--check-tools"       ( set "CHECK_TOOLS=1" & shift & goto parse )
if /I "!ARG!"=="--env"               ( set "PRINT_ENV=1" & shift & goto parse )
if /I "!ARG!"=="--clean"             ( set "CLEAN=1" & shift & goto parse )
if /I "!ARG!"=="--offline"           ( set "OFFLINE=1" & shift & goto parse )
if /I "!ARG!"=="--debug"             ( set "BUILD_TYPE=Debug" & shift & goto parse )
if /I "!ARG!"=="--gl-selftest"       ( set "GL_SELFTEST=ON" & shift & goto parse )
if /I "!ARG!"=="--model3d"           ( set "MODEL3D=ON" & shift & goto parse )
if /I "!ARG!"=="--no-model3d"        ( set "MODEL3D=OFF" & shift & goto parse )
if /I "!ARG!"=="--render-cache"      ( set "RENDER_CACHE=ON" & shift & goto parse )
if /I "!ARG!"=="--no-render-cache"   ( set "RENDER_CACHE=OFF" & shift & goto parse )
if /I "!ARG!"=="--shader-cache"      ( set "SHADER_CACHE=ON" & shift & goto parse )
if /I "!ARG!"=="--no-shader-cache"   ( set "SHADER_CACHE=OFF" & shift & goto parse )
if /I "!ARG!"=="--no-shader-compiler" ( set "SHADER_COMPILER_SET=1" & set "SHADER_COMPILER=" & shift & goto parse )
if /I "!ARG!"=="--no-data"           ( set "DATA_SET=1" & set "DATA_DIR=" & shift & goto parse )

rem Retired with the build-time config patch: these values live in
rem port\psvita\mugen.psvita.cfg now, which the VPK packs verbatim and the
rem engine merges over data/mugen.cfg. Failing here beats accepting a flag that
rem no longer has anything to change.
if /I "!ARGNAME!"=="--resolution"        goto retired_size
if /I "!ARGNAME!"=="--game-resolution"   goto retired_size
if /I "!ARGNAME!"=="--output-resolution" goto retired_size
if /I "!ARGNAME!"=="--renderer"          goto retired_renderer
if /I "!ARGNAME!"=="--precache"          goto retired_precache

if /I "!ARG!"=="--jobs"   ( set "JOBS=%~2" & shift & shift & goto parse )
if /I "!ARG!"=="--data"   ( set "DATA_DIR=%~2" & set "DATA_SET=1" & shift & shift & goto parse )
if /I "!ARG!"=="--titleid" ( set "TITLEID=%~2" & shift & shift & goto parse )
if /I "!ARG!"=="--shader-compiler" (
    set "SHADER_COMPILER_SET=1"
    set "SHADER_COMPILER=%~2"
    shift
    shift
    if not defined SHADER_COMPILER goto need_shader_compiler
    goto parse
)

rem The =VALUE forms. The substring offset is the length of the flag plus the
rem "=", which is why each one is spelled out rather than computed.
if /I "!ARGNAME!"=="--jobs" ( set "JOBS=!ARG:~7!" & shift & goto parse )
if /I "!ARGNAME!"=="--data" ( set "DATA_DIR=!ARG:~7!" & set "DATA_SET=1" & shift & goto parse )
if /I "!ARGNAME!"=="--titleid" ( set "TITLEID=!ARG:~10!" & shift & goto parse )
if /I "!ARGNAME!"=="--shader-compiler" (
    set "SHADER_COMPILER_SET=1"
    set "SHADER_COMPILER=!ARG:~18!"
    shift
    if not defined SHADER_COMPILER goto need_shader_compiler
    goto parse
)

echo error: unknown argument: %~1  (--help)
exit /b 2
:parsed

if "%OFFLINE%"=="1" if "%DO_SETUP%"=="1" (
    echo error: --offline cannot be combined with --setup
    exit /b 2
)
echo %JOBS%| findstr /r /c:"^[1-9][0-9]*$" >nul || (
    echo error: --jobs must be a positive integer, not '%JOBS%'
    exit /b 2
)

rem ------------------------------------------------------------------ VitaSDK
rem VITASDK_DIR is where the SDK gets installed; C:\vitasdk is what VitaSDK's
rem own documentation and this port's README use. An exported VITASDK is
rem honoured only when a working SDK is really there, since a shell may export
rem VITASDK at a path that holds nothing; installing 600 MB somewhere other than
rem the documented C:\vitasdk would be worse than ignoring the variable.
rem VITASDK_DIR always wins when given.
if not defined VITASDK_DIR (
    if defined VITASDK if exist "%VITASDK%\bin\arm-vita-eabi-gcc.exe" set "VITASDK_DIR=%VITASDK%"
)
if not defined VITASDK_DIR set "VITASDK_DIR=C:\vitasdk"
if defined VITASDK_DIR for %%I in ("%VITASDK_DIR%") do set "VITASDK_DIR=%%~fI"

set "VITASDK_WIN=%VITASDK_DIR%"
rem Port fix: the variable the child processes see is the forward-slash form.
rem port\psvita\CMakeLists.txt builds every SDK path as ${MUGEN_VITASDK}/arm-vita-
rem eabi/..., and a backslash there reaches CMake as an escape: FindPkgConfig
rem stops the configure with "Invalid character escape '\v'" (for C:\vitasdk).
rem the MSYS2 build.sh that was removed got the forward form for free from cygpath -m. File operations in
rem this script keep using VITASDK_WIN, which is the form cmd.exe wants.
set "VITASDK=%VITASDK_WIN:\=/%"
set "TOOLCHAIN_FILE_WIN=%VITASDK%/share/vita.toolchain.cmake"
set "CC_BIN=%VITASDK_WIN%\bin\arm-vita-eabi-gcc.exe"

rem BUILD is the repository's own convention for "where output goes"; a relative
rem value is taken from the repository root, and CMake writes its own generator
rem files there.
if "!BUILD_OUT:~1,1!"==":" goto out_abs
if "!BUILD_OUT:~0,1!"=="\" goto out_abs
if "!BUILD_OUT:~0,1!"=="/" goto out_abs
set "OUT=%ROOT%\%BUILD_OUT%"
goto out_done
:out_abs
for %%I in ("%BUILD_OUT%") do set "OUT=%%~fI"
:out_done

rem The environment steps the build needs, for the duration of this script.
rem A batch file cannot change its caller's environment; --env prints the three
rem lines to set in your own shell.
set "PATH=%VITASDK_WIN%\bin;%PATH%"
set "CMAKE_TOOLCHAIN_FILE=%TOOLCHAIN_FILE_WIN%"

if "%PRINT_ENV%"=="1" (
    rem the MSYS2 build.sh that was removed printed "export" lines to eval; cmd.exe's equivalent is "set".
    echo set "VITASDK=%VITASDK%"
    echo set "PATH=%VITASDK_WIN%\bin;%%PATH%%"
    echo set "CMAKE_TOOLCHAIN_FILE=%TOOLCHAIN_FILE_WIN%"
    goto :eof
)

rem ---------------------------------------------------------------------- kit
rem the MSYS2 build.sh that was removed assumes cmake, ninja/make and pkg-config are on PATH because MSYS2
rem got them; cmd.exe has none of those. A devkit's bin directory has all three,
rem so it is found the way port\arm64\build.bat finds its kit, and only when the
rem tools are not already resolvable. Nothing else is taken from the kit: this
rem port compiles with the VitaSDK's own arm-vita-eabi-gcc.
set "KIT="
call :need_kit
if "%NEED_KIT%"=="0" goto kit_done
if defined MUGEN_WIN_KIT call :try_kit "%MUGEN_WIN_KIT%"
if not defined KIT call :try_kit "C:\w64devkit"
if not defined KIT call :try_kit "C:\x86devkit"
if not defined KIT for %%D in ("%PATH:;=" "%") do call :try_kit "%%~D"
if not defined KIT (
    echo error: cmake, ninja/make or pkg-config is missing and no devkit was found.
    echo        Install w64devkit or x86devkit, or point MUGEN_WIN_KIT at one:
    echo          set MUGEN_WIN_KIT=C:\w64devkit
    exit /b 1
)
set "PATH=%KIT%\bin;%PATH%"
:kit_done

rem PowerShell's Get-FileHash (certutil as a fallback) for the checksums and the
rem System32 bsdtar for the archives: both ship with Windows 10 and later.
for %%T in ("%SystemRoot%\System32\tar.exe") do if not exist "%%~fT" (
    echo error: %%~fT is missing; the archives need Windows' bundled bsdtar.
    exit /b 1
)

rem --------------------------------------------------------------------- clean
rem Only this port's own output directory, which is where CMake writes: the
rem downloads cache and build\tmp are shared with the other ports and are not
rem this script's to remove.
if "%CLEAN%"=="1" (
    if exist "%OUT%" (
        echo.
        echo ==^> Cleaning %OUT%
        rmdir /s /q "%OUT%"
    ) else (
        echo.
        echo ==^> Nothing to clean in %OUT%
    )
)

rem --check-tools reports and never touches the network, so it must not install
rem the SDK it is being asked about. Every other mode downloads what is
rem missing: that is what --offline exists to forbid.
if "%CHECK_TOOLS%"=="1" goto report

if "%DO_SETUP%"=="1" goto do_install
call :vitasdk_present
if "%PRESENT%"=="0" goto do_install
goto sdk_done

:do_install
call :install_vitasdk
if errorlevel 1 goto fail
:sdk_done

call :missing_artifacts
call :install_packages
if errorlevel 1 goto fail

if "%DO_SETUP%"=="1" (
    echo.
    echo ==^> Setup complete; nothing was built
    echo     VitaSDK  %VITASDK_WIN%
    echo     build with: port\psvita\build.bat
    goto :eof
)

rem ----------------------------------------------------------------- game data
rem A VPK with no data cannot start, so the default is to pack the project's own
rem M.U.G.E.N tree, the same one make install and the Android build use, when it
rem looks like one. --data / --no-data override it.
if "%DATA_SET%"=="1" goto data_done
if exist "%ROOT%\install\data\system.def" set "DATA_DIR=%ROOT%\install"
:data_done
rem Port fix: a relative --data is resolved against the current directory here.
rem the MSYS2 build.sh that was removed passes it through unchanged and CMake then reads it relative to the
rem build directory, which is almost never the tree that was meant.
if defined DATA_DIR for %%I in ("%DATA_DIR%") do set "DATA_DIR=%%~fI"
rem Guarded: %VAR:a=b% on an undefined variable does not expand to nothing in
rem cmd.exe, it expands to junk, and -DMUGEN_VITA_DATA_DIR=/= is a configure
rem error rather than "no data". Empty must stay empty.
set "DATA_FWD="
if defined DATA_DIR set "DATA_FWD=%DATA_DIR:\=/%"

rem ------------------------------------------------------------------ build
rem Generator: Ninja when available (fast, and what the README shows). "MinGW
rem Makefiles" is deliberately not a fallback: it refuses to configure when
rem sh.exe is on PATH, which it always is through a devkit. "Unix Makefiles"
rem works with the kit's make and its sh.exe.
set "GENERATOR=%CMAKE_GENERATOR%"
if defined GENERATOR goto gen_done
where ninja.exe >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    goto gen_done
)
where make.exe >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Unix Makefiles"
    goto gen_done
)
echo error: neither ninja nor make found; install one (w64devkit has both)
exit /b 1
:gen_done

rem A build directory configured against a different VitaSDK cannot be reused --
rem CMake stores the toolchain file path in its cache and refuses a changed one.
rem Clearing the cache is the only repair, and it has to be said out loud.
if not exist "%OUT%\CMakeCache.txt" goto cache_done
set "PREVTOOL="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_TOOLCHAIN_FILE:" "%OUT%\CMakeCache.txt"') do if not defined PREVTOOL set "PREVTOOL=%%B"
if not defined PREVTOOL goto cache_done
if /I "!PREVTOOL!"=="%TOOLCHAIN_FILE_WIN%" goto cache_done
echo     note: %OUT% was configured for !PREVTOOL!
echo           clearing its CMake cache for %TOOLCHAIN_FILE_WIN%
del /q "%OUT%\CMakeCache.txt"
if exist "%OUT%\CMakeFiles" rmdir /s /q "%OUT%\CMakeFiles"
:cache_done

echo.
echo ==^> Configuring mugen-unleashed for the PS Vita
echo     VitaSDK   %VITASDK_WIN%
echo     toolchain %TOOLCHAIN_FILE_WIN%
echo     build     %BUILD_TYPE%, %GENERATOR%
echo     output    %OUT%
if defined DATA_DIR (
    echo     game data %DATA_DIR% ^(packed into the VPK^)
) else (
    echo     game data none ^(engine-only VPK; see --data^)
)
echo     title id  %TITLEID%
call :install_vita_libs
if errorlevel 1 goto fail
echo     compiler  %COMPILER_REPORT%
rem The bubble icon and the LiveArea artwork come from port\psvita\cmake\
rem Icon.cmake, which can derive them from res\icons\1.ico with Python3. This
rem script disables that lookup (CMAKE_DISABLE_FIND_PACKAGE_Python3), so the
rem committed files in port\psvita\sce_sys\ are packed instead. That decision
rem is the module's, so this build does not attempt a second opinion -- the
rem configure output names the file each PNG came from.
echo     artwork   port\psvita\sce_sys (committed copies)

if "%GL_SELFTEST%"=="ON" goto want_vitagl
if "%SHADER_CACHE%"=="ON" goto want_vitagl
goto vitagl_done
:want_vitagl
if exist "%ROOT%\third_party\vitagl\src\source\vgl.c" goto vitagl_done
if "%OFFLINE%"=="1" (
    echo error: --offline: the vendored vitaGL source is missing from
    echo        %ROOT%\third_party\vitagl\src
    echo        ^(--gl-selftest and --shader-cache build it^)
    goto fail
)
call :fetch_vitagl
if errorlevel 1 goto fail
:vitagl_done

rem Port fix: every -D value is passed with forward slashes. A backslash inside
rem a -D argument is an escape once it reaches CMake, and CMakeLists.txt appends
rem "/arm-vita-eabi" to the paths it reads, so the mixed form is what breaks.
set "CFG_ARG=%PORTDIR:\=/%/mugen.psvita.cfg"

set "CMAKE_ARGS=-S "%PORTDIR%" -B "%OUT%" -G "%GENERATOR%""
set "CMAKE_ARGS=%CMAKE_ARGS% "-DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN_FILE_WIN%""
set "CMAKE_ARGS=%CMAKE_ARGS% "-DCMAKE_BUILD_TYPE=%BUILD_TYPE%""
set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_DATA_DIR=%DATA_FWD%""
set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_GL_SELFTEST=%GL_SELFTEST%""
set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_MODEL3D=%MODEL3D%""
set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_CFG=%CFG_ARG%""
rem The resolved default is passed on every build so the VPK keeps its bundled
rem module; an empty value is what --no-shader-compiler asks for, and it clears
rem the path an earlier build left in CMake's cache. Guarded for the same
rem reason DATA_FWD is: empty must reach CMake as empty.
set "COMPILER_FWD="
if defined SHADER_COMPILER set "COMPILER_FWD=%SHADER_COMPILER:\=/%"
set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_SHADER_COMPILER=%COMPILER_FWD%""
if defined TITLEID set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_TITLEID=%TITLEID%""
if defined RENDER_CACHE set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_RENDER_CACHE=%RENDER_CACHE%""
if defined SHADER_CACHE set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_SHADER_CACHE=%SHADER_CACHE%""
if "%GL_SELFTEST%"=="ON" set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_BUILD_VITAGL=ON""
if "%SHADER_CACHE%"=="ON" set "CMAKE_ARGS=%CMAKE_ARGS% "-DMUGEN_VITA_BUILD_VITAGL=ON""
rem No Python: cmake\Icon.cmake derives sce_sys from res\icons\1.ico with
rem tools/iconpack.py *if* it finds an interpreter. CMAKE_DISABLE_FIND_PACKAGE_
rem Python3 makes that find_package fail on purpose, so the configure always
rem takes Icon.cmake's committed-asset branch -- the same three PNGs the tree
rem ships, and the same ones a machine without Python already got. Doing it here
rem rather than by removing the code keeps the removed psvita the MSYS2 build.sh that was removed (MSYS2) able to
rem derive new artwork from res\icons while this entry point needs no Python.
set "CMAKE_ARGS=%CMAKE_ARGS% "-DCMAKE_DISABLE_FIND_PACKAGE_Python3=ON""

cmake.exe %CMAKE_ARGS%
if errorlevel 1 goto configure_failed

echo.
echo ==^> Building
echo     jobs      %JOBS%
cmake.exe --build "%OUT%" --parallel %JOBS%
if errorlevel 1 goto build_failed

set "SELF=%OUT%\mugen-psvita.self"
set "VPK=%OUT%\mugen-psvita.vpk"
if not exist "%SELF%" (
    echo error: the build reported success but "%SELF%" is missing.
    goto fail
)
if not exist "%VPK%" (
    echo error: the build reported success but "%VPK%" is missing.
    goto fail
)

echo.
echo ==^> Done
call :size_of "%SELF%" SELF_KB
echo     %SELF%  %SELF_KB%
call :size_of "%VPK%" VPK_KB
echo     %VPK%  %VPK_KB%
echo.
rem The console rejects an incomplete package with a generic promoter error and
rem no way to tell a short copy from a bad build, so the exact size and digest
rem are printed here: compare them before blaming the build (see
rem port\psvita\README.md, Troubleshooting).
call :hash_file SHA256 "%VPK%"
for %%A in ("%VPK%") do echo     bytes     %%~zA  %%~nxA
echo     sha256    %HASH%
echo.
echo     Copy the VPK to the console and install it with VitaShell:
echo       ux0:/  ^(USB or FTP mode^), then VitaShell -^> select mugen-psvita.vpk -^> Install
echo.
echo     The SELF cannot run on this machine; see port\psvita\README.md for
echo     diagnosing a failed start on the console.
goto :eof

rem ==================================================================== checks
rem :vitasdk_present -> PRESENT=1 when the two things the build cannot work
rem without are both there. A directory that merely exists is not enough: a
rem half-extracted SDK is the failure that breaks every later run while looking
rem like success.
:vitasdk_present
set "PRESENT=0"
if not exist "%CC_BIN%" goto :eof
if not exist "%VITASDK_WIN%\share\vita.toolchain.cmake" goto :eof
if not exist "%VITASDK_WIN%\share\vita.cmake" goto :eof
set "PRESENT=1"
goto :eof

rem :missing_artifacts -> MISSING (a leading space, then the vdpm package names)
:missing_artifacts
set "MISSING="
set "SEEN= "
for %%E in (%VITA_PACKAGES%) do (
    for /f "tokens=1,* delims=:" %%A in ("%%E") do (
        set "PKG=%%A"
        set "AFILE=%%B"
        set "WANT=1"
        if exist "%VITASDK_WIN%\!AFILE!" set "WANT=0"
        rem libpng is packaged as libpng.a or libpng16.a depending on the release.
        if /I "!AFILE!"=="arm-vita-eabi/lib/libpng.a" if exist "%VITASDK_WIN%\arm-vita-eabi\lib\libpng16.a" set "WANT=0"
        if "!WANT!"=="1" (
            echo !SEEN!| findstr /c:" !PKG! " >nul
            if errorlevel 1 (
                set "SEEN=!SEEN!!PKG! "
                set "MISSING=!MISSING! !PKG!"
            )
        )
    )
)
goto :eof

:install_packages
echo.
echo ==^> VitaSDK packages
if not defined MISSING (
    echo     all present in %VITASDK_WIN%
    exit /b 0
)
echo     missing:%MISSING%
if "%OFFLINE%"=="1" (
    echo error: --offline: these packages are not in %VITASDK_WIN%:%MISSING%
    echo        ^(reinstall from the pinned archive, or drop --offline^)
    exit /b 1
)
set "VDPM=%VITASDK_WIN%\bin\vdpm.exe"
if not exist "%VDPM%" (
    echo error: vdpm not found in %VITASDK_WIN%\bin; the SDK is incomplete
    exit /b 1
)
set "WANTED=%MISSING%"
echo     running: vdpm install%WANTED%
rem vdpm installs from the network and is the only step here that does; a
rem failure is reported with the manual command rather than swallowed, because
rem the build cannot link without what it was asked to fetch.
"%VDPM%" install%WANTED%
if errorlevel 1 (
    echo error: vdpm install%WANTED% failed
    echo        install them by hand, then rerun:
    echo          %VDPM% install%WANTED%
    exit /b 1
)
call :missing_artifacts
if defined MISSING (
    echo error: vdpm reported success but these are still missing:%MISSING%
    exit /b 1
)
echo     installed:%WANTED%
exit /b 0

rem ================================================================ downloads
rem :hash_file KIND FILE -> sets HASH (KIND is SHA256 or MD5)
:hash_file
set "HASH="
for /f "usebackq delims=" %%H in (`powershell.exe -NoProfile -NonInteractive -Command "(Get-FileHash -LiteralPath '%~2' -Algorithm %~1).Hash" 2^>nul`) do if not defined HASH set "HASH=%%H"
if defined HASH exit /b 0
rem certutil prints the digest on the second line, spaced in fours in places.
for /f "usebackq skip=1 delims=" %%H in (`certutil -hashfile "%~2" %~1 2^>nul`) do if not defined HASH (
    set "L=%%H"
    set "HASH=!L: =!"
)
if not defined HASH exit /b 1
exit /b 0

rem :download URL DEST
:download
echo     downloading %~nx2
echo     %~1
curl.exe --fail --location --silent --show-error --proto "=https" --retry 2 ^
    --connect-timeout 20 --max-time 3600 --output "%~2.part" "%~1"
if errorlevel 1 (
    if exist "%~2.part" del /q "%~2.part"
    echo error: download failed: %~1; rerun to retry
    exit /b 1
)
move /y "%~2.part" "%~2" >nul
exit /b 0

rem :extract ARCHIVE DEST  -- System32 bsdtar reads .zip and .tar.xz alike
:extract
if not exist "%~2" mkdir "%~2"
"%SystemRoot%\System32\tar.exe" -xf "%~1" -C "%~2"
if errorlevel 1 (
    echo error: cannot unpack %~nx1
    exit /b 1
)
exit /b 0

rem :try_kit DIR -- a devkit is a directory holding cmake, ninja and pkg-config
:try_kit
if not "%~1"=="" if exist "%~1\bin\cmake.exe" if exist "%~1\bin\pkg-config.exe" if not defined KIT set "KIT=%~1"
exit /b 0

rem :need_kit -> NEED_KIT=1 when the host tools are not already on PATH
:need_kit
set "NEED_KIT=0"
where cmake.exe >nul 2>&1 || set "NEED_KIT=1"
where pkg-config.exe >nul 2>&1 || set "NEED_KIT=1"
where ninja.exe >nul 2>&1 || where make.exe >nul 2>&1 || set "NEED_KIT=1"
exit /b 0

rem ============================================================ install steps
rem :install_vitasdk -- install the pinned archive as a transaction, or leave
rem the installed SDK exactly as it was.
:install_vitasdk
echo.
echo ==^> VitaSDK ^(%VITASDK_FILE%, sha256^)
call :vitasdk_present
if "%PRESENT%"=="1" (
    echo     already installed in %VITASDK_WIN%
    call :cc_version
    echo     ^(!CCVERSION!^)
    exit /b 0
)
if "%OFFLINE%"=="1" (
    echo error: --offline: no VitaSDK at %VITASDK_WIN%
    echo        ^(expected %CC_BIN% and share\vita.toolchain.cmake^)
    exit /b 1
)

if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%"
set "ARCHIVE=%DOWNLOADS%\%VITASDK_FILE%"

if exist "%ARCHIVE%" (
    call :hash_file SHA256 "%ARCHIVE%"
    if /I "!HASH!"=="%VITASDK_SHA256%" (
        echo     cached download verified ^(sha256^)
        goto vsd_unpack
    )
    echo     cached download fails its sha256; fetching again
    del /q "%ARCHIVE%"
)
echo     downloading %VITASDK_FILE% ^(about 196 MB, 600 MB unpacked^)
echo     %VITASDK_URL%
call :download "%VITASDK_URL%" "%ARCHIVE%"
if errorlevel 1 exit /b 1
call :hash_file SHA256 "%ARCHIVE%"
if /I not "!HASH!"=="%VITASDK_SHA256%" (
    del /q "%ARCHIVE%"
    echo error: %VITASDK_FILE% failed its SHA-256; nothing was extracted
    echo        expected %VITASDK_SHA256%
    echo        ^(a re-published asset needs VITASDK_SHA256^=^<new hash^>^)
    exit /b 1
)
echo     sha256 verified

:vsd_unpack
set "STAGE=%SCRATCH%\stage-vitasdk"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
echo     unpacking
call :extract "%ARCHIVE%" "%STAGE%"
if errorlevel 1 (
    if exist "%STAGE%" rmdir /s /q "%STAGE%"
    exit /b 1
)

rem The asset is packed with one top directory (vitasdk/), but that is not its
rem contract: whatever single directory appears is unwrapped, so VITASDK ends up
rem holding bin/, arm-vita-eabi/ and share/ directly.
set "TREE=%STAGE%"
if exist "%TREE%\bin\arm-vita-eabi-gcc.exe" goto vsd_tree
set "TREE="
for /d %%D in ("%STAGE%\*") do if not defined TREE set "TREE=%%~fD"
if not defined TREE goto vsd_layout
if not exist "!TREE!\bin\arm-vita-eabi-gcc.exe" goto vsd_layout
:vsd_tree
if not exist "!TREE!\share\vita.toolchain.cmake" goto vsd_layout

rem Install as a transaction beside the destination, then swap: a mistake or an
rem interrupted copy leaves the SDK that was there untouched.
set "DEST=%VITASDK_WIN%.mugen-new"
set "BACKUP=%VITASDK_WIN%.mugen-old"
if exist "%DEST%" goto vsd_txn
if exist "%BACKUP%" goto vsd_txn

rem An existing non-empty directory is only replaced when it is ours (carries
rem the marker below) or already is a VitaSDK, so a hand-made SDK is never
rem silently wiped.
set "NONEMPTY=0"
for /f "delims=" %%F in ('dir /b /a "%VITASDK_WIN%" 2^>nul') do set "NONEMPTY=1"
if "%NONEMPTY%"=="0" goto vsd_prepare
set "OURS=0"
for %%M in ("%VITASDK_WIN%\.mugen-vitasdk-*") do if exist "%%M" set "OURS=1"
if "%OURS%"=="1" goto vsd_prepare
call :vitasdk_present
if "%PRESENT%"=="1" goto vsd_prepare
rmdir /s /q "%STAGE%" 2>nul
echo error: %VITASDK_WIN% exists but is not a VitaSDK; move it aside or set
echo        VITASDK_DIR to another directory
exit /b 1

:vsd_prepare
for %%I in ("%VITASDK_WIN%") do if not exist "%%~dpI" mkdir "%%~dpI"
move /y "!TREE!" "%DEST%" >nul || goto vsd_stage_fail
set "MARKER12=%VITASDK_SHA256:~0,12%"
> "%DEST%\.mugen-vitasdk-%MARKER12%" echo %VITASDK_SHA256%
if exist "%VITASDK_WIN%" (
    move /y "%VITASDK_WIN%" "%BACKUP%" >nul || goto vsd_restore_fail
)
move /y "%DEST%" "%VITASDK_WIN%" >nul || goto vsd_restore_fail
if exist "%BACKUP%" rmdir /s /q "%BACKUP%"
rmdir /s /q "%STAGE%" 2>nul
echo     installing into %VITASDK_WIN%
call :vitasdk_present
if "%PRESENT%"=="0" (
    echo error: the install finished but %CC_BIN% is not there
    exit /b 1
)
call :cc_version
echo     VitaSDK !CCVERSION!
exit /b 0

:vsd_layout
rmdir /s /q "%STAGE%" 2>nul
echo error: unexpected archive layout: no bin\arm-vita-eabi-gcc.exe and
echo        share\vita.toolchain.cmake in %VITASDK_FILE%
echo        nothing was changed
exit /b 1
:vsd_txn
rmdir /s /q "%STAGE%" 2>nul
echo error: unfinished VitaSDK transaction at
echo        %DEST%
echo        or %BACKUP%
echo        The installed SDK was kept on purpose; delete the leftover and re-run.
exit /b 1
:vsd_stage_fail
rmdir /s /q "%STAGE%" 2>nul
echo error: cannot stage the SDK beside %VITASDK_WIN%
exit /b 1
:vsd_restore_fail
if not exist "%VITASDK_WIN%" if exist "%BACKUP%" move /y "%BACKUP%" "%VITASDK_WIN%" >nul
if exist "%DEST%" rmdir /s /q "%DEST%"
rmdir /s /q "%STAGE%" 2>nul
echo error: cannot install the SDK; the previous directory was restored
exit /b 1

rem :cc_version -> CCVERSION (the compiler's first version line)
:cc_version
set "CCVERSION=arm-vita-eabi-gcc (version unknown)"
if not exist "%CC_BIN%" goto :eof
for /f "usebackq delims=" %%V in (`"%CC_BIN%" --version 2^>nul`) do if not defined CCVERSION_FIRST set "CCVERSION_FIRST=%%V"
if defined CCVERSION_FIRST set "CCVERSION=!CCVERSION_FIRST!"
set "CCVERSION_FIRST="
goto :eof

rem ============================================================ shader compiler
rem The vitaGL shader compiler bundled as module/libshacccg.suprx. It is a
rem decrypted Sony module, so it is not tracked here: it is the lib-for-vita.zip
rem release asset, verified against the pinned SHA-256 and unpacked to
rem build\downloads\libshacccg.suprx, which becomes the default input -- the
rem cache directory every script in this repository shares.
rem An explicit --shader-compiler FILE or --no-shader-compiler is taken as
rem final: nothing is downloaded or unpacked.
:install_vita_libs
set "COMPILER_REPORT="
if not "%SHADER_COMPILER_SET%"=="0" (
    if defined SHADER_COMPILER (
        if not exist "%SHADER_COMPILER%" (
            echo error: --shader-compiler "%SHADER_COMPILER%" does not exist
            exit /b 1
        )
        set "COMPILER_REPORT=%SHADER_COMPILER% ^(supplied^)"
    ) else (
        set "COMPILER_REPORT=none ^(--no-shader-compiler; an OpenGL RenderMode then"
        set "COMPILER_REPORT=!COMPILER_REPORT! needs libshacccg.suprx on the console^)"
    )
    exit /b 0
)

set "ARCHIVE=%DOWNLOADS%\%VITA_LIBS_FILE%"
set "DESTFILE=%DOWNLOADS%\libshacccg.suprx"
set "SHADER_COMPILER="

if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%"
if not exist "%ARCHIVE%" goto vlib_need
call :hash_file SHA256 "%ARCHIVE%"
if /I not "!HASH!"=="%VITA_LIBS_SHA256%" goto vlib_stale
echo     compiler  cached %VITA_LIBS_FILE% verified ^(sha256^)
goto vlib_unpack

:vlib_stale
rem A cached archive that fails its digest is not the asset this build was
rem validated against; it is removed rather than unpacked.
del /q "%ARCHIVE%"
:vlib_need
if "%OFFLINE%"=="1" (
    echo     compiler  none: --offline and %VITA_LIBS_FILE% is not cached
    echo               an OpenGL RenderMode then needs libshacccg.suprx
    echo               installed on the console ^(ur0:/data/^)
    set "COMPILER_REPORT=none ^(--offline, asset not cached^)"
    exit /b 0
)
echo     compiler  downloading %VITA_LIBS_FILE% ^(vitaGL shader compiler^)
echo               %VITA_LIBS_URL%
call :download "%VITA_LIBS_URL%" "%ARCHIVE%"
if errorlevel 1 exit /b 1
call :hash_file SHA256 "%ARCHIVE%"
if /I not "!HASH!"=="%VITA_LIBS_SHA256%" (
    del /q "%ARCHIVE%"
    echo error: %VITA_LIBS_FILE% failed its SHA-256; nothing was unpacked
    echo        expected %VITA_LIBS_SHA256%
    echo        ^(a re-published asset needs VITA_LIBS_SHA256^=^<new hash^>^)
    exit /b 1
)
echo     sha256 verified

:vlib_unpack
rem Port fix: the single member is extracted into a staging directory and moved
rem into place, so only a complete extraction is ever published as the default
rem input (the same rule the MSYS2 build.sh that was removed applies with unzip -p and a .part file).
set "STAGE=%SCRATCH%\stage-shader-compiler"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
"%SystemRoot%\System32\tar.exe" -xf "%ARCHIVE%" -C "%STAGE%" libshacccg.suprx
if errorlevel 1 (
    rmdir /s /q "%STAGE%"
    echo error: %ARCHIVE% does not contain a readable libshacccg.suprx
    echo        nothing was unpacked
    exit /b 1
)
if not exist "%STAGE%\libshacccg.suprx" (
    rmdir /s /q "%STAGE%"
    echo error: %ARCHIVE% does not contain libshacccg.suprx
    echo        nothing was unpacked
    exit /b 1
)
for %%A in ("%STAGE%\libshacccg.suprx") do set "SSIZE=%%~zA"
if "%SSIZE%"=="0" (
    rmdir /s /q "%STAGE%"
    echo error: libshacccg.suprx is empty in %ARCHIVE%
    exit /b 1
)
if exist "%DESTFILE%" del /q "%DESTFILE%"
move /y "%STAGE%\libshacccg.suprx" "%DESTFILE%" >nul
rmdir /s /q "%STAGE%" 2>nul
if not exist "%DESTFILE%" (
    echo error: cannot write %DESTFILE%
    exit /b 1
)
set "SHADER_COMPILER=%DESTFILE%"
set "COMPILER_REPORT=%DESTFILE% ^(goes in as module/libshacccg.suprx^)"
exit /b 0

rem ================================================================== vitaGL
rem the MSYS2 build.sh that was removed called tools\psvita\fetch_vitagl.sh, which is a /bin/sh script.
rem Rather than reach for a POSIX shell, the same three commands are run
rem directly: the vendored tree is not committed, so it is cloned at the exact
rem commit the port was validated against. The staging directory is build\tmp
rem rather than the the removed .sh's build\psvita so it stays out of the tree CMake
rem owns.
:fetch_vitagl
set "VPIN=9bf3e3f4e55efd1f4a191b179144f7f9a98cd925"
set "VURL=https://github.com/Rinnegatamante/vitaGL.git"
set "VDEST=%ROOT%\third_party\vitagl\src"
where git.exe >nul 2>&1 || (
    echo error: --gl-selftest and --shader-cache build the vendored vitaGL, and
    echo        fetching it needs git.exe on PATH
    exit /b 1
)
echo     fetching vitaGL %VPIN%
set "VTMP=%SCRATCH%\vitagl-fetch"
if exist "%VTMP%" rmdir /s /q "%VTMP%"
git.exe clone --quiet "%VURL%" "%VTMP%"
if errorlevel 1 (
    if exist "%VTMP%" rmdir /s /q "%VTMP%"
    echo error: git clone failed; nothing was replaced
    exit /b 1
)
git.exe -C "%VTMP%" checkout --quiet %VPIN%
if errorlevel 1 (
    rmdir /s /q "%VTMP%"
    echo error: cannot check out vitaGL %VPIN%; nothing was replaced
    exit /b 1
)
if exist "%VDEST%" rmdir /s /q "%VDEST%"
if not exist "%ROOT%\third_party\vitagl" mkdir "%ROOT%\third_party\vitagl"
move /y "%VTMP%" "%VDEST%" >nul
if errorlevel 1 (
    rmdir /s /q "%VTMP%" 2>nul
    echo error: cannot move the vitaGL source into %VDEST%
    exit /b 1
)
if exist "%VDEST%\.git" rmdir /s /q "%VDEST%\.git"
if exist "%VDEST%\samples" rmdir /s /q "%VDEST%\samples"
if exist "%VDEST%\.github" rmdir /s /q "%VDEST%\.github"
echo     vitaGL source ready at %VDEST%
exit /b 0

rem ================================================================== reports
rem :size_of FILE VARNAME -- rounded KB into the named variable
:size_of
set "RSIZE=0"
for %%A in ("%~1") do set "RSIZE=%%~zA"
set /a RKB=(%RSIZE%+512)/1024
set "%~2=%RKB% KB"
exit /b 0

:report
echo.
echo ==^> Tools
rem The kit is what supplies cmake/ninja/pkg-config when cmd.exe has none of
rem them; naming it first makes a report that picks the wrong one obvious.
if defined KIT echo   kit        %KIT%
call :vitasdk_present
if "%PRESENT%"=="0" (
    echo   VitaSDK    MISSING  ^(%VITASDK_WIN%^)
    echo              run: port\psvita\build.bat --setup
    set "RC=1"
) else (
    call :cc_version
    echo   VitaSDK    ok       !CCVERSION!
    echo              %VITASDK_WIN%
    echo              toolchain file %TOOLCHAIN_FILE_WIN%
    set "RC=0"
)
call :missing_artifacts
if defined MISSING (
    echo   packages   MISSING %MISSING%
    echo              run: port\psvita\build.bat   ^(it runs vdpm install^)
    set "RC=1"
) else (
    echo   packages   ok       sdl2 sdl2_mixer sdl2_ttf mpg123 zlib libpng vitaGL
)
where cmake.exe >nul 2>&1 && (
    for /f "usebackq delims=" %%V in (`cmake.exe --version 2^>nul`) do if not defined CV set "CV=%%V"
    echo   cmake      ok       !CV!
    set "CV="
) || (
    echo   cmake      MISSING  ^(the kit's cmake, or install one^)
    set "RC=1"
)
where ninja.exe >nul 2>&1 && (
    for /f "usebackq delims=" %%V in (`ninja.exe --version 2^>nul`) do if not defined NV set "NV=%%V"
    echo   ninja      ok       !NV!
    set "NV="
) || (
    where make.exe >nul 2>&1 && (
        for /f "usebackq delims=" %%V in (`make.exe --version 2^>nul`) do if not defined MV set "MV=%%V"
        echo   make       ok       !MV!  ^(no Ninja; CMake will use Unix Makefiles^)
        set "MV="
    ) || (
        echo   ninja/make MISSING  ^(install ninja or make^)
        set "RC=1"
    )
)
rem CMakeLists.txt reads the SDK's .pc files through a *host* pkg-config (the
rem SDK's own arm-vita-eabi-pkg-config is a bash script Windows cannot exec).
where pkg-config.exe >nul 2>&1 && (
    for /f "usebackq delims=" %%V in (`pkg-config.exe --version 2^>nul`) do echo   pkg-config ok       %%V
) || (
    echo   pkg-config MISSING  ^(the kit's pkg-config, or install pkgconf^)
    set "RC=1"
)
rem No Python row on purpose: the configure below disables CMake's Python3
rem lookup, so the VPK always packs port\psvita\sce_sys. Ports that do want the
rem derivation from res\icons\1.ico are the removed psvita the MSYS2 build.sh that was removed and the tests.
:report_end
if not "%RC%"=="0" (
    echo.
    echo ==^> Missing pieces
    echo     port\psvita\build.bat --setup   installs VitaSDK and its packages
)
rem setlocal is undone by exit /b taking the errorlevel out with it
exit /b %RC%

rem ==================================================================== errors
:need_shader_compiler
echo error: --shader-compiler requires a file
exit /b 2
:retired_size
echo error: %ARGNAME% is retired: the game and output sizes are [Config]
echo        GameWidth/GameHeight and [Video] Width/Height in
echo        %PORTDIR%\mugen.psvita.cfg. Edit that file.
exit /b 1
:retired_renderer
echo error: %ARGNAME% is retired: the renderer is [Video] RenderMode in
echo        %PORTDIR%\mugen.psvita.cfg ^(system ^| opengl ^| openglscreen^).
echo        Edit that file.
exit /b 1
:retired_precache
echo error: %ARGNAME% is retired: precache is [Misc] Precache in
echo        %PORTDIR%\mugen.psvita.cfg ^(0 is the confirmed hardware workaround^).
exit /b 1
:configure_failed
echo.
echo error: the CMake configure failed; nothing was built
exit /b 1
:build_failed
echo.
echo error: the build failed; an existing VPK may be from an earlier build
echo        if vita-elf-create crashed, keep %OUT%\mugen ^(the linked ARM ELF^)
echo        the ELF and converter output are needed to diagnose the Windows
echo        host-tool failure
exit /b 1
:fail
exit /b 1

rem ===================================================================== usage
:usage
rem The header block at the top of this file, minus its "rem" prefixes: the
rem same help text the MSYS2 build.sh that was removed printed, so the two cannot drift apart.
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
