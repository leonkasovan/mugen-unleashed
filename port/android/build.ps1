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
    installed with BITS/Get-FileHash/bsdtar and sdkmanager; missing SDL2 and its
    companions are cross-compiled by tools\build_sdl2_companions.bat. Nothing
    is fetched with git. -CheckTools never downloads.

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

.PARAMETER Debug
    Debug APK (default release).

.PARAMETER NoAssets
    Package no game data.

.PARAMETER Model3d
    Opt-in glTF renderer; implies the GLES renderer (build.bat rewrites --model3d
    into --gles).

.PARAMETER VideoBg
    Enable WebM backgrounds from a prepared Android FFmpeg prefix.

.EXAMPLE
    .\build.ps1 -CheckTools

.EXAMPLE
    .\build.ps1 -Debug -NoAssets
#>
# Deliberately NOT an advanced script: [CmdletBinding()] or a [Parameter()]
# attribute would inject -Debug/-Verbose/-ErrorAction as common parameters,
# and -Debug is the name this build's flag has everywhere else (build.bat
# --debug). A plain param block also means anything unrecognised lands in
# $args, so the build.bat spelling (--model3d and the rest) passes straight
# through.
#
# Port fix: no -Gles switch. build.bat rejects --gles outright (this port always
# builds the GLES 3.0 renderer), so a switch declared here would forward a value
# the batch file then failed on. An unrecognised --gles still reaches build.bat
# through the $args pass-through and gets that error.
param(
    [switch]$Help,
    [switch]$CheckTools,
    [switch]$Setup,
    [switch]$DownloadTools,
    [switch]$Clean,
    [switch]$Debug,
    [switch]$NoAssets,
    [switch]$Model3d,
    [switch]$VideoBg
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

if ($Debug)         { $argv.Add('--debug') }
if ($NoAssets)      { $argv.Add('--no-assets') }
# PowerShell comments, not batch ones: this file is parsed by PowerShell, which
# does not know `rem` and aborts before build.bat is reached (with
# $ErrorActionPreference = 'Stop' the first one is a terminating error).
# --model3d implies --gles in build.bat, which is why no -Gles switch exists.
if ($Model3d)       { $argv.Add('--model3d') }
if ($VideoBg)       { $argv.Add('--video-bg') }
if ($args)          { $argv.AddRange([string[]]$args) }

Write-Host "==> $bat $($argv -join ' ')" -ForegroundColor DarkGray
& $bat @argv
exit $LASTEXITCODE
