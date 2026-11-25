[CmdletBinding()]
param(
    [ValidateRange(1, 3600)][int]$Seconds = 50,
    [ValidateRange(1, 100)][int]$Repeats = 3,
    [ValidateRange(1, 1000)][int]$FrameHz = 120,
    [ValidateRange(0, 600)][int]$WarmupSeconds = 5
)

$ErrorActionPreference = 'Stop'
$buildCommand = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
if ($buildCommand) {
    $buildToolPath = $buildCommand.Source
} else {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswherePath)) {
        throw 'MSBuild was not found. Install Visual Studio 2022 with Desktop development with C++.'
    }
    $buildToolPath = & $vswherePath -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $buildToolPath) { throw 'MSBuild was not found in Visual Studio.' }
}

# A separate executable lets an already-open interactive simulation keep running.
& $buildToolPath (Join-Path $PSScriptRoot 'Physics.sln') /p:Configuration=Release /p:Platform=x64 /p:TargetName=Physics-benchmark /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }

$benchmarkExe = Join-Path $PSScriptRoot 'build\x64\Release\Physics-benchmark.exe'
& $benchmarkExe --benchmark --seconds $Seconds --repeats $Repeats --frame-hz $FrameHz --warmup-seconds $WarmupSeconds
if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $LASTEXITCODE" }
