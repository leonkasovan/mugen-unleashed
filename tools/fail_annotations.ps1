<#
.SYNOPSIS
    Re-emit the interesting lines of a captured build log as GitHub annotations.

.DESCRIPTION
    A port's build log is the only place its failure is explained, and the step
    log needs a login to read. An annotation does not, so on failure the port
    jobs pipe their captured output through this script.

    GitHub keeps at most ten annotations per step, and it keeps the *first*
    ones -- so emitting the tail of the log is wrong: the failure is at the end
    and the ten earliest lines are progress chatter. This filters for the lines
    that explain a failure, takes the last few of them, and falls back to the
    tail when nothing matches.

.PARAMETER Log
    The captured log.

.PARAMETER Count
    How many lines to emit (default 10, the per-step annotation limit).

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\fail_annotations.ps1 `
        -Log build\tmp\port-psvita.log
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Log,
    [int]$Count = 10
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Log)) {
    Write-Host ('::error::no build log at {0}' -f $Log)
    exit 0
}

$lines = @(Get-Content -LiteralPath $Log)
if ($lines.Count -eq 0) {
    Write-Host ('::error::build log {0} is empty' -f $Log)
    exit 0
}

# The patterns that actually name a cause. Progress output ("Unzipping... 45%",
# "downloading ... (attempt 1 of 3)") is deliberately not matched.
$patterns = @(
    'error', 'denied', 'failed', 'failure', 'cannot', 'can not', 'not found',
    'missing', 'no such file', 'unable', 'invalid', 'unrecognized', 'abort'
)

$hits = @($lines | Select-String -Pattern ($patterns -join '|') -CaseSensitive:$false |
    ForEach-Object { $_.Line } | Select-Object -Last $Count)

if ($hits.Count -eq 0) {
    $hits = @($lines | Select-Object -Last $Count)
}

foreach ($line in $hits) {
    $text = $line.TrimEnd()
    if ($text.Length -gt 1000) { $text = $text.Substring(0, 1000) }
    if ($text.Length -eq 0) { continue }
    Write-Host ('::error::{0}' -f $text)
}
