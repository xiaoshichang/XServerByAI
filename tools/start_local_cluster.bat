@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "DEFAULT_CONFIG=%SCRIPT_DIR%..\configs\local-dev.json"

python "%SCRIPT_DIR%cluster_ctl.py" start --config "%DEFAULT_CONFIG%" %*
