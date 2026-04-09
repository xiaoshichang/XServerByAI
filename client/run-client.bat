@echo off
setlocal

pushd "%~dp0.."
dotnet run --project client/XServer.Client.App/XServer.Client.App.csproj --script client/demo.txt
set "EXITCODE=%ERRORLEVEL%"
popd

exit /b %EXITCODE%
