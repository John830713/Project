@echo off
rem ---------------------------------------------------------------------------
rem BuildUI.bat - launch the checkbox build tool without a console window.
rem
rem Prefers the windowless launchers (pyw / pythonw) so the only thing on screen
rem is the Tk window. Falls back to "py" (console kept) when neither exists,
rem which is also the way to see tracebacks while debugging BuildUI.py.
rem ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

where pyw >nul 2>&1
if not errorlevel 1 (
    start "" pyw "%~dp0BuildUI.py"
    exit /b 0
)

where pythonw >nul 2>&1
if not errorlevel 1 (
    start "" pythonw "%~dp0BuildUI.py"
    exit /b 0
)

echo pythonw.exe / pyw.exe not found - running with a console window instead.
py "%~dp0BuildUI.py"
if errorlevel 1 pause
