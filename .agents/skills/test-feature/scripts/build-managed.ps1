Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$dotnetCliHome = Join-Path $repoRoot '.dotnet-cli'
$nugetPackages = Join-Path $repoRoot '.nuget\packages'

New-Item -ItemType Directory -Force -Path $dotnetCliHome | Out-Null
New-Item -ItemType Directory -Force -Path $nugetPackages | Out-Null

$env:DOTNET_CLI_HOME = $dotnetCliHome
$env:NUGET_PACKAGES = $nugetPackages
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'

Push-Location $repoRoot
try {
    dotnet build .\XServerByAI.Managed.sln -m:1 -c Debug
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
