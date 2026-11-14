<#
.SYNOPSIS
    Cross-compile SDL2_ttf and SDL2_mixer for Android -- PowerShell front end.

.DESCRIPTION
    The build itself is build_sdl2_companions.bat: one implementation, so the
    two entry points cannot drift apart. This script only maps PowerShell
    parameters onto the environment variables that .bat reads and then runs it,
    which is how port\android\build.ps1 wraps build.bat.

    Nothing here needs MSYS2, Git Bash, WSL or Python. Downloads are curl.exe's,
    archives are unpacked by System32's bsdtar, and CMake/Ninja come from the
    Android SDK's pinned cmake package.

.PARAMETER Help
    Print the full reference (the .bat header) and exit.

.PARAMETER Abi
    ANDROID_ABI; default arm64-v8a.

.PARAMETER Platform
    ANDROID_PLATFORM; default android-21.

.PARAMETER Sdl2DepsDir
    The SDL2 prefix to build against; default build\android\sdl2\<abi>. When it
    does not exist, SDL2 itself is downloaded (curl, never git) and built there
    first.

.PARAMETER SdkRoot
    ANDROID_SDK_ROOT override.

.PARAMETER NdkHome
    ANDROID_NDK_HOME override; must match the revision app\build.gradle pins.

.PARAMETER Sdl2Version
    SDL2 release to build when the prefix is absent; default 2.32.10.

.PARAMETER TtfVersion
    SDL2_ttf release tag; default release-2.20.2.

.PARAMETER MixerVersion
    SDL2_mixer release tag; default release-2.6.3.

.PARAMETER FreetypeVersion
    FreeType version vendored into SDL2_ttf; default 2.12.1.

.EXAMPLE
    .\build_sdl2_companions.ps1 -Sdl2DepsDir C:\build\sdl2-arm64

.EXAMPLE
    .\build_sdl2_companions.ps1 -Abi arm64-v8a -Platform android-21
#>
# Deliberately NOT an advanced script: [CmdletBinding()] or a [Parameter()]
# attribute would inject -Debug/-Verbose/-ErrorAction as common parameters, and
# anything unrecognised lands in $args, which is passed straight through.
param(
    [switch]$Help,
    [string]$Abi,
    [string]$Platform,
    [string]$Sdl2DepsDir,
    [string]$SdkRoot,
    [string]$NdkHome,
    [string]$Sdl2Version,
    [string]$TtfVersion,
    [string]$MixerVersion,
    [string]$FreetypeVersion
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition
$bat = Join-Path $here 'build_sdl2_companions.bat'

if (-not (Test-Path -LiteralPath $bat)) {
    Write-Error "error: $bat is missing; restore it from git"
    exit 1
}

# The .bat reads the environment, so the parameters are simply exported into
# this process; child processes inherit them. Explicitly empty values are not
# exported, so each keeps its own default.
if ($Abi)              { $env:ANDROID_ABI = $Abi }
if ($Platform)         { $env:ANDROID_PLATFORM = $Platform }
if ($Sdl2DepsDir)      { $env:SDL2_DEPS_DIR = $Sdl2DepsDir }
if ($SdkRoot)          { $env:ANDROID_SDK_ROOT = $SdkRoot }
if ($NdkHome)          { $env:ANDROID_NDK_HOME = $NdkHome }
if ($Sdl2Version)      { $env:SDL2_VER = $Sdl2Version }
if ($TtfVersion)       { $env:SDL2TTF_VER = $TtfVersion }
if ($MixerVersion)     { $env:SDL2MIXER_VER = $MixerVersion }
if ($FreetypeVersion)  { $env:FREETYPE_VER = $FreetypeVersion }

$argv = New-Object System.Collections.Generic.List[string]
if ($Help) { $argv.Add('--help') }
if ($args) { $argv.AddRange([string[]]$args) }

Write-Host "==> $bat $($argv -join ' ')" -ForegroundColor DarkGray
& $bat @argv
exit $LASTEXITCODE
