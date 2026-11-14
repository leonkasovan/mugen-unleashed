<#
.SYNOPSIS
    Build the Android arm64-v8a APK -- PowerShell front end.

.DESCRIPTION
    The build itself is port\android\build.bat: one implementation, so the two
    entry points cannot drift apart. This script only gives PowerShell users
    native parameters and PowerShell's own conventions (Write-Host output,
    $LASTEXITCODE propagation, -Help instead of --help).

    Nothing here needs MSYS2, Git Bash, WSL or Python: the JDK, the SDK's own
    sdkmanager.bat and CMake, and gradlew.bat are all native. Missing tools are
    installed with curl/Get-FileHash/bsdtar and sdkmanager; missing SDL2 and its
    companions are cross-compiled by tools\build_sdl2_companions.bat. Nothing
    is fetched with git. -CheckTools and -Offline never download.

    Arguments not listed below are passed straight through, so the build.bat
    spelling works too:   .\build.ps1 --debug --no-assets

.PARAMETER Help
    Print the full flag reference (build.bat's header) and exit.

.PARAMETER CheckTools
    Validate the tools; build nothing.

.PARAMETER Setup
    Install missing tools and native libraries, then stop before Gradle.

.PARAMETER DownloadTools
    Install missing tools and native libraries, then build the APK.

.PARAMETER Clean
    Clean the generated app build and staging directories.

.PARAMETER Offline
    Use only cached Gradle/dependencies.

.PARAMETER Debug
    Debug APK (default release).

.PARAMETER NoAssets
    Package no game data.

.PARAMETER Gles
    Build the GLES 3.0 renderer (M8).

.PARAMETER Model3d
    Opt-in glTF renderer; implies -Gles.

.EXAMPLE
    .\build.ps1 -CheckTools

.EXAMPLE
    .\build.ps1 -Debug -NoAssets
#>
# Deliberately NOT an advanced script: [CmdletBinding()] or a [Parameter()]
# attribute would inject -Debug/-Verbose/-ErrorAction as common parameters,
# and -Debug is the name this build's flag has everywhere else (build.bat
# --debug). A plain param block also means anything unrecognised lands in
# $args, so the build.bat spelling (--gles 0) passes straight through.
param(
    [switch]$Help,
    [switch]$CheckTools,
    [switch]$Setup,
    [switch]$DownloadTools,
    [switch]$Clean,
    [switch]$Offline,
    [switch]$Debug,
    [switch]$NoAssets,
    [switch]$Gles,
    [switch]$Model3d
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition
$bat = Join-Path $here 'build.bat'

if (-not (Test-Path -LiteralPath $bat)) {
    Write-Error "error: $bat is missing; restore it from git"
    exit 1
}

$argv = New-Object System.Collections.Generic.List[string]
if ($Help)          { $argv.Add('--help') }
if ($CheckTools)    { $argv.Add('--check-tools') }
if ($Setup)         { $argv.Add('--setup') }
if ($DownloadTools) { $argv.Add('--download-tools') }
if ($Clean)         { $argv.Add('--clean') }
if ($Offline)       { $argv.Add('--offline') }
if ($Debug)         { $argv.Add('--debug') }
if ($NoAssets)      { $argv.Add('--no-assets') }
if ($Gles)          { $argv.Add('--gles') }
if ($Model3d)       { $argv.Add('--model3d') }
if ($args)          { $argv.AddRange([string[]]$args) }

Write-Host "==> $bat $($argv -join ' ')" -ForegroundColor DarkGray
& $bat @argv
exit $LASTEXITCODE
