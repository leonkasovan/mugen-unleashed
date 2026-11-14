<#
.SYNOPSIS
    Read vendor metadata for the Android toolchain bootstrap.

.DESCRIPTION
    The cmd.exe replacement for the removed tools/toolchain_metadata.py: it
    parses the documents the vendors publish and prints the archive URL plus
    its checksum as one pipe-separated line, so build.bat can hand the values
    to curl and Get-FileHash without trying to parse JSON/XML itself.

    This script downloads and verifies nothing. build.bat does that with
    curl.exe and Get-FileHash, unpacks with System32's bsdtar, and installs
    with the SDK's own sdkmanager.bat.

    Operations:
      jdk <metadata.json>            -> URL|sha256
      sdk <repository.xml> <host> <version> -> URL|algorithm|checksum
      jdk-home <extracted-dir>       -> path of the unique Java home

    Only the data each publisher documents is trusted: the JDK link must be
    on github.com/adoptium and the SDK archive inside Google's repository.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('jdk', 'sdk', 'jdk-home')]
    [string]$Operation,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Fail([string]$message) {
    [Console]::Error.WriteLine('toolchain metadata: ' + $message)
    exit 1
}

switch ($Operation) {
    'jdk' {
        if ($Rest.Count -lt 1) { Fail 'jdk needs a metadata JSON path' }
        $assets = Get-Content -LiteralPath $Rest[0] -Raw | ConvertFrom-Json
        foreach ($asset in $assets) {
            $binary = $asset.binary
            if ($null -eq $binary) { continue }
            if ($binary.image_type -and $binary.image_type -ne 'jdk') { continue }
            $package = $binary.package
            if ($null -eq $package) { continue }
            $link = [string]$package.link
            if (-not $link.StartsWith('https://github.com/adoptium/')) {
                Fail 'Temurin archive is outside the official publisher'
            }
            $checksum = [string]$package.checksum
            if ($checksum -notmatch '^[0-9a-fA-F]{64}$') { Fail 'invalid Temurin SHA-256' }
            [Console]::Out.WriteLine($link + '|' + $checksum.ToLowerInvariant())
            return
        }
        Fail 'Adoptium returned no JDK archive'
    }

    'sdk' {
        if ($Rest.Count -lt 3) { Fail 'sdk needs a repository XML, host OS and version' }
        $hostOs = $Rest[1]
        $version = $Rest[2]
        [xml]$repository = Get-Content -LiteralPath $Rest[0] -Raw
        $wanted = 'cmdline-tools;' + $version
        $package = $repository.SelectNodes("//*[local-name()='remotePackage']") |
            Where-Object { $_.path -eq $wanted } | Select-Object -First 1
        if ($null -eq $package) { Fail ('command-line tools ' + $version + ' is unavailable') }

        foreach ($archive in $package.SelectNodes(".//*[local-name()='archive']")) {
            $os = $archive.SelectSingleNode(".//*[local-name()='host-os']")
            if ($null -eq $os -or $os.InnerText -ne $hostOs) { continue }
            $complete = $archive.SelectSingleNode(".//*[local-name()='complete']")
            if ($null -eq $complete) { continue }
            $relative = $complete.SelectSingleNode("./*[local-name()='url']").InnerText.Trim()
            $checksumNode = $complete.SelectSingleNode("./*[local-name()='checksum']")
            if ($null -eq $checksumNode) { Fail 'SDK archive has no checksum' }
            $url = 'https://dl.google.com/android/repository/' + $relative
            if (-not $url.StartsWith('https://dl.google.com/android/repository/')) {
                Fail 'SDK archive is outside Google''s repository'
            }
            $algorithm = $checksumNode.GetAttribute('type')
            if (-not $algorithm) { $algorithm = 'sha1' }
            $algorithm = $algorithm.ToLowerInvariant().Replace('-', '')
            if ($algorithm -ne 'sha1' -and $algorithm -ne 'sha256') {
                Fail ('unsupported SDK checksum algorithm: ' + $algorithm)
            }
            $value = $checksumNode.InnerText.Trim()
            $expected = if ($algorithm -eq 'sha1') { 40 } else { 64 }
            if ($value -notmatch ('^[0-9a-fA-F]{' + $expected + '}$')) {
                Fail 'invalid SDK checksum'
            }
            [Console]::Out.WriteLine($url + '|' + $algorithm + '|' + $value.ToLowerInvariant())
            return
        }
        Fail ('command-line tools ' + $version + ' is unavailable for ' + $hostOs)
    }

    'jdk-home' {
        if ($Rest.Count -lt 1) { Fail 'jdk-home needs an extracted directory' }
        $root = (Resolve-Path -LiteralPath $Rest[0]).Path
        $homes = @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter 'java.exe' |
            Where-Object { $_.Directory.Name -eq 'bin' } |
            ForEach-Object { $_.Directory.Parent.FullName } |
            Select-Object -Unique)
        if ($homes.Count -ne 1) { Fail 'expected one Java home in the verified archive' }
        [Console]::Out.WriteLine($homes[0])
    }
}
