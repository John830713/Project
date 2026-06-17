@echo off
setlocal

set "TARGET_DIR=%~dp0"
powershell -NoProfile -Command "Start-Process cmd.exe -Verb RunAs -ArgumentList '/k cd /d ""%TARGET_DIR%""'"

endlocal
exit /b