Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
Push-Location $repoRoot
try {
    cmake -S . -B build -DXS_BUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: cmake -S . -B build -DXS_BUILD_TESTS=ON"
    }

    cmake --build build --config Debug
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: cmake --build build --config Debug"
    }

    ctest --test-dir build -C Debug --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: ctest --test-dir build -C Debug --output-on-failure"
    }
}
finally {
    Pop-Location
}
