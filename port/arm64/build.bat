@echo off
rem ===========================================================================
rem  build.bat -- build mugen-unleashed for Linux arm64 from Windows, in cmd.exe
rem
rem  Same job as the removed arm64 the MSYS2 build.sh that was removed, without MSYS2, Git Bash or WSL: the only
rem  shells used are the ones the w64devkit / x86devkit kits already carry
rem  (busybox sh as make's SHELL). Everything else is what Windows ships:
rem  curl.exe for the downloads, PowerShell for the checksums, and the
rem  System32 bsdtar for the archives (it reads .zip and .tar.xz alike).
rem
rem    port\arm64\build.bat                 download anything missing, then build
rem    port\arm64\build.bat --setup         download the toolchain, then stop
rem    port\arm64\build.bat --check-tools   report what is present, build nothing
rem    port\arm64\build.bat --sysroot       re-download the dependency tree
rem    port\arm64\build.bat --no-sysroot    use the existing tree as-is
rem    port\arm64\build.bat --clean         remove this port's build output
rem    port\arm64\build.bat --offline       fail instead of downloading anything
rem    port\arm64\build.bat --jobs N        parallel make jobs (default: CPU count)
rem    port\arm64\build.bat --gles N       renderer: 1 = GLES 3.0 (default
rem                                         for linux-arm64), 0 = desktop GL
rem    port\arm64\build.bat --model3d       3D stage models (default on)
rem    port\arm64\build.bat --no-model3d    turn model support off
rem    port\arm64\build.bat --debug         -O0 -g3 -DMUGEN_DEBUG, own tree
rem    port\arm64\build.bat --upload        build, then copy to DEVICE
rem    port\arm64\build.bat --device U@H --device-dir DIR --no-keep-old
rem
rem  Environment:
rem    ARM64_TOOLCHAIN          9.2 (default, glibc 2.30) or 15.3 (glibc 2.42)
rem    TOOLCHAIN_DIR            where the toolchain is installed
rem    DEVICE / DEVICE_DIR / DEVICE_BIN   the --upload target
rem    ARM64_SYSROOT_URL      re-published dependency tree
rem    ARM64_SYSROOT_SHA256   its SHA-256, when you publish your own
rem    MUGEN_WIN_KIT            kit to take make/sh/sed from (autodetected:
rem                             C:\w64devkit, C:\x86devkit, then PATH)
rem
rem  Output: build\linux-arm64-aarch64-none-linux-gnu-sdl2\
rem            mugen-linux-arm64-aarch64-none-linux-gnu
rem ===========================================================================

setlocal EnableExtensions EnableDelayedExpansion

rem The script's own directory, from anywhere, without a trailing backslash.
for %%I in ("%~dp0.") do set "PORTDIR=%%~fI"
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI"

set "OUT_NAME=build\linux-arm64-aarch64-none-linux-gnu-sdl2"
set "BIN=mugen-linux-arm64-aarch64-none-linux-gnu"
set "CROSS=aarch64-none-linux-gnu-"

rem --------------------------------------------------------------- defaults
set "DO_SETUP=0"
set "DO_UPLOAD=0"
set "KEEP_OLD=1"
set "CHECK_TOOLS=0"
set "DO_SYSROOT=0"
set "SKIP_SYSROOT=0"
set "CLEAN=0"
set "OFFLINE=0"
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"
set "GLES=1"
set "MODEL3D=1"
set "DEBUG_BUILD=0"
if not defined DEVICE set "DEVICE=ark@192.168.7.2"
if not defined DEVICE_DIR set "DEVICE_DIR=mugen"
if not defined DEVICE_BIN set "DEVICE_BIN=mugen-linux-arm64"

rem ------------------------------------------------------------------- args
:parse
if "%~1"=="" goto parsed
set "ARG=%~1"
if /I "!ARG!"=="--help"     goto usage
if /I "!ARG!"=="-h"         goto usage
rem --setup installs and then reports, like the MSYS2 build.sh that was removed: it does not go on to build.
if /I "!ARG!"=="--setup"    ( set "DO_SETUP=1" & set "CHECK_TOOLS=1" & shift & goto parse )
if /I "!ARG!"=="--check-tools" ( set "CHECK_TOOLS=1" & shift & goto parse )
if /I "!ARG!"=="--sysroot"  ( set "DO_SYSROOT=1" & shift & goto parse )
if /I "!ARG!"=="--no-sysroot" ( set "SKIP_SYSROOT=1" & shift & goto parse )
if /I "!ARG!"=="--clean"    ( set "CLEAN=1" & shift & goto parse )
if /I "!ARG!"=="--offline"  ( set "OFFLINE=1" & shift & goto parse )
if /I "!ARG!"=="--model3d"  ( set "MODEL3D=1" & shift & goto parse )
if /I "!ARG!"=="--no-model3d" ( set "MODEL3D=0" & shift & goto parse )
if /I "!ARG!"=="--debug"    ( set "DEBUG_BUILD=1" & shift & goto parse )
if /I "!ARG!"=="--upload"   ( set "DO_UPLOAD=1" & shift & goto parse )
if /I "!ARG!"=="--no-keep-old" ( set "KEEP_OLD=0" & shift & goto parse )
if /I "!ARG!"=="--jobs"     ( set "JOBS=%~2" & shift & shift & goto parse )
if /I "!ARG!"=="--gles"     ( set "GLES=%~2" & shift & shift & goto parse )
if /I "!ARG!"=="--device"   ( set "DEVICE=%~2" & shift & shift & goto parse )
if /I "!ARG!"=="--device-dir" ( set "DEVICE_DIR=%~2" & shift & shift & goto parse )
if /I "!ARG!"=="--device-bin" ( set "DEVICE_BIN=%~2" & shift & shift & goto parse )
echo error: unknown argument: %~1  (--help)
exit /b 2
:parsed

if not "%GLES%"=="0" if not "%GLES%"=="1" goto bad_gles
if not "%MODEL3D%"=="0" if not "%MODEL3D%"=="1" goto bad_model3d
echo %JOBS%| findstr /r /c:"^[1-9][0-9]*$" >nul || goto bad_jobs
if "%OFFLINE%"=="1" if "%DO_SETUP%"=="1" (
    echo error: --offline cannot be combined with --setup
    exit /b 2
)
if "%OFFLINE%"=="1" if "%DO_SYSROOT%"=="1" (
    echo error: --offline cannot be combined with --sysroot
    exit /b 2
)

rem --------------------------------------------------------------- toolchain
set "TOOLCHAIN_KIND=%ARM64_TOOLCHAIN%"
if "%TOOLCHAIN_KIND%"=="" set "TOOLCHAIN_KIND=9.2"

if "%TOOLCHAIN_KIND%"=="9.2"     goto tc_92
if "%TOOLCHAIN_KIND%"=="2019.12" goto tc_92
if "%TOOLCHAIN_KIND%"=="15.3"     goto tc_153
if "%TOOLCHAIN_KIND%"=="15.3.rel2" goto tc_153
echo error: unknown ARM64_TOOLCHAIN=%TOOLCHAIN_KIND% (use 9.2 or 15.3)
exit /b 1

:tc_92
rem The last release that ships glibc 2.30 -- what an RG351MP/ArkOS has. Arm
rem publishes MD5 for this series (a 32-hex .sha256asc beside the archive).
set "TOOLCHAIN_VER=9.2-2019.12"
set "TOOLCHAIN_TAG=9.2"
set "TOOLCHAIN_GLIBC=2.30"
set "TOOLCHAIN_FILE=gcc-arm-%TOOLCHAIN_VER%-mingw-w64-i686-aarch64-none-linux-gnu.tar.xz"
set "TOOLCHAIN_URL=https://developer.arm.com/-/media/Files/downloads/gnu-a/%TOOLCHAIN_VER%/binrel/%TOOLCHAIN_FILE%"
set "TOOLCHAIN_HASH_KIND=MD5"
set "TOOLCHAIN_HASH=a004a0ab54d7e46ae8bab60297dcda3d"
goto tc_done

:tc_153
set "TOOLCHAIN_VER=15.3.rel2"
set "TOOLCHAIN_TAG=15.3"
set "TOOLCHAIN_GLIBC=2.42"
set "TOOLCHAIN_FILE=arm-gnu-toolchain-%TOOLCHAIN_VER%-mingw-w64-x86_64-aarch64-none-linux-gnu.zip"
rem Note the encoded %2F: the same URL with a literal / in the project path is 404.
set "TOOLCHAIN_URL=https://gitlab.arm.com/api/v4/projects/tooling%%2Fgnu-toolchains-for-arm/packages/generic/gnu-toolchain/%TOOLCHAIN_VER%/%TOOLCHAIN_FILE%"
set "TOOLCHAIN_HASH_KIND=SHA256"
set "TOOLCHAIN_HASH=dd6bce7fef2724e510e1ee5880dea3dc037fd677a71c1fa094319b4d102850d2"
goto tc_done

:tc_done
rem One directory per version, so switching never has to destroy the other.
if not defined TOOLCHAIN_DIR set "TOOLCHAIN_DIR=C:\arm-gnu-toolchain-%TOOLCHAIN_TAG%"
set "CC_BIN=%TOOLCHAIN_DIR%\bin\%CROSS%gcc.exe"
set "XSYSROOT=%ROOT%\build\sysroots\arm64-sysroot"
set "DOWNLOADS=%ROOT%\build\downloads"
set "SCRATCH=%ROOT%\build\tmp"

set "SYSROOT_FILE=arm64-sysroot.zip"
set "SYSROOT_URL=https://github.com/leonkasovan/mugen-unleashed/releases/download/toolchains-v1.0/arm64-sysroot.zip"
set "SYSROOT_SHA256=16bbbf25b83b770f92ab478e330ea9eca0351c1f5792037ba958f02bece65049"
if defined ARM64_SYSROOT_URL set "SYSROOT_URL=%ARM64_SYSROOT_URL%"
if defined ARM64_SYSROOT_SHA256 set "SYSROOT_SHA256=%ARM64_SYSROOT_SHA256%"

if "%DEBUG_BUILD%"=="1" (
    set "OUT_NAME=build\linux-arm64-aarch64-none-linux-gnu-sdl2-debug"
    set "DEBUG_VALUE=1"
    set "BUILD_TYPE=Debug"
) else (
    set "DEBUG_VALUE=0"
    set "BUILD_TYPE=Release"
)
set "OUT=%ROOT%\%OUT_NAME%"

rem ------------------------------------------------------------------- kit
rem make, sh (busybox, used as make's SHELL), sed, grep, strings and md5sum all
rem come from a devkit. Either kit works for them; w64devkit is preferred
rem because its sh is the newer busybox. This is what keeps MSYS2 and Git Bash
rem out of the picture entirely.
set "KIT="
if defined MUGEN_WIN_KIT call :try_kit "%MUGEN_WIN_KIT%"
if not defined KIT call :try_kit "C:\w64devkit"
if not defined KIT call :try_kit "C:\x86devkit"
if not defined KIT for %%D in ("%PATH:;=" "%") do call :try_kit "%%~D"
if not defined KIT (
    echo error: no devkit found: need make.exe and sh.exe in the same bin directory.
    echo        Install w64devkit or x86devkit ^(build.bat does both^), or point
    echo        MUGEN_WIN_KIT at one, for example:
    echo          set MUGEN_WIN_KIT=C:\w64devkit
    exit /b 1
)
set "KITBIN=%KIT%\bin"
set "KITBIN_FWD=%KITBIN:\=/%"
set "PATH=%KITBIN%;%PATH%"

rem PowerShell's certutil-free hashing, System32 bsdtar for the archives and
rem Windows' curl for the downloads: all three ship with Windows 10 and later.
for %%T in ("%SystemRoot%\System32\tar.exe") do if not exist "%%~fT" (
    echo error: %%~fT is missing; the archives need Windows' bundled bsdtar.
    exit /b 1
)
where curl.exe >nul 2>&1 || (
    echo error: curl.exe not found; it ships with Windows 10 1803 and later.
    exit /b 1
)

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

if "%DO_SETUP%"=="1" (
    call :install_toolchain
    if errorlevel 1 goto fail
) else (
    call :check_toolchain
    if errorlevel 1 (
        if exist "%CC_BIN%" (
            "%CC_BIN%" -dumpmachine >nul 2>&1
            if errorlevel 1 (
                echo error: "%CC_BIN%" is installed but cannot run from this shell.
                echo        Nothing was downloaded; the existing toolchain was left
                echo        as it is. Check ARM64_TOOLCHAIN ^(ARM64_TOOLCHAIN=%ARM64_TOOLCHAIN%^).
                goto fail
            )
        )
        if "%OFFLINE%"=="1" (
            echo error: --offline: the toolchain is missing from "%TOOLCHAIN_DIR%".
            goto fail
        )
        call :install_toolchain
        if errorlevel 1 goto fail
    )
)

if "%CHECK_TOOLS%"=="1" goto report

if "%SKIP_SYSROOT%"=="1" goto sysroot_done
call :check_sysroot
if not errorlevel 1 if not "%DO_SYSROOT%"=="1" goto sysroot_done
if "%OFFLINE%"=="1" (
    echo error: --offline: the dependency tree is missing from "%XSYSROOT%".
    goto fail
)
call :install_sysroot
if errorlevel 1 goto fail
:sysroot_done

rem ---------------------------------------------------------------- account
echo.
echo ==^> Building mugen-unleashed for linux-arm64
echo     kit       %KIT%
echo     compiler  %CC_BIN%
echo     sysroot   %XSYSROOT%
echo     output    %OUT%
echo     jobs      %JOBS%
echo     gles      %GLES%
echo     model3d   %MODEL3D%
echo     build     %BUILD_TYPE%
echo.

set "PATH=%TOOLCHAIN_DIR%\bin;%PATH%"
set "XSYSROOT_FWD=%XSYSROOT:\=/%"

rem Everything the Makefile needs from a shell: the kit's busybox sh, named
rem explicitly because a native make would otherwise use cmd.exe for its
rem recipes, which are POSIX (mkdir -p, rm -f, printf, sed).
make.exe -C "%ROOT%" -j%JOBS% ^
    CROSS_COMPILE=%CROSS% ^
    XSYSROOT=%XSYSROOT_FWD% ^
    MUGEN_TARGET=linux-arm64 ^
    BUILD=%OUT_NAME:\=/% ^
    DEBUG=%DEBUG_VALUE% ^
    MUGEN_GLES=%GLES% ^
    MUGEN_MODEL3D=%MODEL3D% ^
    SHELL=%KITBIN_FWD%/sh.exe
if errorlevel 1 goto build_failed

if not exist "%OUT%\%BIN%" (
    echo error: the build reported success but "%OUT%\%BIN%" is missing.
    goto fail
)

echo.
echo ==^> Done
echo     %OUT%\%BIN%
echo.
echo     This is an arm64 Linux binary for the target device; it cannot run here.
echo     Copy it across with --upload, or by hand into the game directory.

if "%DO_UPLOAD%"=="1" (
    call :upload
    if errorlevel 1 goto fail
)
goto :eof

rem =================================================================== checks
rem :check_toolchain [verbose] -> errorlevel 1 when unusable
:check_toolchain
set "TC_RC=0"
set "TRIPLE="
if not exist "%CC_BIN%" (
    if "%~1"=="1" (
        echo   toolchain  MISSING  ^(%CC_BIN%^)
        echo              run: port\arm64\build.bat --setup
    )
    set "TC_RC=1"
    goto check_toolchain_end
)
for /f "usebackq delims=" %%T in (`"%CC_BIN%" -dumpmachine 2^>nul`) do set "TRIPLE=%%T"
echo !TRIPLE!| findstr /b /c:"aarch64-" >nul || (
    if "%~1"=="1" (
        echo   toolchain  WRONG    expected aarch64-, got !TRIPLE!
        echo              %TOOLCHAIN_DIR%
    )
    set "TC_RC=1"
    goto check_toolchain_end
)
if "%~1"=="1" (
    for /f "usebackq delims=" %%V in (`"%CC_BIN%" --version 2^>nul`) do if not defined TCV set "TCV=%%V"
    echo   toolchain  ok       !TCV!
    echo              %TOOLCHAIN_DIR% -^> !TRIPLE!
    set "TCV="
)
rem What glibc is actually installed, against the version this kit stands for.
set "INSTALLED="
if exist "%TOOLCHAIN_DIR%\aarch64-none-linux-gnu\libc\lib64\libc.so.6" (
    for /f "usebackq delims=" %%G in (`strings.exe "%TOOLCHAIN_DIR%\aarch64-none-linux-gnu\libc\lib64\libc.so.6" ^| sed.exe -n "s/.*release version \([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p"`) do if not defined INSTALLED set "INSTALLED=%%G"
)
if defined INSTALLED if not "!INSTALLED!"=="%TOOLCHAIN_GLIBC%" (
    if "%~1"=="1" (
        echo   glibc      WRONG    installed !INSTALLED!, ARM64_TOOLCHAIN=%TOOLCHAIN_KIND% wants %TOOLCHAIN_GLIBC%
        echo              run: port\arm64\build.bat --setup   ^(it replaces the tree^)
    )
    set "TC_RC=1"
    goto check_toolchain_end
)
if "%~1"=="1" (
    if defined INSTALLED (
        echo   glibc      ok       !INSTALLED! ^(target needs ^>= this^)
    ) else (
        echo   glibc      ok       %TOOLCHAIN_GLIBC% ^(target needs ^>= this^)
    )
)
:check_toolchain_end
exit /b %TC_RC%

rem :check_sysroot [verbose] -> errorlevel 1 when unusable
:check_sysroot
if exist "%XSYSROOT%\usr\lib\pkgconfig\sdl2.pc" (
    if "%~1"=="1" echo   sysroot    ok       %XSYSROOT%
    exit /b 0
)
if "%~1"=="1" (
    echo   sysroot    MISSING  ^(%XSYSROOT%^)
    echo              run: port\arm64\build.bat --sysroot
)
exit /b 1

:report
echo.
echo ==^> Tools
echo   kit        !KIT!
call :check_toolchain 1
set "RC=0"
if errorlevel 1 set "RC=1"
call :check_sysroot 1
if errorlevel 1 set "RC=1"
where pkg-config.exe >nul 2>&1 && (
    for /f "usebackq delims=" %%P in (`pkg-config.exe --version 2^>nul`) do echo   pkg-config ok       %%P
) || (
    echo   pkg-config MISSING  ^(the kit's pkg-config, or install one^)
    set "RC=1"
)
rem pkg-config is only consulted by the Makefile, so a missing one fails here,
rem not because the arm64 build needs it: the tree's .pc files are read through
rem PKG_CONFIG_LIBDIR, and the Makefile refuses to fall back to the host.
exit /b %RC%

rem ================================================================ downloads
rem :hash_file KIND FILE -> sets HASH
:hash_file
set "HASH="
for /f "usebackq delims=" %%H in (`powershell.exe -NoProfile -NonInteractive -Command "(Get-FileHash -LiteralPath '%~2' -Algorithm %~1).Hash" 2^>nul`) do if not defined HASH set "HASH=%%H"
if defined HASH exit /b 0
rem certutil prints the digest as the second line, sometimes with spaces.
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
    --connect-timeout 20 --max-time 1800 --output "%~2.part" "%~1"
if errorlevel 1 (
    if exist "%~2.part" del /q "%~2.part"
    echo error: download failed: %~1
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

:try_kit
if not "%~1"=="" if exist "%~1\bin\make.exe" if exist "%~1\bin\sh.exe" if not defined KIT set "KIT=%~1"
exit /b 0

rem ============================================================ install steps
:install_toolchain
echo.
echo ==^> Arm GNU Toolchain %TOOLCHAIN_VER% ^(glibc %TOOLCHAIN_GLIBC%, %TOOLCHAIN_HASH_KIND%^)
if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%"
set "ARCHIVE=%DOWNLOADS%\%TOOLCHAIN_FILE%"

if exist "%ARCHIVE%" (
    call :hash_file %TOOLCHAIN_HASH_KIND% "%ARCHIVE%"
    if /I "!HASH!"=="%TOOLCHAIN_HASH%" (
        echo     cached download verified ^(%TOOLCHAIN_HASH_KIND%^)
        goto tc_unpack
    )
    echo     cached download fails its %TOOLCHAIN_HASH_KIND%; fetching again
    del /q "%ARCHIVE%"
)
if "%OFFLINE%"=="1" (
    echo error: --offline: %ARCHIVE% is missing or fails its checksum
    exit /b 1
)
call :download "%TOOLCHAIN_URL%" "%ARCHIVE%"
if errorlevel 1 exit /b 1
call :hash_file %TOOLCHAIN_HASH_KIND% "%ARCHIVE%"
if /I not "!HASH!"=="%TOOLCHAIN_HASH%" (
    del /q "%ARCHIVE%"
    echo error: %TOOLCHAIN_FILE% failed its %TOOLCHAIN_HASH_KIND%; nothing was extracted
    echo        expected %TOOLCHAIN_HASH%
    echo        got      !HASH!
    exit /b 1
)
echo     %TOOLCHAIN_HASH_KIND% verified

:tc_unpack
set "STAGE=%SCRATCH%\stage-%TOOLCHAIN_VER%"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
echo     unpacking
call :extract "%ARCHIVE%" "%STAGE%"
if errorlevel 1 (
    if exist "%STAGE%" rmdir /s /q "%STAGE%"
    exit /b 1
)

rem The archives differ in shape: 15.3 unpacks its tree directly, 9.2 wraps it
rem in gcc-arm-<version>-.../ -- unwrap whatever single directory appeared.
set "TREE=%STAGE%"
if not exist "%TREE%\bin\%CROSS%gcc.exe" (
    set "TREE="
    for /d %%D in ("%STAGE%\*") do if not defined TREE set "TREE=%%~fD"
)
if not defined TREE (
    rmdir /s /q "%STAGE%"
    echo error: the archive did not produce %CROSS%gcc.exe
    exit /b 1
)
if not exist "!TREE!\bin\%CROSS%gcc.exe" (
    rmdir /s /q "%STAGE%"
    echo error: the archive did not produce bin\%CROSS%gcc.exe
    exit /b 1
)

rem Swap in beside the destination and keep the old tree until the new one is
rem in place -- the same discipline as the MSYS2 build.sh that was removed, so a dead download can never
rem leave a half-unpacked toolchain behind.
set "DEST=%TOOLCHAIN_DIR%.mugen-new"
set "BACKUP=%TOOLCHAIN_DIR%.mugen-old"
if exist "%DEST%" goto tc_txn
if exist "%BACKUP%" goto tc_txn
for %%I in ("%TOOLCHAIN_DIR%") do if not exist "%%~dpI" mkdir "%%~dpI"
move /y "!TREE!" "%DEST%" >nul || goto tc_stage_fail
if exist "%TOOLCHAIN_DIR%" (
    move /y "%TOOLCHAIN_DIR%" "%BACKUP%" >nul || goto tc_restore_fail
)
move /y "%DEST%" "%TOOLCHAIN_DIR%" >nul || goto tc_restore_fail
if exist "%BACKUP%" rmdir /s /q "%BACKUP%"
rmdir /s /q "%STAGE%" 2>nul
> "%TOOLCHAIN_DIR%\.mugen-toolchain-%TOOLCHAIN_VER%" echo %TOOLCHAIN_VER%
echo     installed into %TOOLCHAIN_DIR%
exit /b 0

:tc_txn
rmdir /s /q "%STAGE%" 2>nul
echo error: unfinished toolchain transaction at
echo        %DEST%
echo        or %BACKUP%
echo        The installed tree was kept on purpose; delete the leftover and re-run.
exit /b 1
:tc_stage_fail
rmdir /s /q "%STAGE%" 2>nul
echo error: cannot stage the toolchain beside %TOOLCHAIN_DIR%
exit /b 1
:tc_restore_fail
if not exist "%TOOLCHAIN_DIR%" if exist "%BACKUP%" move /y "%BACKUP%" "%TOOLCHAIN_DIR%" >nul
if exist "%DEST%" rmdir /s /q "%DEST%"
rmdir /s /q "%STAGE%" 2>nul
echo error: cannot install the replacement; the previous toolchain was restored
exit /b 1

:install_sysroot
echo.
echo ==^> Dependency tree ^(%SYSROOT_FILE%, sha256^)
if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%"
set "ARCHIVE=%DOWNLOADS%\%SYSROOT_FILE%"

if exist "%ARCHIVE%" (
    call :hash_file SHA256 "%ARCHIVE%"
    if /I "!HASH!"=="%SYSROOT_SHA256%" (
        echo     cached download verified ^(sha256^)
        goto sr_unpack
    )
)
if "%OFFLINE%"=="1" (
    echo error: --offline: %ARCHIVE% is missing or fails its checksum
    exit /b 1
)
if exist "%ARCHIVE%" del /q "%ARCHIVE%"
call :download "%SYSROOT_URL%" "%ARCHIVE%"
if errorlevel 1 exit /b 1
call :hash_file SHA256 "%ARCHIVE%"
if /I not "!HASH!"=="%SYSROOT_SHA256%" (
    del /q "%ARCHIVE%"
    echo error: %SYSROOT_FILE% failed its SHA-256; nothing was extracted
    echo        expected %SYSROOT_SHA256%
    echo        ^(a re-published asset needs ARM64_SYSROOT_SHA256^=^<new hash^>^)
    exit /b 1
)
echo     sha256 verified

:sr_unpack
set "STAGE=%SCRATCH%\stage-sysroot"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
echo     unpacking
call :extract "%ARCHIVE%" "%STAGE%"
if errorlevel 1 (
    if exist "%STAGE%" rmdir /s /q "%STAGE%"
    exit /b 1
)
set "TREE=%STAGE%"
if not exist "%TREE%\usr" (
    set "TREE="
    for /d %%D in ("%STAGE%\*") do if not defined TREE set "TREE=%%~fD"
)
if not defined TREE goto sr_layout
if not exist "!TREE!\usr\lib\pkgconfig\sdl2.pc" goto sr_layout

rem No absolute path may survive: the published asset bakes the prefix of the
rem machine that generated it, and unpacked anywhere else every -I and -L would
rem point at a directory that does not exist. ${pcfiledir} is the directory
rem holding the .pc file, so ../.. is the tree's usr/.
for %%P in ("!TREE!\usr\lib\pkgconfig\*.pc") do (
    sed.exe -i "s|^prefix=.*|prefix=${pcfiledir}/../..|" "%%~fP"
)

set "DEST=%XSYSROOT%.mugen-new"
set "BACKUP=%XSYSROOT%.mugen-old"
if exist "%DEST%" goto sr_txn
if exist "%BACKUP%" goto sr_txn
for %%I in ("%XSYSROOT%") do if not exist "%%~dpI" mkdir "%%~dpI"
move /y "!TREE!" "%DEST%" >nul || goto sr_stage_fail
if exist "%XSYSROOT%" (
    move /y "%XSYSROOT%" "%BACKUP%" >nul || goto sr_restore_fail
)
move /y "%DEST%" "%XSYSROOT%" >nul || goto sr_restore_fail
if exist "%BACKUP%" rmdir /s /q "%BACKUP%"
rmdir /s /q "%STAGE%" 2>nul
echo     installed into %XSYSROOT%
exit /b 0

:sr_layout
rmdir /s /q "%STAGE%" 2>nul
echo error: %SYSROOT_FILE% did not produce usr\lib\pkgconfig\sdl2.pc;
echo        the existing tree was kept
exit /b 1
:sr_txn
rmdir /s /q "%STAGE%" 2>nul
echo error: unfinished sysroot transaction at
echo        %DEST%
echo        or %BACKUP%
echo        The tree in place was kept on purpose; delete the leftover and re-run.
exit /b 1
:sr_stage_fail
rmdir /s /q "%STAGE%" 2>nul
echo error: cannot stage the tree beside %XSYSROOT%
exit /b 1
:sr_restore_fail
if not exist "%XSYSROOT%" if exist "%BACKUP%" move /y "%BACKUP%" "%XSYSROOT%" >nul
if exist "%DEST%" rmdir /s /q "%DEST%"
rmdir /s /q "%STAGE%" 2>nul
echo error: cannot install the dependency tree; the previous one was restored
exit /b 1

rem ==================================================================== upload
rem Transport is the ssh exec channel with the binary on stdin, not scp or sftp:
rem the device's sshd points its sftp subsystem at a dropbear path that does not
rem exist, so those fail against it while plain ssh works. The file is staged
rem under a temporary name and moved into place only after the checksum matches,
rem and the previous binary is kept as .prev.
rem
rem SSHOPTS keep the host key out of the user profile and hide OpenSSH's
rem post-quantum advisory. DEVICE_PASS is not honoured here -- Windows has no
rem sshpass; use a key, or type the password when ssh asks for it.
:upload
echo.
echo ==^> Uploading to %DEVICE%:%DEVICE_DIR%/
where ssh.exe >nul 2>&1 || (
    echo error: ssh.exe not found
    exit /b 1
)
set "SSHOPTS=-o StrictHostKeyChecking=no -o UserKnownHostsFile=%SCRATCH%\arm64-ssh-known-hosts -o ConnectTimeout=15 -o LogLevel=ERROR"
if not exist "%SCRATCH%" mkdir "%SCRATCH%"
set "NEWNAME=.%DEVICE_BIN%.new"

ssh.exe %SSHOPTS% "%DEVICE%" "mkdir -p '%DEVICE_DIR%'" || (
    echo error: cannot log in to %DEVICE% -- check DEVICE and your key
    exit /b 1
)
echo     sending %BIN%
ssh.exe %SSHOPTS% "%DEVICE%" "cd '%DEVICE_DIR%' && cat > '%NEWNAME%'" < "%OUT%\%BIN%" || (
    echo error: transfer failed
    exit /b 1
)
for /f "usebackq delims=" %%M in (`md5sum.exe "%OUT%\%BIN%"`) do set "LOCAL_MD5=%%M"
set "LOCAL_MD5=!LOCAL_MD5: =!"
set "LOCAL_MD5=!LOCAL_MD5:~0,32!"
set "REMOTE_MD5="
for /f "usebackq delims=" %%M in (`ssh.exe %SSHOPTS% "%DEVICE%" "md5sum '%DEVICE_DIR%/%NEWNAME%'" 2^>nul`) do if not defined REMOTE_MD5 set "REMOTE_MD5=%%M"
set "REMOTE_MD5=!REMOTE_MD5: =!"
set "REMOTE_MD5=!REMOTE_MD5:~0,32!"
if /I not "!REMOTE_MD5!"=="!LOCAL_MD5!" (
    ssh.exe %SSHOPTS% "%DEVICE%" "rm -f '%DEVICE_DIR%/%NEWNAME%'" 2>nul
    echo error: checksum mismatch after upload ^(local !LOCAL_MD5!, remote !REMOTE_MD5!^);
    echo        nothing was replaced
    exit /b 1
)
echo     checksum verified ^(!LOCAL_MD5!^)
set "MV=chmod +x '%NEWNAME%' && mv -f '%NEWNAME%' '%DEVICE_BIN%'"
if "%KEEP_OLD%"=="1" set "MV=chmod +x '%NEWNAME%' && if [ -f '%DEVICE_BIN%' ]; then mv -f '%DEVICE_BIN%' '%DEVICE_BIN%.prev'; fi && mv -f '%NEWNAME%' '%DEVICE_BIN%'"
ssh.exe %SSHOPTS% "%DEVICE%" "cd '%DEVICE_DIR%' && %MV%" || (
    echo error: could not move the new binary into place
    exit /b 1
)
echo     installed as %DEVICE_DIR%/%DEVICE_BIN%
exit /b 0

rem ==================================================================== errors
:bad_gles
echo error: --gles must be 0 or 1, not '%GLES%'
exit /b 2
:bad_model3d
echo error: --model3d must be 0 or 1, not '%MODEL3D%'
exit /b 2
:bad_jobs
echo error: --jobs must be a positive integer, not '%JOBS%'
exit /b 2
:build_failed
echo.
echo error: the build failed; see the compiler output above
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
