<#
.SYNOPSIS
    Build mugen-unleashed for Linux arm64 -- PowerShell front end.

.DESCRIPTION
    The build itself is port\arm64\build.bat: one implementation, so the two
    entry points cannot drift apart. This script only gives PowerShell users
    native parameters and PowerShell's own conventions (Write-Host output,
    $LASTEXITCODE propagation, -Help instead of --help).

    Nothing here needs MSYS2, Git Bash or WSL. See build.bat for what the build
    does: it drives Arm's Mingw-w64 toolchain with Windows' curl, PowerShell's
    Get-FileHash and System32's bsdtar.

    Arguments not listed below are passed straight through, so the build.bat
    spelling works too:   .\build.ps1 --gles 0 --upload

.PARAMETER Help
    Print the full flag reference (build.bat's header) and exit.

.PARAMETER Setup
    Download and install the toolchain, then stop.

.PARAMETER CheckTools
    Report what is installed; build nothing.

.PARAMETER Clean
    Remove this port's build output.

.PARAMETER Offline
    Fail instead of downloading anything.

.PARAMETER Sysroot
    Re-download the dependency tree.

.PARAMETER NoSysroot
    Use the existing dependency tree as it is.

.PARAMETER Jobs
    Parallel make jobs. Default: the CPU count.

.PARAMETER Gles
    Renderer: 1 (default) = GLES 3.0, 0 = desktop GL.

.PARAMETER NoModel3d
    Compile 3D stage model support out (it is on by default).

.PARAMETER Debug
    -O0 -g3 -DMUGEN_DEBUG into a separate tree.

.PARAMETER Upload
    Build, then copy the binary to the device.

.PARAMETER Device
    Device for -Upload, e.g. ark@192.168.7.2.

.PARAMETER DeviceDir
    Target folder on the device (default mugen).

.EXAMPLE
    .\build.ps1 -CheckTools

.EXAMPLE
    .\build.ps1 -Jobs 8 -Debug

.EXAMPLE
    .\build.ps1 -Upload -Device ark@192.168.7.2
#>
# Deliberately NOT an advanced script: [CmdletBinding()] or a [Parameter()]
# attribute would inject -Debug/-Verbose/-ErrorAction as common parameters,
# and -Debug is the name this build's flag has everywhere else (build.bat
# --debug). A plain param block also means anything unrecognised lands in
# $args, so the build.bat spelling (--gles 0) passes straight through.
param(
    [switch]$Help,
    [switch]$Setup,
    [switch]$CheckTools,
    [switch]$Clean,
    [switch]$Offline,
    [switch]$Sysroot,
    [switch]$NoSysroot,
    [int]$Jobs = 0,
    [ValidateSet('0', '1')][string]$Gles,
    [switch]$NoModel3d,
    [switch]$Debug,
    [switch]$Upload,
    [string]$Device,
    [string]$DeviceDir
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition
$bat = Join-Path $here 'build.bat'

if (-not (Test-Path -LiteralPath $bat)) {
    Write-Error "error: $bat is missing; restore it from git"
    exit 1
}

$argv = New-Object System.Collections.Generic.List[string]
if ($Help)       { $argv.Add('--help') }
if ($Setup)      { $argv.Add('--setup') }
if ($CheckTools) { $argv.Add('--check-tools') }
if ($Clean)      { $argv.Add('--clean') }
if ($Offline)    { $argv.Add('--offline') }
if ($Sysroot)    { $argv.Add('--sysroot') }
if ($NoSysroot)  { $argv.Add('--no-sysroot') }
if ($Jobs -gt 0) { $argv.Add('--jobs'); $argv.Add("$Jobs") }
if ($Gles)       { $argv.Add('--gles'); $argv.Add($Gles) }
if ($NoModel3d)  { $argv.Add('--no-model3d') }
if ($Debug)      { $argv.Add('--debug') }
if ($Upload)     { $argv.Add('--upload') }
if ($Device)     { $argv.Add('--device'); $argv.Add($Device) }
if ($DeviceDir)  { $argv.Add('--device-dir'); $argv.Add($DeviceDir) }
if ($args)       { $argv.AddRange([string[]]$args) }

Write-Host "==> $bat $($argv -join ' ')" -ForegroundColor DarkGray
& $bat @argv
exit $LASTEXITCODE
