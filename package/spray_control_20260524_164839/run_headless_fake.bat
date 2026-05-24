@echo off
cd /d "%~dp0"
"%~dp0spray_control.exe" --headless --simulate-robot --config "%~dp0config\default.toml"
