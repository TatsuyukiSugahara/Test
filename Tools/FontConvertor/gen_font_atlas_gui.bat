@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0gen_font_atlas_gui.ps1"
if errorlevel 1 (
    echo.
    echo [ERROR] Failed to run PowerShell script.
    echo Path: %~dp0gen_font_atlas_gui.ps1
    pause
)
