<#
.SYNOPSIS
    Download a file with BITS, Windows' own transfer service, showing progress.

.DESCRIPTION
    Used by the port build scripts instead of curl.exe. BITS is part of
    Windows: it follows HTTP redirects, uses the system proxy, and retries a
    transfer from where it stopped when the connection drops. The job is
    given a stable display name, so a run that is interrupted leaves the job
    in the BITS queue and the next run resumes it instead of starting over.

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
    }
    if ($try -lt $Attempts) { Start-Sleep -Seconds 3 }
}

Write-Output ('error: download failed after {0} attempts: {1}' -f $Attempts, $Url)
exit 1
