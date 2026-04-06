param(
    [string]$SummaryJson = "tests/logs/rom_suite_summary.json",
    [string]$HistoryJson = "tests/logs/rom_suite_history.json",
    [string]$OutMarkdown = "tests/logs/rom_suite_trend.md"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Ensure-ParentDirectory {
    param([string]$FilePath)
    $parent = Split-Path -Parent $FilePath
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

if (-not (Test-Path $SummaryJson)) {
    Write-Error "Summary JSON not found at: $SummaryJson"
    exit 2
}

$current = Get-Content -Path $SummaryJson -Raw | ConvertFrom-Json
if (-not $current -or -not $current.totals) {
    Write-Error "Summary JSON is missing required totals data: $SummaryJson"
    exit 2
}

$history = @()
if (Test-Path $HistoryJson) {
    $loaded = Get-Content -Path $HistoryJson -Raw | ConvertFrom-Json
    if ($loaded -and $loaded.PSObject.Properties.Name -contains 'runs') {
        $history = @($loaded.runs)
    } elseif ($loaded -is [System.Array]) {
        $history = @($loaded)
    } elseif ($loaded) {
        $history = @($loaded)
    }
}

$currentRecord = [PSCustomObject]@{
    generatedAt = "$($current.generatedAt)"
    total = [int]$current.totals.total
    passed = [int]$current.totals.passed
    failed = [int]$current.totals.failed
    passRate = [double]$current.totals.passRate
}

if ($history.Count -gt 0 -and "$($history[-1].generatedAt)" -eq $currentRecord.generatedAt) {
    $history[$history.Count - 1] = $currentRecord
} else {
    $history += $currentRecord
}

# Keep history bounded.
if ($history.Count -gt 200) {
    $history = @($history | Select-Object -Last 200)
}

Ensure-ParentDirectory -FilePath $HistoryJson
$historyPayload = [PSCustomObject]@{
    generatedAt = (Get-Date).ToString('s')
    runs = @($history)
}
($historyPayload | ConvertTo-Json -Depth 6) | Set-Content -Path $HistoryJson -Encoding UTF8

$latest = $history[-1]
$previous = $null
if ($history.Count -ge 2) {
    $previous = $history[-2]
}

$deltaPassed = 0
$deltaFailed = 0
$deltaPassRate = 0.0
if ($previous) {
    $deltaPassed = [int]$latest.passed - [int]$previous.passed
    $deltaFailed = [int]$latest.failed - [int]$previous.failed
    $deltaPassRate = [Math]::Round(([double]$latest.passRate - [double]$previous.passRate), 2)
}

function Format-Delta {
    param([double]$Value)
    if ($Value -gt 0) { return "+$Value" }
    return "$Value"
}

$md = @()
$md += '# ROM Suite Trend'
$md += ''
$md += "Latest run: $($latest.generatedAt)"
$md += "History length: $($history.Count)"
$md += ''
$md += '## Latest Totals'
$md += ''
$md += "- Total: $($latest.total)"
$md += "- Passed: $($latest.passed)"
$md += "- Failed: $($latest.failed)"
$md += "- Pass Rate: $($latest.passRate)%"
$md += ''
$md += '## Delta vs Previous Run'
$md += ''
if ($previous) {
    $md += "- Passed: $(Format-Delta -Value $deltaPassed)"
    $md += "- Failed: $(Format-Delta -Value $deltaFailed)"
    $md += "- Pass Rate: $(Format-Delta -Value $deltaPassRate)%"
} else {
    $md += '- No previous run available.'
}
$md += ''
$md += '## Recent Runs (Newest First)'
$md += ''
$md += '| Generated At | Total | Passed | Failed | Pass Rate |'
$md += '|---|---:|---:|---:|---:|'
foreach ($row in @($history | Select-Object -Last 10 | Sort-Object generatedAt -Descending)) {
    $md += "| $($row.generatedAt) | $($row.total) | $($row.passed) | $($row.failed) | $($row.passRate)% |"
}

Ensure-ParentDirectory -FilePath $OutMarkdown
$md -join "`r`n" | Set-Content -Path $OutMarkdown -Encoding UTF8

Write-Host "Trend history written: $HistoryJson"
Write-Host "Trend report written: $OutMarkdown"
exit 0
