<#
.SYNOPSIS
    Build mugen-unleashed for the PlayStation Vita -- PowerShell front end.

.DESCRIPTION
    The build itself is port\psvita\build.bat: one implementation, so the two
    entry points cannot drift apart. This script only gives PowerShell users
    native parameters and PowerShell's own conventions (Write-Host output,
    $LASTEXITCODE propagation, -Help instead of --help).

    Nothing here needs MSYS2, Git Bash or WSL, and no Python: VitaSDK's own
    cmake/ninja/vita-pack-vpk do the work, with Windows' curl, PowerShell's
    Get-FileHash and System32's bsdtar.

    Arguments not listed below are passed straight through, so the build.bat
    spelling works too:   .\build.ps1 --titleid MUGEN0001 --data install

.PARAMETER Help
    Print the full flag reference (build.bat's header) and exit.

.PARAMETER Setup
    Install VitaSDK and its packages, then stop.

.PARAMETER CheckTools
    Report what is installed; build nothing.

.PARAMETER Env
    Print the environment lines (VITASDK, PATH, CMAKE_TOOLCHAIN_FILE) and exit.

.PARAMETER Clean
    Remove this port's build output.

.PARAMETER Offline
    Fail instead of downloading anything.

.PARAMETER Jobs
    Parallel build jobs. Default: the CPU count.

.PARAMETER Debug
    Debug build (default Release).

.PARAMETER NoModel3d
    Compile 3D stage model support out (basic glTF profile is on by default).

.PARAMETER Data
    M.U.G.E.N tree to pack into the VPK.

.PARAMETER NoData
    Pack no game data (engine only).

.PARAMETER TitleId
    Nine-character title id, e.g. MUGEN0001.

.PARAMETER ShaderCompiler
    Bundle this libshacccg.suprx instead of the release asset.

.PARAMETER NoShaderCompiler
    Bundle no shader compiler.

.EXAMPLE
    .\build.ps1 -CheckTools

.EXAMPLE
    .\build.ps1 -Data install -Debug
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
    [switch]$Env,
    [switch]$Clean,
    [switch]$Offline,
    [int]$Jobs = 0,
    [switch]$Debug,
    [switch]$NoModel3d,
    [string]$Data,
    [switch]$NoData,
    [string]$TitleId,
    [string]$ShaderCompiler,
    [switch]$NoShaderCompiler
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Definition
$bat = Join-Path $here 'build.bat'

if (-not (Test-Path -LiteralPath $bat)) {
    Write-Error "error: $bat is missing; restore it from git"
    exit 1
}

$argv = New-Object System.Collections.Generic.List[string]
if ($Help)             { $argv.Add('--help') }
if ($Setup)            { $argv.Add('--setup') }
if ($CheckTools)       { $argv.Add('--check-tools') }
if ($Env)              { $argv.Add('--env') }
if ($Clean)            { $argv.Add('--clean') }
if ($Offline)          { $argv.Add('--offline') }
if ($Jobs -gt 0) { $argv.Add('--jobs'); $argv.Add("$Jobs") }
if ($Debug)            { $argv.Add('--debug') }
if ($NoModel3d)        { $argv.Add('--no-model3d') }
if ($Data)             { $argv.Add('--data'); $argv.Add($Data) }
if ($NoData)           { $argv.Add('--no-data') }
if ($TitleId)          { $argv.Add('--titleid'); $argv.Add($TitleId) }
if ($ShaderCompiler)   { $argv.Add('--shader-compiler'); $argv.Add($ShaderCompiler) }
if ($NoShaderCompiler) { $argv.Add('--no-shader-compiler') }
if ($args)             { $argv.AddRange([string[]]$args) }

Write-Host "==> $bat $($argv -join ' ')" -ForegroundColor DarkGray
& $bat @argv
exit $LASTEXITCODE
