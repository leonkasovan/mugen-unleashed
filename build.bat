@echo off
rem ===========================================================================
rem build.bat - build mugen-unleashed on Windows from nothing
rem
rem   build.bat             install what is missing, then build win32 and win64
rem   build.bat /build      the same thing, spelled out
rem   build.bat /build64    ... then build win64 only
rem   build.bat /build32    ... then build win32 only
rem   build.bat /check      only verify what is installed, install nothing
rem   build.bat /force      re-download and re-extract even if installed
rem   build.bat /help
rem
rem The Windows counterpart of port\arm64\build.bat, and named the same way: no
rem arguments is "do the whole job". Installing and building live in one file
rem because the build needs the kit first on PATH, so the two cannot disagree
rem about which compiler or tree is in play.
rem
rem What it installs:
rem
rem   1. w64devkit 2.11.0 (x64)  ->  %W64DEVKIT_DIR%   default C:\w64devkit
rem   2. w64devkit 2.11.0 (x86)  ->  %X86DEVKIT_DIR%   default C:\x86devkit
rem   3. win64-sysroot.zip       ->  build\sysroots\win64-sysroot
rem   4. win32-sysroot.zip       ->  build\sysroots\win32-sysroot
rem
rem Downloads are cached in build\downloads and unpacking happens in
rem build\tmp, so the three folders under build\ follow the project rule:
rem downloads are network caches, sysroots are installed trees, tmp is scratch.
rem
rem The kits are skeeto's w64devkit: gcc, binutils, make, sh/sed/awk and their
rem own pkg-config -- and nothing else, no SDL, no libpng, no .pc files at all.
rem The dependencies come from this project's published dependency trees
rem (toolchains-v1.0 release): SDL2, SDL2_mixer, SDL2_ttf, zlib, libpng16 and
rem the whole static closure of codecs behind them, with .pc files whose prefix
rem is relocatable. That split is why a fresh kit is enough: the kit is the
rem compiler, the tree is the libraries.
rem
rem Setup and build live in one file because the build needs the kit first on
rem PATH *and* pkg-config pointed at that kit's tree, in Windows spelling
rem (C:/...): sh does not convert POSIX paths for a native pkg-config.exe, and
rem when that goes wrong pkg-config silently finds nothing and the build stops
rem with "pkg-config cannot find: SDL2_mixer ...". build.bat sets exactly the
rem variables it verified with, so the two cannot disagree.
rem
rem Downloads are cached in %WINKIT_CACHE% (default build\downloads,
rem git-ignored) and checked against a pinned SHA-256, so a re-run costs
rem nothing and a truncated or replaced download is refused. Nothing is written
rem outside the kit directories, that cache and build\. A non-empty
rem %W64DEVKIT_DIR%/%X86DEVKIT_DIR% is left alone unless /force is given.
rem ===========================================================================
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "ORIGINAL_PATH=%PATH%"

set "KIT64_FILE=w64devkit-x64-2.11.0.7z.exe"
set "KIT64_URL=https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x64-2.11.0.7z.exe"
set "KIT64_SHA256=54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b"
set "KIT86_FILE=w64devkit-x86-2.11.0.7z.exe"
set "KIT86_URL=https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x86-2.11.0.7z.exe"
set "KIT86_SHA256=f7e8c79eacc4cbdd718a4cbb6519f2f370311017dc08f7750abedb4340d1b65d"

set "SYS64_FILE=win64-sysroot.zip"
set "SYS64_URL=https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/win64-sysroot.zip"
set "SYS64_SHA256=156f6139e8c52f9e398fdb1beb6042f62f503d218d6b55bdc72b4b29558c291c"
set "SYS64_TOP=win64-sysroot"
set "SYS86_FILE=win32-sysroot.zip"
set "SYS86_URL=https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/win32-sysroot.zip"
set "SYS86_SHA256=7e13139cdf9be50ba828a6f54510c5fa0749f74c1a74fcd979d6746d58505603"
set "SYS86_TOP=win32-sysroot"

set "PACKAGES=sdl2 SDL2_mixer SDL2_ttf zlib libpng16"

if not defined W64DEVKIT_DIR set "W64DEVKIT_DIR=C:\w64devkit"
if not defined X86DEVKIT_DIR set "X86DEVKIT_DIR=C:\x86devkit"
if not defined WINKIT_CACHE set "WINKIT_CACHE=%ROOT%\build\downloads"
set "SYSR64=%ROOT%\build\sysroots\win64-sysroot"
set "SYSR86=%ROOT%\build\sysroots\win32-sysroot"
set "STAGE=%ROOT%\build\tmp\winkit-stage"

set "DO_BUILD32=0"
set "DO_BUILD64=0"
set "DO_FORCE=0"
set "DO_CHECK=0"
set "FAILED="

rem No arguments means the whole job: install what is missing and then build
rem both targets. That is what "/build" spelled out before the rename, and it
rem is what the arm64 script does with no arguments too.
if "%~1"=="" (
    set "DO_BUILD32=1"
    set "DO_BUILD64=1"
)

for %%a in (%*) do (
    if /i "%%~a"=="/build" (
        set "DO_BUILD32=1"
        set "DO_BUILD64=1"
    )
    if /i "%%~a"=="/build32" set "DO_BUILD32=1"
    if /i "%%~a"=="/build64" set "DO_BUILD64=1"
    if /i "%%~a"=="/force" set "DO_FORCE=1"
    if /i "%%~a"=="/check" set "DO_CHECK=1"
    if /i "%%~a"=="/help" goto :usage
    if /i "%%~a"=="-h" goto :usage
    if /i "%%~a"=="--help" goto :usage
)

echo.
echo mugen-unleashed - Windows build
echo   x64 kit  %W64DEVKIT_DIR%
echo   x86 kit  %X86DEVKIT_DIR%
echo   sysroots %SYSR64%
echo            %SYSR86%
echo   downloads %WINKIT_CACHE%
echo.

if not exist "%ROOT%\build\tmp" mkdir "%ROOT%\build\tmp" >nul 2>&1
if not exist "%ROOT%\build\sysroots" mkdir "%ROOT%\build\sysroots" >nul 2>&1
if not exist "%WINKIT_CACHE%" mkdir "%WINKIT_CACHE%" >nul 2>&1
if not exist "%WINKIT_CACHE%" (
    echo ERROR: cannot create %WINKIT_CACHE%
    goto :failed
)

if "%DO_CHECK%"=="0" (
    call :install_kit 64 "%KIT64_FILE%" "%KIT64_URL%" "%KIT64_SHA256%" "%W64DEVKIT_DIR%"
    if errorlevel 1 goto :failed
    call :install_kit 86 "%KIT86_FILE%" "%KIT86_URL%" "%KIT86_SHA256%" "%X86DEVKIT_DIR%"
    if errorlevel 1 goto :failed
    call :install_sysroot 64 "%SYS64_FILE%" "%SYS64_URL%" "%SYS64_SHA256%" "%SYS64_TOP%" "%SYSR64%"
    if errorlevel 1 goto :failed
    call :install_sysroot 86 "%SYS86_FILE%" "%SYS86_URL%" "%SYS86_SHA256%" "%SYS86_TOP%" "%SYSR86%"
    if errorlevel 1 goto :failed
)

call :verify win64 "%W64DEVKIT_DIR%" "%SYSR64%" "x86_64-w64-mingw32"
if errorlevel 1 set "FAILED=1"
call :verify win32 "%X86DEVKIT_DIR%" "%SYSR86%" "i686-w64-mingw32"
if errorlevel 1 set "FAILED=1"
if defined FAILED goto :failed

call :game_data
if errorlevel 1 set "FAILED=1"
if defined FAILED goto :failed

if "%DO_BUILD64%"=="1" (
    call :build win64
    if errorlevel 1 set "FAILED=1"
)
if "%DO_BUILD32%"=="1" (
    call :build win32
    if errorlevel 1 set "FAILED=1"
)
if defined FAILED goto :failed

set "BASH64=%W64DEVKIT_DIR:\=/%"
set "BASH86=%X86DEVKIT_DIR:\=/%"
echo.
echo Ready.
echo.
echo The Makefile finds build\sysroots\win64-sysroot (or win32-sysroot) by
echo itself, so a build needs nothing but the kit on PATH:
echo.
echo   export PATH="%BASH64%/bin:$PATH"      # win64
echo   make -j%%NUMBER_OF_PROCESSORS%%
echo.
echo   export PATH="%BASH86%/bin:$PATH"      # win32, the reference build
echo   make -j%%NUMBER_OF_PROCESSORS%%
echo.
echo The target comes from the compiler's own triple, so there is no variable
echo to set. Let this script do it instead:  build.bat
exit /b 0

rem ---------------------------------------------------------------- helpers --
:usage
echo.
echo build.bat - build mugen-unleashed for Windows (w64devkit 2.11.0)
echo.
echo   build.bat             install what is missing, then build win32 and win64
echo   build.bat /build      the same thing, spelled out
echo   build.bat /build32    ... only the reference build
echo   build.bat /build64    ... only the 64-bit build
echo   build.bat /check      only verify what is installed
echo   build.bat /force      re-download and re-extract even if installed
echo.
echo Overrides (environment variables):
echo   W64DEVKIT_DIR   where the x64 kit goes          (default C:\w64devkit)
echo   X86DEVKIT_DIR   where the x86 kit goes          (default C:\x86devkit)
echo   WINKIT_CACHE    download cache                  (default build\downloads)
echo.
echo The dependency trees land in build\sysroots\win64-sysroot and
echo build\sysroots\win32-sysroot, where the Makefile finds them by itself.
echo.
echo The kits come from github.com/skeeto/w64devkit, the dependency trees from
echo this project's toolchains-v1.0 release. Both are checked against a pinned
echo SHA-256. See docs/BUILD-MINGW.md.
exit /b 0

:failed
echo.
echo ERROR: setup did not complete - see the messages above.
exit /b 1

rem :sha256 <file>  ->  HASH  (empty when certutil could not read the file)
:sha256
set "HASH="
for /f "skip=1 tokens=* delims=" %%h in ('certutil -hashfile "%~1" SHA256 2^>nul') do (
    if not defined HASH (
        set "line=%%h"
        set "line=!line: =!"
        if not "!line!"=="" set "HASH=!line!"
    )
)
exit /b 0

rem :dir_empty <dir>  ->  IS_EMPTY=1 (missing or empty) / 0 (has entries)
:dir_empty
set "IS_EMPTY=1"
if not exist "%~1" exit /b 0
for /f "delims=" %%f in ('dir /b /a "%~1" 2^>nul') do set "IS_EMPTY=0"
exit /b 0

rem :set_env <kit>  - kit first on PATH, pkg-config pointed at its tree
rem The kit goes first on PATH; the tree is *not* exported any more. The
rem Makefile finds build\sysroots\<target>-sysroot on its own and points
rem pkg-config at it, so there is one place that decides how the tree is
rem addressed instead of two that can disagree.
:set_env
set "PATH=%~1\bin;%ORIGINAL_PATH%"
exit /b 0

rem :fetch <url> <dest> <sha256> <label>
:fetch
set "F_URL=%~1"
set "F_DEST=%~2"
set "F_WANT=%~3"
set "F_LABEL=%~4"
if exist "%F_DEST%" (
    call :sha256 "%F_DEST%"
    if /i "!HASH!"=="%F_WANT%" (
        echo   cached download verified
        exit /b 0
    )
    echo   cached file has the wrong hash, downloading again
    del /q "%F_DEST%" >nul 2>&1
)
echo   downloading %F_LABEL%
where curl.exe >nul 2>&1
if errorlevel 1 goto :fetch_ps
curl.exe --fail --location --silent --show-error --proto "=https" --retry 2 --connect-timeout 20 --max-time 3600 -o "%F_DEST%.part" "%F_URL%"
if errorlevel 1 goto :fetch_failed
goto :fetch_check
:fetch_ps
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'; Invoke-WebRequest -UseBasicParsing -Uri '%F_URL%' -OutFile '%F_DEST%.part'"
if errorlevel 1 goto :fetch_failed
:fetch_check
move /y "%F_DEST%.part" "%F_DEST%" >nul
call :sha256 "%F_DEST%"
if /i "!HASH!"=="%F_WANT%" (
    echo   sha256 verified
    exit /b 0
)
echo   ERROR: %F_LABEL% failed its SHA-256
echo          expected %F_WANT%
echo          got      !HASH!
del /q "%F_DEST%" >nul 2>&1
exit /b 1
:fetch_failed
if exist "%F_DEST%.part" del /q "%F_DEST%.part" >nul 2>&1
echo   ERROR: download failed: %F_URL%
exit /b 1

rem ------------------------------------------------------------- installers --
rem :install_kit <tag> <file> <url> <sha256> <destination>
:install_kit
set "TAG=%~1"
set "K_FILE=%~2"
set "K_URL=%~3"
set "K_WANT=%~4"
set "K_DEST=%~5"
echo.
echo [kit %TAG%] %K_FILE%  -^>  %K_DEST%

if "%DO_FORCE%"=="0" if exist "%K_DEST%\bin\gcc.exe" (
    echo   already installed, skipping ^(/force to redo^)
    exit /b 0
)
if "%DO_FORCE%"=="0" (
    call :dir_empty "%K_DEST%"
    if "!IS_EMPTY!"=="0" (
        echo   ERROR: %K_DEST% exists, is not empty, and holds no bin\gcc.exe.
        echo          Move it away, or pass /force to replace it.
        exit /b 1
    )
)

call :fetch "%K_URL%" "%WINKIT_CACHE%\%K_FILE%" "%K_WANT%" "%K_FILE%"
if errorlevel 1 exit /b 1

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" >nul 2>&1
if not exist "%STAGE%" (
    echo   ERROR: cannot create %STAGE%
    exit /b 1
)
echo   extracting - this takes a minute
rem The 7-Zip self-extractor is not reliably synchronous: started from a batch
rem file it can return before it has written anything, which used to fail the
rem check below on a fast machine and pass on a slow one. start /wait pins it,
rem -o says where, and the poll afterwards covers the case where it re-launches
rem itself instead of extracting in place.
pushd "%STAGE%"
start "" /wait "%WINKIT_CACHE%\%K_FILE%" -y -o"%STAGE%"
popd
rem Two conditions, and both are needed: the extractor process has to be gone,
rem and the tree has to be there. Waiting only for bin\gcc.exe raced a
rem still-running extraction once - gcc.exe appears early - and the move then
rem failed on a file that was still being written.
set "K_TRIES=0"
:kit_wait
tasklist /fi "imagename eq %K_FILE%" 2>nul | find /i "%K_FILE%" >nul
if not errorlevel 1 goto :kit_wait_more
if exist "%STAGE%\w64devkit\bin\gcc.exe" goto :kit_got
:kit_wait_more
set /a K_TRIES+=1
if !K_TRIES! geq 120 goto :kit_timeout
ping -n 2 127.0.0.1 >nul
goto :kit_wait
:kit_timeout
echo   ERROR: the archive did not produce w64devkit\bin\gcc.exe
echo          (whatever it did produce is left in %STAGE%)
exit /b 1
:kit_got
rem Port fix: cmd MOVE cannot move directories across volumes. CI stages on
rem D: but a local kit may live on C:, so copy the complete tree first and only
rem delete staging after success. Robocopy codes 0-7 are successful outcomes;
rem a gcc.exe sentinel alone would accept a partially copied compiler.
if exist "%K_DEST%" rmdir /s /q "%K_DEST%"
robocopy "%STAGE%\w64devkit" "%K_DEST%" /e /r:2 /w:1 /nfl /ndl /njh /njs /np
if errorlevel 8 (
    echo   ERROR: cannot copy the complete extracted kit to %K_DEST%
    echo          the extracted tree is left in %STAGE% for a retry.
    echo          Check destination permissions, free space and locked files.
    exit /b 1
)
if not exist "%K_DEST%\bin\gcc.exe" (
    echo   ERROR: the copied kit has no bin\gcc.exe at %K_DEST%
    echo          the extracted tree is left in %STAGE% for inspection.
    exit /b 1
)
rmdir /s /q "%STAGE%"
echo   installed
exit /b 0

rem :install_sysroot <tag> <file> <url> <sha256> <top-dir> <destination>
:install_sysroot
set "TAG=%~1"
set "S_FILE=%~2"
set "S_URL=%~3"
set "S_WANT=%~4"
set "S_TOP=%~5"
set "S_KIT=%~6"
echo.
echo [sysroot %TAG%] %S_FILE%  -^>  %S_KIT%

if "%DO_FORCE%"=="0" if exist "%S_KIT%\usr\lib\pkgconfig\sdl2.pc" (
    echo   already installed, skipping ^(/force to redo^)
    exit /b 0
)

call :fetch "%S_URL%" "%WINKIT_CACHE%\%S_FILE%" "%S_WANT%" "%S_FILE%"
if errorlevel 1 exit /b 1

if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%" >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; Expand-Archive -Force -LiteralPath '%WINKIT_CACHE%\%S_FILE%' -DestinationPath '%STAGE%'"
if errorlevel 1 (
    echo   ERROR: cannot unpack %S_FILE% ^(PowerShell Expand-Archive^)
    exit /b 1
)
if not exist "%STAGE%\%S_TOP%\usr\lib\pkgconfig\sdl2.pc" (
    echo   ERROR: %S_FILE% did not unpack a %S_TOP%\usr tree
    rmdir /s /q "%STAGE%"
    exit /b 1
)
if exist "%S_KIT%" rmdir /s /q "%S_KIT%"
mkdir "%S_KIT%" >nul 2>&1
xcopy /e /i /q /y "%STAGE%\%S_TOP%\*" "%S_KIT%\" >nul
if not exist "%S_KIT%\usr\lib\pkgconfig\sdl2.pc" (
    echo   ERROR: cannot copy the tree into %S_KIT%
    exit /b 1
)
rmdir /s /q "%STAGE%"
echo   installed
exit /b 0

rem ---------------------------------------------------------------- verify --
rem :set_pc <sysroot>  - the four variables pkg-config needs for that tree.
rem Only :verify uses this now: the build goes through the Makefile, which
rem points pkg-config at build\sysroots\<target>-sysroot itself.
:set_pc
set "PKG_CONFIG_PATH="
set "PKG_CONFIG_LIBDIR=%~1\usr\lib\pkgconfig"
set "PKG_CONFIG_SYSTEM_INCLUDE_PATH=%~1\usr\include"
set "PKG_CONFIG_SYSTEM_LIBRARY_PATH=%~1\usr\lib"
set "PKG_CONFIG_ALLOW_SYSTEM_CFLAGS=1"
set "PKG_CONFIG_ALLOW_SYSTEM_LIBS=1"
exit /b 0

rem :verify <name> <kit> <sysroot> <exact triple>
:verify
set "V_NAME=%~1"
set "V_KIT=%~2"
set "V_SYSROOT=%~3"
set "V_TRIPLE=%~4"
echo.
echo [verify %V_NAME%] %V_KIT%
if not exist "%V_KIT%\bin\gcc.exe" (
    echo   ERROR: no bin\gcc.exe - run build.bat without /check
    exit /b 1
)
call :set_env "%V_KIT%"
if not exist "%V_SYSROOT%\usr\lib\pkgconfig\sdl2.pc" (
    echo   ERROR: no dependency tree at %V_SYSROOT% - run build.bat without /check
    exit /b 1
)
call :set_pc "%V_SYSROOT%"
set "GOT="
for /f "usebackq tokens=*" %%t in (`gcc -dumpmachine`) do if not defined GOT set "GOT=%%t"
set "GCCVER="
for /f "usebackq tokens=*" %%v in (`gcc --version`) do if not defined GCCVER set "GCCVER=%%v"
echo   compiler  !GCCVER!
echo   triple    !GOT!
if /i not "!GOT!"=="%V_TRIPLE%" (
    echo   ERROR: expected a %V_TRIPLE% compiler, got "!GOT!"
    exit /b 1
)
where pkg-config.exe >nul 2>&1
if errorlevel 1 (
    echo   ERROR: no pkg-config.exe in %V_KIT%\bin
    exit /b 1
)
set "MISSING="
for %%p in (%PACKAGES%) do (
    pkg-config --exists %%p 2>nul
    if errorlevel 1 set "MISSING=!MISSING! %%p"
)
if not "!MISSING!"=="" (
    echo   ERROR: pkg-config cannot find:!MISSING!
    echo          expected in %V_SYSROOT% - run build.bat without /check
    exit /b 1
)
set "SDLVER="
for /f "usebackq tokens=*" %%v in (`pkg-config --modversion sdl2`) do set "SDLVER=%%v"
echo   packages  %PACKAGES%
echo   SDL2      !SDLVER!
echo   tree      %V_SYSROOT%
echo   ok
exit /b 0

rem --------------------------------------------------------------- building --
rem :build <name> - win32 or win64; the kit follows from the name
rem :game_data -> install\ holds a M.U.G.E.N tree (the default assets when the
rem clone has none). /check only reports; a failed download is a warning, not a
rem failed setup, because the engine still builds without game data.
:game_data
echo.
echo [game data] %ROOT%\install
if exist "%ROOT%\install\data\mugen.cfg" (
    echo   present
    exit /b 0
)
if "%DO_CHECK%"=="1" (
    echo   MISSING  run build.bat /build to fetch the default assets
    exit /b 0
)
if not exist "%ROOT%\tools\fetch_game_data.sh" (
    echo   WARNING: tools\fetch_game_data.sh is missing
    exit /b 0
)
"%W64DEVKIT_DIR%\bin\sh.exe" "%ROOT%\tools\fetch_game_data.sh"
if errorlevel 1 (
    echo   WARNING: could not fetch the default game data
    echo   the built binary will exit at start until install\ has a tree
    echo   (see tools/fetch_game_data.sh --help^)
    exit /b 0
)
exit /b 0

:build
set "B_NAME=%~1"
set "B_KIT=%W64DEVKIT_DIR%"
if /i "%B_NAME%"=="win32" set "B_KIT=%X86DEVKIT_DIR%"
echo.
echo [build %B_NAME%] make -j%NUMBER_OF_PROCESSORS%
call :set_env "%B_KIT%"
pushd "%ROOT%"
make -j%NUMBER_OF_PROCESSORS%
set "B_RC=%ERRORLEVEL%"
popd
if not "%B_RC%"=="0" (
    echo   ERROR: the %B_NAME% build failed
    exit /b 1
)
set "B_EXE=%ROOT%\build\mingw64-sdl2\mugen-mingw-win64-sdl2.exe"
if /i "%B_NAME%"=="win32" set "B_EXE=%ROOT%\build\mingw-sdl2\mugen-mingw-win32-sdl2.exe"
if not exist "%B_EXE%" (
    echo   ERROR: the build reported success but %B_EXE% is missing
    exit /b 1
)
for %%s in ("%B_EXE%") do echo   built  %%~nxs  %%~zs bytes
exit /b 0
