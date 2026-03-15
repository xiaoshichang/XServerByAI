Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$buildDir = Join-Path $repoRoot 'build'
$cmakeCache = Join-Path $buildDir 'CMakeCache.txt'

if (-not (Test-Path $cmakeCache)) {
    throw "Native build directory is not configured: $buildDir"
}

Push-Location $buildDir
try {
    cmake --build .
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
