param(
    [UInt64]$Births = 10000000,
    [UInt64]$Seed = 1,
    [string]$Out = "",
    [string]$View = "",
    [switch]$NoModules,
    [switch]$NoDrift,
    [switch]$NoBrowser
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    # Python is only the local web server. The evolutionary engine remains C++.
    $pythonCommand = $null
    $pythonPrefix = @()
    foreach ($candidate in @("py", "python", "python3")) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) {
            $prefix = @()
            if ($candidate -eq "py") { $prefix = @("-3") }
            & $candidate @prefix -c "import sys; assert sys.version_info >= (3, 9)" 2>$null
            if ($LASTEXITCODE -eq 0) {
                $pythonCommand = $candidate
                $pythonPrefix = $prefix
                break
            }
        }
    }
    if (-not $pythonCommand) { throw "Python 3.9 or later is required for the local visual lab. The C++ simulator can still run without the viewer." }
    $arguments = @("scripts/lab.py")
    if (-not [string]::IsNullOrWhiteSpace($View)) {
        if (-not (Test-Path -LiteralPath $View -PathType Container)) { throw "Run directory not found: $View" }
        $arguments += @("--run", $View)
    } else {
        cmake -S lab -B build-lab -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
        cmake --build build-lab --config Release --parallel 2
        if ($LASTEXITCODE -ne 0) { throw "Build failed" }
        ctest --test-dir build-lab -C Release --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed; experiment not started" }
        $exe = Join-Path $root "build-lab/Release/driftvm_lab.exe"
        if (-not (Test-Path $exe)) { $exe = Join-Path $root "build-lab/driftvm_lab.exe" }
        if (-not (Test-Path $exe)) { throw "Compiled lab executable not found" }
        if ([string]::IsNullOrWhiteSpace($Out)) {
            $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
            $suffix = [Guid]::NewGuid().ToString("N").Substring(0, 8)
            $Out = "out/cambrian02-seed-$Seed-$stamp-$suffix"
        }
        if (Test-Path -LiteralPath $Out) { throw "Output already exists; choose a new directory: $Out" }
        $arguments += @("--run", $Out, "--launch", $exe, "--births", "$Births", "--seed", "$Seed")
        if ($NoModules) { $arguments += "--no-modules" }
        if ($NoDrift) { $arguments += "--no-drift" }
    }
    if (-not $NoBrowser) { $arguments += "--open" }
    & $pythonCommand @pythonPrefix @arguments
    if ($LASTEXITCODE -ne 0) { throw "Visual lab exited with an error" }
} finally {
    Pop-Location
}
