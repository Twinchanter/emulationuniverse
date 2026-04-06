param(
    [string]$CsvPath = "tests/logs/cpu_micro_results.csv",
    [string]$OutMarkdown = "tests/logs/cpu_micro_dashboard.md",
    [string]$OutJson = "tests/logs/cpu_micro_summary.json"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$priorityOrder = @{ critical = 0; high = 1; medium = 2; low = 3 }

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
    if ($priorityOrder.ContainsKey($value)) { return $value }
    return 'high'
}

if (-not (Test-Path $CsvPath)) {
    Write-Error "CPU micro CSV not found at: $CsvPath"
    exit 2
}

$rows = Import-Csv -Path $CsvPath
if (-not $rows -or $rows.Count -eq 0) {
    Write-Error "CPU micro CSV is empty: $CsvPath"
    exit 2
}

$normalized = @()
foreach ($row in $rows) {
    $normalized += [PSCustomObject]@{
        priority = Normalize-Priority -Priority "$($row.priority)"
        test = "$($row.test)"
        passed = ("$($row.passed)").Trim().ToUpperInvariant()
        detail = "$($row.detail)"
    }
}

$total = $normalized.Count
$passCount = @($normalized | Where-Object { $_.passed -eq 'PASS' }).Count
$failCount = $total - $passCount
$passRate = if ($total -gt 0) { [Math]::Round(($passCount * 100.0) / $total, 2) } else { 0 }

$prioritySummary = @()
foreach ($group in ($normalized | Group-Object priority | Sort-Object { $priorityOrder[$_.Name] })) {
    $grpTotal = $group.Count
    $grpPass = @($group.Group | Where-Object { $_.passed -eq 'PASS' }).Count
    $grpFail = $grpTotal - $grpPass
    $grpRate = if ($grpTotal -gt 0) { [Math]::Round(($grpPass * 100.0) / $grpTotal, 2) } else { 0 }

    $prioritySummary += [PSCustomObject]@{
        priority = $group.Name
        total = $grpTotal
        passed = $grpPass
        failed = $grpFail
        passRate = $grpRate
    }
}

$failures = @($normalized | Where-Object { $_.passed -ne 'PASS' } | Sort-Object @{Expression={ $priorityOrder[$_.priority] }}, test)
$generatedAt = (Get-Date).ToString('s')

$payload = [PSCustomObject]@{
    generatedAt = $generatedAt
    sourceCsv = $CsvPath
    totals = [PSCustomObject]@{ total = $total; passed = $passCount; failed = $failCount; passRate = $passRate }
    priorities = $prioritySummary
    failures = $failures
}

Ensure-ParentDirectory -FilePath $OutJson
($payload | ConvertTo-Json -Depth 6) | Set-Content -Path $OutJson -Encoding UTF8

$md = @()
$md += '# CPU Micro Dashboard'
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
foreach ($row in $prioritySummary) {
    $md += "| $($row.priority) | $($row.total) | $($row.passed) | $($row.failed) | $($row.passRate)% |"
}
$md += ''
$md += '## Failing Tests'
$md += ''
if ($failures.Count -eq 0) {
    $md += '- None'
} else {
    foreach ($f in $failures) {
        $md += "- [$($f.priority)] $($f.test) -> $($f.detail)"
    }
}

Ensure-ParentDirectory -FilePath $OutMarkdown
$md -join "`r`n" | Set-Content -Path $OutMarkdown -Encoding UTF8

Write-Host "CPU micro dashboard written: $OutMarkdown"
Write-Host "CPU micro summary written: $OutJson"

if ($failCount -gt 0) { exit 1 }
exit 0
