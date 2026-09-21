param(
    [UInt64]$Births = 10000000,
    [int]$Rounds = 3,
    [UInt64]$Seed = 1,
    [string]$Out = "",
    [string]$Resume = "",
    [switch]$NoModules,
    [switch]$NoDrift,
    [switch]$NoBrowser,
    [switch]$RequireRust
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is required. Use the same C++ tools as your existing DriftVM build."
}
$python = $null
$prefix = @()
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3,9) else 1)" 2>$null
    if ($LASTEXITCODE -eq 0) { $python = "py"; $prefix = @("-3") }
}
if (-not $python -and (Get-Command python -ErrorAction SilentlyContinue)) {
    & python -c "import sys; sys.exit(0 if sys.version_info >= (3,9) else 1)" 2>$null
    if ($LASTEXITCODE -eq 0) { $python = "python" }
}
if (-not $python) { throw "Python 3.9+ is required for the native benchmark controller." }
$arguments = @($prefix) + @("scripts/sort_goal.py", "--births", "$Births", "--rounds", "$Rounds", "--seed", "$Seed")
if ($Out) { $arguments += @("--out", $Out) }
if ($Resume) { $arguments += @("--resume", $Resume) }
if ($NoModules) { $arguments += "--no-modules" }
if ($NoDrift) { $arguments += "--no-drift" }
if ($NoBrowser) { $arguments += "--no-browser" }
if ($RequireRust) { $arguments += "--require-rust" }
& $python @arguments
if ($LASTEXITCODE -ne 0) { throw "DriftSort stopped with exit code $LASTEXITCODE. See the run.log in the new result directory." }
