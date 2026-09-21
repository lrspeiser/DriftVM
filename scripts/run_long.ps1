param(
    [UInt64]$Births = 10000000,
    [UInt64]$Seed = 1,
    [string]$Out = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Out)) {
    $Out = "out/cambrian0-seed-$Seed"
}

cmake -S . -B build
cmake --build build --config Release

$exe = ".\\build\\Release\\driftvm.exe"
if (-not (Test-Path $exe)) {
    $exe = ".\\build\\driftvm.exe"
}

Write-Host "Running DriftVM: births=$Births seed=$Seed out=$Out"
& $exe --births $Births --population 256 --seed $Seed --report-every 100000 --out $Out

Write-Host ""
Get-Content "$Out\\summary.txt"
