@echo off
rem ---------------------------------------------------------------------------
rem BuildUI.bat - command-line entry point for the checkbox build tool.
rem
rem A .bat always flashes a console window, so all the launching logic lives in
rem BuildUI.vbs (wscript = no console). Double-click BuildUI.vbs directly if
rem you want zero flicker; use "py BuildUI.py" to keep a console for tracebacks.
rem ---------------------------------------------------------------------------
start "" wscript //nologo "%~dp0BuildUI.vbs"
exit /b 0
