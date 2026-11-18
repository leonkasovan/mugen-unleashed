<#
.SYNOPSIS
    Download a file with BITS, Windows' own transfer service, showing progress.

.DESCRIPTION
    Used by the port build scripts instead of curl.exe. BITS is part of
    Windows: it follows HTTP redirects, uses the system proxy, and retries a
    transfer from where it stopped when the connection drops. The job is
    given a stable display name, so a run that is interrupted leaves the job
    in the BITS queue and the next run resumes it instead of starting over.

    BITS is not always usable. Hosted CI runners ship it disabled, and
    Start-BitsTransfer then fails immediately with "the service cannot be
    started" -- which is how every port's first download died on
    windows-2022, nine seconds in, before anything was fetched. When BITS
    cannot be reached this script falls back to curl.exe, which every Windows
    since 1803 carries. The caller verifies a pinned SHA-256 afterwards either
    way, so a truncated transfer cannot pass.

    Set MUGEN_NO_BITS=1 to force the curl path (that is also how the fallback
    is tested on a machine where BITS works).

    Progress is printed as a percentage line every couple of seconds; a
    transfer that makes no progress for -StallSeconds is abandoned but its
    job is kept, so rerunning the build resumes it.

.PARAMETER Url
    Source URL. BITS supports HTTP and HTTPS.

.PARAMETER Destination
    Full path of the file to write. The parent directory must exist.

.PARAMETER Attempts
    Attempts after a hard failure (a BITS job that ends in Error/Cancelled
    or a stalled transfer). Transient network errors are retried by BITS
    itself and do not consume an attempt.

.PARAMETER StallSeconds
    How long a transfer may make no progress before the run gives up.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\download.ps1 `
        -Url https://example.invalid/x.zip -Destination C:\cache\x.zip
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Url,
    [Parameter(Mandatory = $true)][string]$Destination,
    [int]$Attempts = 3,
    [int]$StallSeconds = 600
)

$ErrorActionPreference = 'Stop'
$Destination = [IO.Path]::GetFullPath($Destination)

$leaf = Split-Path -Leaf $Destination
$jobName = 'mugen-' + $leaf

function Format-MB([double]$bytes) {
    return ($bytes / 1MB).ToString('N1', [Globalization.CultureInfo]::InvariantCulture) + ' MB'
}

# True when a BITS transfer can actually be started. A disabled service makes
# Get-BitsTransfer throw; a stopped-but-startable one succeeds and starts it.
#
# The messages go to Write-Host, not Write-Output: everything a function writes
# to the output stream joins its return value, so a Write-Output here turned the
# result into @('...', $false) -- an array is truthy, -not made it false, and the
# fallback was skipped even though BITS was unusable.
function Test-BitsAvailable {
    if ($env:MUGEN_NO_BITS -eq '1') {
        Write-Host '    BITS disabled by MUGEN_NO_BITS=1'
        return $false
    }
    try {
        Get-BitsTransfer -ErrorAction Stop | Out-Null
        return $true
    } catch {
        Write-Host ('    BITS unavailable: {0}' -f $_.Exception.Message)
        return $false
    }
}

# curl.exe ships with Windows (System32, since 1803). --retry covers the
# transient failures BITS would have absorbed; there is no resume here because
# a stale partial is a worse failure mode than re-fetching, and every caller
# checks a pinned hash anyway.
function Invoke-CurlDownload {
    param([string]$Url, [string]$Destination)

    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    if (-not (Test-Path -LiteralPath $curl)) { $curl = 'curl.exe' }
    $partial = $Destination + '.part'
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }

    Write-Output ('    downloading {0} (curl.exe)' -f (Split-Path -Leaf $Destination))
    Write-Output ('    {0}' -f $Url)
    & $curl --location --fail --retry 5 --retry-delay 5 --retry-connrefused `
        --output $partial $Url
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
        throw ('curl.exe exited {0}' -f $LASTEXITCODE)
    }
    if (-not (Test-Path -LiteralPath $partial)) { throw 'curl.exe reported success but wrote no file' }
    Move-Item -LiteralPath $partial -Destination $Destination -Force
    Write-Output ('    downloaded {0}' -f (Format-MB ([double](Get-Item -LiteralPath $Destination).Length)))
}

# A job left in the queue by an earlier run is reused when it belongs to this
# URL. Jobs in a terminal error state, or for a different URL, are discarded.
function Find-ReusableJob {
    param([string]$Name, [string]$Source)

    $job = Get-BitsTransfer -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $job) { return $null }
    if ($job.Description -ne $Source) {
        Remove-BitsTransfer -BitsJob $job -ErrorAction SilentlyContinue
        return $null
    }
    if ($job.JobState -eq 'Transferred') { return $job }
    if (@('Connecting', 'Queued', 'Transferring', 'TransientError', 'Suspended') -contains $job.JobState) {
        return $job
    }
    Remove-BitsTransfer -BitsJob $job -ErrorAction SilentlyContinue
    return $null
}

# Waits for a job, printing progress. Returns 'done' or 'stalled'; any
# terminal error is thrown.
function Wait-BitsJob {
    param([object]$Job, [int]$StallSeconds)

    $lastBytes = -1.0
    $lastChange = Get-Date
    $connectingSince = $null
    while ($true) {
        $state = $Job.JobState
        if ($state -eq 'Transferred') { return 'done' }
        if ($state -eq 'Error' -or $state -eq 'Cancelled') {
            throw ('BITS job ended as {0}: {1}' -f $state, $Job.ErrorDescription)
        }
        if ($state -eq 'Connecting') {
            if ($null -eq $connectingSince) { $connectingSince = Get-Date }
            if (((Get-Date) - $connectingSince).TotalSeconds -ge 60) { return 'stalled' }
        } else {
            $connectingSince = $null
        }
        if ($state -eq 'Suspended') {
            Resume-BitsTransfer -BitsJob $Job | Out-Null
        } else {
            $done = [double]$Job.BytesTransferred
            $total = [double]$Job.BytesTotal
            if ($done -ne $lastBytes) {
                $lastBytes = $done
                $lastChange = Get-Date
            } elseif (((Get-Date) - $lastChange).TotalSeconds -ge $StallSeconds) {
                return 'stalled'
            }
            if ($state -eq 'Transferring' -and $total -gt 0) {
                $pct = [int](100 * $done / $total)
                Write-Host ('    {0,3}%  {1} of {2}' -f $pct, (Format-MB $done), (Format-MB $total))
            }
        }
        Start-Sleep -Seconds 2
        $next = Get-BitsTransfer -JobId $Job.JobId -ErrorAction SilentlyContinue
        if ($null -eq $next) { throw 'the BITS job disappeared from the queue' }
        $Job = $next
    }
}

if (-not (Test-BitsAvailable)) {
    for ($try = 1; $try -le $Attempts; $try++) {
        try {
            Invoke-CurlDownload -Url $Url -Destination $Destination
            exit 0
        } catch {
            Write-Output ('    attempt {0} failed: {1}' -f $try, $_.Exception.Message)
        }
        if ($try -lt $Attempts) { Start-Sleep -Seconds 3 }
    }
    Write-Output ('error: download failed after {0} attempts: {1}' -f $Attempts, $Url)
    exit 1
}

for ($try = 1; $try -le $Attempts; $try++) {
    Write-Output ('    downloading {0} (attempt {1} of {2}, BITS)' -f $leaf, $try, $Attempts)
    Write-Output ('    {0}' -f $Url)
    $job = $null
    try {
        $job = Find-ReusableJob -Name $jobName -Source $Url
        if ($null -ne $job) {
            Write-Output '    resuming the queued BITS transfer'
        } else {
            $job = Start-BitsTransfer -Source $Url -Destination $Destination -Asynchronous `
                -DisplayName $jobName -Description $Url
        }
        $result = Wait-BitsJob -Job $job -StallSeconds $StallSeconds
        if ($result -eq 'stalled') {
            $current = Get-BitsTransfer -JobId $job.JobId -ErrorAction SilentlyContinue
            if ($null -ne $current -and [double]$current.BytesTransferred -gt 0) {
                Write-Output ('error: transfer stalled: {0}' -f $Url)
                Write-Output '       the BITS job was kept; rerun the build to resume it'
            } else {
                if ($null -ne $current) { Remove-BitsTransfer -BitsJob $current -ErrorAction SilentlyContinue }
                Write-Output ('error: could not transfer {0}' -f $Url)
            }
            exit 1
        }
        Complete-BitsTransfer -BitsJob $job
        Write-Output ('    downloaded {0}' -f (Format-MB ([double](Get-Item -LiteralPath $Destination).Length)))
        exit 0
    } catch {
        Write-Output ('    attempt {0} failed: {1}' -f $try, $_.Exception.Message)
        if ($null -ne $job) {
            $current = Get-BitsTransfer -JobId $job.JobId -ErrorAction SilentlyContinue
            if ($null -eq $current -or $current.JobState -eq 'Error' -or $current.JobState -eq 'Cancelled') {
                Remove-BitsTransfer -BitsJob $job -ErrorAction SilentlyContinue
            }
        }
        # A service-level failure will not clear on a retry; stop wasting the
        # attempts and use curl instead.
        if ($_.Exception.Message -match 'service|BITS') {
            Write-Output '    BITS is unusable; switching to curl.exe'
            for ($c = 1; $c -le $Attempts; $c++) {
                try {
                    Invoke-CurlDownload -Url $Url -Destination $Destination
                    exit 0
                } catch {
                    Write-Output ('    attempt {0} failed: {1}' -f $c, $_.Exception.Message)
                }
                if ($c -lt $Attempts) { Start-Sleep -Seconds 3 }
            }
            break
        }
    }
    if ($try -lt $Attempts) { Start-Sleep -Seconds 3 }
}

Write-Output ('error: download failed after {0} attempts: {1}' -f $Attempts, $Url)
exit 1
