@echo off
cd /d "%~dp0"
start "spray_control" "%~dp0spray_control.exe" --config "%~dp0config\default.toml"
