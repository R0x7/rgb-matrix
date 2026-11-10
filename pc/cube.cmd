@echo off
rem Runs cubectl.ps1 even when PowerShell scripts are blocked on this PC.
rem   pc\cube off | on | toggle | status | scene=lava | ...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0cubectl.ps1" %*
