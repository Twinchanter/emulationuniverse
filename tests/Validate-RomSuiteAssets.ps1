param(
    [string]$ManifestPath = "tests/rom_manifest.csv",
    [string]$RomsRoot = "",
    [string]$OutMarkdown = "tests/logs/rom_assets_readiness.md",
    [string]$OutJson = "tests/logs/rom_assets_readiness.json",
    [switch]$FailOnMissing = $true
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

function Resolve-RomRoot {
    param([string]$RootArg)

    if (-not [string]::IsNullOrWhiteSpace($RootArg)) {
        return $RootArg
    }

    if (-not [string]::IsNullOrWhiteSpace($env:EMU_TEST_ROMS_ROOT)) {
        return $env:EMU_TEST_ROMS_ROOT
    }

    if (Test-Path "roms/test") {
        return "roms/test"
    }

    if (Test-Path "../roms/test") {
        return "../roms/test"
    }

    return ""
}

if (-not (Test-Path $ManifestPath)) {
    Write-Error "Manifest not found at: $ManifestPath"
    exit 2
}

$root = Resolve-RomRoot -RootArg $RomsRoot
if ([string]::IsNullOrWhiteSpace($root)) {
    Write-Error "ROM root not found. Set EMU_TEST_ROMS_ROOT or provide -RomsRoot."
    exit 2
}

$entries = @()
$lineNumber = 0
Get-Content -Path $ManifestPath | ForEach-Object {
    $lineNumber += 1
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

    $fullPath = Join-Path -Path $root -ChildPath $path
    $exists = Test-Path $fullPath

    $entries += [PSCustomObject]@{
        suite = $suite
        mode = $mode
        priority = $priority
        relativePath = $path
        fullPath = $fullPath
        exists = $exists
        lineNumber = $lineNumber
    }
}

if ($entries.Count -eq 0) {
    Write-Error "Manifest parsed but no valid entries were found: $ManifestPath"
    exit 2
}

$missing = @($entries | Where-Object { -not $_.exists } | Sort-Object @{Expression = { $priorityOrder[$_.priority] }}, suite, relativePath)
$present = @($entries | Where-Object { $_.exists })

$prioritySummary = @()
foreach ($group in ($entries | Group-Object priority | Sort-Object { $priorityOrder[$_.Name] })) {
    $total = $group.Count
    $ok = @($group.Group | Where-Object { $_.exists }).Count
    $missingCount = $total - $ok

    $prioritySummary += [PSCustomObject]@{
        priority = $group.Name
        total = $total
        present = $ok
        missing = $missingCount
    }
}

$generatedAt = (Get-Date).ToString('s')
$jsonModel = [PSCustomObject]@{
    generatedAt = $generatedAt
    manifestPath = $ManifestPath
    romsRoot = $root
    totals = [PSCustomObject]@{
        total = $entries.Count
        present = $present.Count
        missing = $missing.Count
    }
    priorities = $prioritySummary
    missingFiles = $missing
}

Ensure-ParentDirectory -FilePath $OutJson
($jsonModel | ConvertTo-Json -Depth 6) | Set-Content -Path $OutJson -Encoding UTF8

$md = @()
$md += '# ROM Asset Readiness'
$md += ''
$md += "Generated: $generatedAt"
$md += "Manifest: $ManifestPath"
$md += "ROM Root: $root"
$md += ''
$md += '## Totals'
$md += ''
$md += "- Total manifest entries: $($entries.Count)"
$md += "- Present: $($present.Count)"
$md += "- Missing: $($missing.Count)"
$md += ''
$md += '## Per Priority'
$md += ''
$md += '| Priority | Total | Present | Missing |'
$md += '|---|---:|---:|---:|'
foreach ($row in $prioritySummary) {
    $md += "| $($row.priority) | $($row.total) | $($row.present) | $($row.missing) |"
}
$md += ''
$md += '## Missing Files'
$md += ''
if ($missing.Count -eq 0) {
    $md += '- None'
} else {
    foreach ($file in $missing) {
        $md += "- [$($file.priority)] [$($file.suite)] $($file.relativePath)"
    }
}

Ensure-ParentDirectory -FilePath $OutMarkdown
$md -join "`r`n" | Set-Content -Path $OutMarkdown -Encoding UTF8

Write-Host "Asset readiness written: $OutMarkdown"
Write-Host "Asset summary JSON written: $OutJson"

if ($FailOnMissing -and $missing.Count -gt 0) {
    exit 1
}

exit 0
