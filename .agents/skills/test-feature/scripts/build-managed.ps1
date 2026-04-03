Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-DotnetCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & dotnet @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: dotnet $($Arguments -join ' ')"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$dotnetCliHome = Join-Path $repoRoot '.dotnet-cli'
$nugetPackages = Join-Path $repoRoot '.nuget\packages'

New-Item -ItemType Directory -Force -Path $dotnetCliHome | Out-Null
New-Item -ItemType Directory -Force -Path $nugetPackages | Out-Null

$env:DOTNET_CLI_HOME = $dotnetCliHome
$env:NUGET_PACKAGES = $nugetPackages
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_ADD_GLOBAL_TOOLS_TO_PATH = '0'

Push-Location $repoRoot
try {
    Invoke-DotnetCommand @('build', '.\XServerByAI.Managed.sln', '-m:1', '-c', 'Debug')
    Invoke-DotnetCommand @('test', '.\XServerByAI.Managed.sln', '-m:1', '-c', 'Debug', '--no-build', '--no-restore')
    Invoke-DotnetCommand @('test', '.\client\Tests\XServer.Client.Tests\XServer.Client.Tests.csproj', '-m:1', '-c', 'Debug')
}
finally {
    Pop-Location
}
