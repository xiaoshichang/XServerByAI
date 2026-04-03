@echo off
setlocal

pushd "%~dp0.."
dotnet run --project client/XServer.Client/XServer.Client.csproj --script client/demo.txt
set "EXITCODE=%ERRORLEVEL%"
popd

exit /b %EXITCODE%
