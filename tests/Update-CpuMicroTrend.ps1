param(
    [string]$SummaryJson = "tests/logs/cpu_micro_summary.json",
    [string]$HistoryJson = "tests/logs/cpu_micro_history.json",
    [string]$OutMarkdown = "tests/logs/cpu_micro_trend.md"
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
    Write-Error "CPU micro summary JSON not found at: $SummaryJson"
    exit 2
}

$current = Get-Content -Path $SummaryJson -Raw | ConvertFrom-Json
if (-not $current -or -not $current.totals) {
    Write-Error "CPU micro summary JSON missing totals: $SummaryJson"
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

$record = [PSCustomObject]@{
    generatedAt = "$($current.generatedAt)"
    total = [int]$current.totals.total
    passed = [int]$current.totals.passed
    failed = [int]$current.totals.failed
    passRate = [double]$current.totals.passRate
}

if ($history.Count -gt 0 -and "$($history[-1].generatedAt)" -eq $record.generatedAt) {
    $history[$history.Count - 1] = $record
} else {
    $history += $record
}

if ($history.Count -gt 200) {
    $history = @($history | Select-Object -Last 200)
}

Ensure-ParentDirectory -FilePath $HistoryJson
$payload = [PSCustomObject]@{ generatedAt = (Get-Date).ToString('s'); runs = @($history) }
($payload | ConvertTo-Json -Depth 6) | Set-Content -Path $HistoryJson -Encoding UTF8

$latest = $history[-1]
$previous = $null
if ($history.Count -ge 2) { $previous = $history[-2] }

$deltaPassed = 0
$deltaFailed = 0
$deltaRate = 0.0
if ($previous) {
    $deltaPassed = [int]$latest.passed - [int]$previous.passed
    $deltaFailed = [int]$latest.failed - [int]$previous.failed
    $deltaRate = [Math]::Round(([double]$latest.passRate - [double]$previous.passRate), 2)
}

function Format-Delta {
    param([double]$Value)
    if ($Value -gt 0) { return "+$Value" }
    return "$Value"
}

$md = @()
$md += '# CPU Micro Trend'
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
    $md += "- Pass Rate: $(Format-Delta -Value $deltaRate)%"
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

Write-Host "CPU micro trend history written: $HistoryJson"
Write-Host "CPU micro trend report written: $OutMarkdown"
exit 0
