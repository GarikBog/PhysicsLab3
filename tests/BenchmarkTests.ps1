[CmdletBinding()]
param([string]$Executable = (Join-Path $PSScriptRoot '..\build\x64\Release\Physics-benchmark.exe'))

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
$testRoot = Join-Path $PSScriptRoot ('..\build\benchmark-tests\' + [Guid]::NewGuid().ToString('N'))

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Number([string]$Value) { [double]::Parse($Value, $culture) }
function Close([double]$Actual, [double]$Expected) {
    Require ([Math]::Abs($Actual - $Expected) -lt 0.000001) "Expected $Expected, got $Actual"
}

# 120 Hz checks idle frames; 144 Hz checks a non-integer frequency ratio.
foreach ($hz in @(120, 144)) {
    $directory = Join-Path $testRoot "fps-$hz"
    & $Executable --benchmark --seconds 2 --repeats 2 --frame-hz $hz --warmup-seconds 1 --output-dir $directory
    Require ($LASTEXITCODE -eq 0) 'Benchmark invocation failed'
    $runs = @(Import-Csv -LiteralPath (Join-Path $directory 'runs.csv'))
    $summary = @(Import-Csv -LiteralPath (Join-Path $directory 'summary.csv'))
    Require ($runs.Count -eq 8 -and $summary.Count -eq 4) 'Expected four variants with two repeats'
    foreach ($row in $runs) {
        $frames = 2 * $hz
        $steps = 2 * [int]$row.base_hz
        Require ([int]$row.frames -eq $frames -and [int]$row.base_steps -eq $steps) 'Incorrect fixed-step scheduling'
        $total = Number $row.physics_total_ms
        Require ($total -gt 0) 'Physics timer must record work'
        Close (Number $row.mean_ms_frame) ($total / $frames)
        Close (Number $row.mean_ms_step) ($total / $steps)
        Require ([int]$row.ccd_limit_hits -eq 0 -and [int]$row.escaped_bodies -eq 0) 'Controlled workload must stay valid'
        if ($row.variant -eq 'MultiRate60_120_240') {
            Require ([int]$row.fast_group_substeps -eq 480) 'Fast group must perform 240 substeps per simulation second'
            Close (Number $row.fast_group_substeps_frame) (240.0 / $hz)
        }
    }
    foreach ($row in $summary) {
        $matching = @($runs | Where-Object variant -eq $row.variant)
        $first = Number $matching[0].mean_ms_frame
        $second = Number $matching[1].mean_ms_frame
        Close (Number $row.mean_ms_frame) (($first + $second) / 2)
        Close (Number $row.sd_ms_frame) ([Math]::Abs($first - $second) / [Math]::Sqrt(2))
        Require ($row.repeatable -eq 'true') 'Repeated snapshot runs must reproduce the final state'
    }
    $inputHash = (Get-FileHash -LiteralPath (Join-Path $directory 'input-scene.txt')).Hash
    $sourceHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '..\snapshots\lab04_timing_primitive.txt')).Hash
    Require ($inputHash -eq $sourceHash) 'Export must preserve the exact input snapshot'
    Require (Test-Path -LiteralPath (Join-Path $directory 'report.md')) 'Human-readable report missing'
    $before = (Get-FileHash -LiteralPath (Join-Path $directory 'runs.csv')).Hash
    & $Executable --benchmark --seconds 1 --repeats 1 --warmup-seconds 0 --output-dir $directory 2>&1 | Out-String | Out-Null
    Require ($LASTEXITCODE -ne 0) 'A previous result must not be overwritten'
    Require ((Get-FileHash -LiteralPath (Join-Path $directory 'runs.csv')).Hash -eq $before) 'Existing output changed'
}

$invalidOutput = Join-Path $testRoot 'invalid'
& $Executable --benchmark --frame-hz 0 --output-dir $invalidOutput 2>&1 | Out-String | Out-Null
Require ($LASTEXITCODE -ne 0 -and -not (Test-Path -LiteralPath $invalidOutput)) 'Invalid options must fail before creating output'
Write-Host 'All benchmark tests passed'
