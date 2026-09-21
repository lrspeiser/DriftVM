param(
    [UInt64]$Births = 10000000,
    [UInt64]$Seed = 1,
    [string]$Out = "",
    [UInt64]$ReportEvery = 100000,
    [switch]$NoDrift,
    [switch]$FixedLanguage
)
$ErrorActionPreference = "Stop"
function Invoke-Checked {
    param([string]$Command, [string[]]$Arguments)
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    if ([string]::IsNullOrWhiteSpace($Out)) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
        $tag = [guid]::NewGuid().ToString("N").Substring(0, 6)
        $Out = "out/cambrian01-seed-$Seed-$stamp-$tag"
    }
    if (Test-Path $Out) { throw "Output already exists. Choose a new -Out; old runs are never overwritten." }
    Invoke-Checked -Command "cmake" -Arguments @("-S", ".", "-B", "build")
    Invoke-Checked -Command "cmake" -Arguments @("--build", "build", "--config", "Release", "--parallel")
    Invoke-Checked -Command "ctest" -Arguments @("--test-dir", "build", "-C", "Release", "--output-on-failure")
    $exe = ".\build\Release\driftvm.exe"
    if (-not (Test-Path $exe)) { $exe = ".\build\driftvm.exe" }
    if (-not (Test-Path $exe)) { throw "Build did not produce driftvm.exe" }
    $runArgs = @("--births", "$Births", "--population", "256", "--seed", "$Seed", "--report-every", "$ReportEvery", "--out", $Out)
    if ($NoDrift) { $runArgs += @("--drift-fraction", "0") }
    if ($FixedLanguage) { $runArgs += @("--semantic-mutation-rate", "0") }
    Write-Host "Running corrected DriftVM: births=$Births seed=$Seed out=$Out"
    Invoke-Checked -Command $exe -Arguments $runArgs
    Get-Content (Join-Path $Out "summary.txt")
} finally {
    Pop-Location
}
