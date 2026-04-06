param(
    [string]$ManifestPath = "tests/rom_manifest.csv",
    [string]$SourceRoot,
    [string]$DestinationRoot = "roms/test",
    [string]$OutMarkdown = "tests/logs/rom_assets_import.md",
    [string]$OutJson = "tests/logs/rom_assets_import.json",
    [switch]$Overwrite = $false,
    [switch]$FailOnUnresolved = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$priorityOrder = @{
    critical = 0
    high = 1
    medium = 2
    low = 3
}

function Ensure-ParentDirectory {
    param([string]$FilePath)
    $parent = Split-Path -Parent $FilePath
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

function Normalize-Priority {
    param([string]$Priority)
    $value = ("$Priority").Trim().ToLowerInvariant()
    if ($priorityOrder.ContainsKey($value)) {
        return $value
    }
    return 'high'
}

if (-not (Test-Path $ManifestPath)) {
    Write-Error "Manifest not found at: $ManifestPath"
    exit 2
}

if ([string]::IsNullOrWhiteSpace($SourceRoot) -or -not (Test-Path $SourceRoot)) {
    Write-Error "Source root is missing or invalid. Provide -SourceRoot pointing to your local ROM asset directory."
    exit 2
}

if (-not (Test-Path $DestinationRoot)) {
    New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null
}

$entries = @()
Get-Content -Path $ManifestPath | ForEach-Object {
    $line = $_.Trim()
    if ([string]::IsNullOrWhiteSpace($line)) { return }
    if ($line.StartsWith('#')) { return }

    $parts = @($line.Split(','))
    if ($parts.Count -lt 3) { return }

    $suite = $parts[0].Trim()
    $mode = $parts[1].Trim().ToLowerInvariant()
    $path = $parts[2].Trim()

    if ($suite.ToLowerInvariant() -eq 'suite' -and $mode -eq 'mode') { return }
    if ([string]::IsNullOrWhiteSpace($suite) -or [string]::IsNullOrWhiteSpace($mode) -or [string]::IsNullOrWhiteSpace($path)) { return }

    $priority = if ($parts.Count -ge 4) { Normalize-Priority -Priority $parts[3].Trim() } else { 'high' }

    $entries += [PSCustomObject]@{
        suite = $suite
        mode = $mode
        priority = $priority
        relativePath = $path
    }
}

if ($entries.Count -eq 0) {
    Write-Error "No valid manifest entries found in: $ManifestPath"
    exit 2
}

$results = @()
foreach ($entry in ($entries | Sort-Object @{Expression = { $priorityOrder[$_.priority] }}, suite, relativePath)) {
    $sourceCandidate = Join-Path -Path $SourceRoot -ChildPath $entry.relativePath
    $destinationPath = Join-Path -Path $DestinationRoot -ChildPath $entry.relativePath

    $status = 'unresolved'
    $detail = ''
    $selectedSource = ''

    if ((Test-Path $destinationPath) -and (-not $Overwrite)) {
        $status = 'already-present'
        $detail = 'destination exists and overwrite disabled'
        $selectedSource = $destinationPath
    } else {
        $foundCandidates = @()
        if (Test-Path $sourceCandidate) {
            $foundCandidates += (Resolve-Path $sourceCandidate).Path
        } else {
            $leaf = Split-Path -Leaf $entry.relativePath
            $foundCandidates = @(
                Get-ChildItem -Path $SourceRoot -Filter $leaf -File -Recurse -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName
            )
        }

        if ($foundCandidates.Count -eq 1) {
            $selectedSource = $foundCandidates[0]
            $destParent = Split-Path -Parent $destinationPath
            if (-not (Test-Path $destParent)) {
                New-Item -ItemType Directory -Path $destParent -Force | Out-Null
            }

            if ($Overwrite) {
                Copy-Item -Path $selectedSource -Destination $destinationPath -Force
            } else {
                Copy-Item -Path $selectedSource -Destination $destinationPath
            }
            $status = 'copied'
            $detail = 'copied into manifest path'
        } elseif ($foundCandidates.Count -gt 1) {
            $status = 'ambiguous'
            $detail = ('multiple candidates found: ' + ($foundCandidates -join '; '))
        } else {
            $status = 'missing-in-source'
            $detail = 'file not found under source root'
        }
    }

    $results += [PSCustomObject]@{
        suite = $entry.suite
        mode = $entry.mode
        priority = $entry.priority
        relativePath = $entry.relativePath
        source = $selectedSource
        destination = $destinationPath
        status = $status
        detail = $detail
    }
}

$copied = @($results | Where-Object { $_.status -eq 'copied' })
$alreadyPresent = @($results | Where-Object { $_.status -eq 'already-present' })
$unresolved = @($results | Where-Object { $_.status -in @('missing-in-source', 'ambiguous', 'unresolved') })

$generatedAt = (Get-Date).ToString('s')
$payload = [PSCustomObject]@{
    generatedAt = $generatedAt
    manifestPath = $ManifestPath
    sourceRoot = (Resolve-Path $SourceRoot).Path
    destinationRoot = (Resolve-Path $DestinationRoot).Path
    totals = [PSCustomObject]@{
        total = $results.Count
        copied = $copied.Count
        alreadyPresent = $alreadyPresent.Count
        unresolved = $unresolved.Count
    }
    entries = $results
}

Ensure-ParentDirectory -FilePath $OutJson
($payload | ConvertTo-Json -Depth 7) | Set-Content -Path $OutJson -Encoding UTF8

$md = @()
$md += '# ROM Asset Import Report'
$md += ''
$md += "Generated: $generatedAt"
$md += "Manifest: $ManifestPath"
$md += "Source Root: $((Resolve-Path $SourceRoot).Path)"
$md += "Destination Root: $((Resolve-Path $DestinationRoot).Path)"
$md += ''
$md += '## Totals'
$md += ''
$md += "- Total entries: $($results.Count)"
$md += "- Copied: $($copied.Count)"
$md += "- Already present: $($alreadyPresent.Count)"
$md += "- Unresolved: $($unresolved.Count)"
$md += ''
$md += '## Unresolved Entries'
$md += ''
if ($unresolved.Count -eq 0) {
    $md += '- None'
} else {
    foreach ($item in $unresolved) {
        $md += "- [$($item.priority)] [$($item.suite)] $($item.relativePath) -> $($item.status): $($item.detail)"
    }
}

Ensure-ParentDirectory -FilePath $OutMarkdown
$md -join "`r`n" | Set-Content -Path $OutMarkdown -Encoding UTF8

Write-Host "Asset import report written: $OutMarkdown"
Write-Host "Asset import JSON written: $OutJson"

if ($FailOnUnresolved -and $unresolved.Count -gt 0) {
    exit 1
}

exit 0
