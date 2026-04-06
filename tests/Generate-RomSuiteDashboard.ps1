param(
    [string]$CsvPath = "tests/logs/rom_suite_results.csv",
    [string]$OutMarkdown = "tests/logs/rom_suite_dashboard.md",
    [string]$OutJson = "tests/logs/rom_suite_summary.json"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$priorityOrder = @{
    critical = 0
    high = 1
    medium = 2
    low = 3
}

function Normalize-Priority {
    param([string]$Priority)
    $value = ("$Priority").Trim().ToLowerInvariant()
    if ($priorityOrder.ContainsKey($value)) {
        return $value
    }
    return 'high'
}

function Ensure-ParentDirectory {
    param([string]$FilePath)
    $parent = Split-Path -Parent $FilePath
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

if (-not (Test-Path $CsvPath)) {
    Write-Error "ROM suite CSV not found at: $CsvPath"
    exit 2
}

$rows = Import-Csv -Path $CsvPath
if (-not $rows -or $rows.Count -eq 0) {
    Write-Error "ROM suite CSV is empty: $CsvPath"
    exit 2
}

$normalized = @()
foreach ($row in $rows) {
    $status = ("$($row.passed)").Trim().ToUpperInvariant()
    $priority = if ($row.PSObject.Properties.Name -contains 'priority') {
        Normalize-Priority -Priority "$($row.priority)"
    } else {
        'high'
    }

    $maxFrames = if ($row.PSObject.Properties.Name -contains 'max_frames') {
        [int]("$($row.max_frames)" -as [int])
    } else {
        0
    }

    $normalized += [PSCustomObject]@{
        suite = "$($row.suite)"
        priority = $priority
        maxFrames = $maxFrames
        rom = "$($row.rom)"
        passed = $status
        detail = "$($row.detail)"
    }
}

$total = $normalized.Count
$passCount = @($normalized | Where-Object { $_.passed -eq 'PASS' }).Count
$failCount = @($normalized | Where-Object { $_.passed -ne 'PASS' }).Count
$passRate = if ($total -gt 0) { [Math]::Round(($passCount * 100.0) / $total, 2) } else { 0 }

$suiteSummary = @()
foreach ($group in ($normalized | Group-Object suite | Sort-Object Name)) {
    $suiteTotal = $group.Count
    $suitePass = @($group.Group | Where-Object { $_.passed -eq 'PASS' }).Count
    $suiteFail = $suiteTotal - $suitePass
    $suiteRate = if ($suiteTotal -gt 0) { [Math]::Round(($suitePass * 100.0) / $suiteTotal, 2) } else { 0 }

    $suiteSummary += [PSCustomObject]@{
        suite = $group.Name
        total = $suiteTotal
        passed = $suitePass
        failed = $suiteFail
        passRate = $suiteRate
    }
}

$prioritySummary = @()
foreach ($group in ($normalized | Group-Object priority | Sort-Object { $priorityOrder[$_.Name] })) {
    $prioTotal = $group.Count
    $prioPass = @($group.Group | Where-Object { $_.passed -eq 'PASS' }).Count
    $prioFail = $prioTotal - $prioPass
    $prioRate = if ($prioTotal -gt 0) { [Math]::Round(($prioPass * 100.0) / $prioTotal, 2) } else { 0 }

    $prioritySummary += [PSCustomObject]@{
        priority = $group.Name
        total = $prioTotal
        passed = $prioPass
        failed = $prioFail
        passRate = $prioRate
    }
}

$failures = @($normalized | Where-Object { $_.passed -ne 'PASS' } | Sort-Object @{Expression = { $priorityOrder[$_.priority] }}, suite, rom)
$generatedAt = (Get-Date).ToString('s')

$jsonModel = [PSCustomObject]@{
    generatedAt = $generatedAt
    sourceCsv = $CsvPath
    totals = [PSCustomObject]@{
        total = $total
        passed = $passCount
        failed = $failCount
        passRate = $passRate
    }
    priorities = $prioritySummary
    suites = $suiteSummary
    failures = $failures
}

Ensure-ParentDirectory -FilePath $OutJson
($jsonModel | ConvertTo-Json -Depth 6) | Set-Content -Path $OutJson -Encoding UTF8

$md = @()
$md += '# ROM Suite Dashboard'
$md += ''
$md += "Generated: $generatedAt"
$md += ''
$md += '## Totals'
$md += ''
$md += "- Total: $total"
$md += "- Passed: $passCount"
$md += "- Failed: $failCount"
$md += "- Pass Rate: $passRate%"
$md += ''
$md += '## Per Priority'
$md += ''
$md += '| Priority | Total | Passed | Failed | Pass Rate |'
$md += '|---|---:|---:|---:|---:|'
foreach ($priority in $prioritySummary) {
    $md += "| $($priority.priority) | $($priority.total) | $($priority.passed) | $($priority.failed) | $($priority.passRate)% |"
}
$md += ''
$md += '## Per Suite'
$md += ''
$md += '| Suite | Total | Passed | Failed | Pass Rate |'
$md += '|---|---:|---:|---:|---:|'
foreach ($suite in $suiteSummary) {
    $md += "| $($suite.suite) | $($suite.total) | $($suite.passed) | $($suite.failed) | $($suite.passRate)% |"
}
$md += ''
$md += '## Failing Cases'
$md += ''
if ($failures.Count -eq 0) {
    $md += '- None'
} else {
    foreach ($failure in $failures) {
        $md += "- [$($failure.priority)] [$($failure.suite)] $($failure.rom) (max_frames=$($failure.maxFrames)) -> $($failure.detail)"
    }
}

Ensure-ParentDirectory -FilePath $OutMarkdown
$md -join "`r`n" | Set-Content -Path $OutMarkdown -Encoding UTF8

Write-Host "Dashboard written: $OutMarkdown"
Write-Host "Summary JSON written: $OutJson"

if ($failCount -gt 0) {
    exit 1
}

exit 0
