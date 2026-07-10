@echo off
setlocal EnableDelayedExpansion

cd /d "%~dp0"

echo.
echo ========================================
echo Project Build Start
echo Root: %CD%
echo ========================================
echo.

set "MAKE_CMD="
set "BUILD_MODE=%~1"

REM =========================================================
REM 1. Read custom make path from MakePath.txt
REM =========================================================
if exist "MakePath.txt" (
    set /p MAKE_CMD=<MakePath.txt
)

REM =========================================================
REM 2. Search in PATH
REM =========================================================
if not defined MAKE_CMD (
    where mingw32-make >nul 2>&1
    if not errorlevel 1 set "MAKE_CMD=mingw32-make"
)

if not defined MAKE_CMD (
    where make >nul 2>&1
    if not errorlevel 1 set "MAKE_CMD=make"
)

if not defined MAKE_CMD (
    where gmake >nul 2>&1
    if not errorlevel 1 set "MAKE_CMD=gmake"
)

REM =========================================================
REM 3. Search common MSYS2 locations
REM =========================================================
if not defined MAKE_CMD if exist "C:\msys64\ucrt64\bin\mingw32-make.exe" set "MAKE_CMD=C:\msys64\ucrt64\bin\mingw32-make.exe"
if not defined MAKE_CMD if exist "C:\msys64\mingw64\bin\mingw32-make.exe" set "MAKE_CMD=C:\msys64\mingw64\bin\mingw32-make.exe"
if not defined MAKE_CMD if exist "C:\msys64\usr\bin\make.exe" set "MAKE_CMD=C:\msys64\usr\bin\make.exe"

if not defined MAKE_CMD (
    echo [ERROR] No build tool found.
    echo.
    echo Checked:
    echo   - MakePath.txt
    echo   - mingw32-make
    echo   - make
    echo   - gmake
    echo   - C:\msys64\ucrt64\bin\mingw32-make.exe
    echo   - C:\msys64\mingw64\bin\mingw32-make.exe
    echo   - C:\msys64\usr\bin\make.exe
    echo.
    echo Please install make or provide MakePath.txt.
    pause
    exit /b 1
)

echo [INFO] Using build tool: %MAKE_CMD%

REM =========================================================
REM 4. Prepend toolchain bin directory to PATH so that g++,
REM    windres, etc. match the selected make's toolchain
REM =========================================================
for %%i in ("%MAKE_CMD%") do set "TOOLCHAIN_BIN=%%~dpi"
if defined TOOLCHAIN_BIN (
    set "PATH=%TOOLCHAIN_BIN%;%PATH%"
    echo [INFO] Toolchain PATH: %TOOLCHAIN_BIN%
)

echo.
echo [INFO] Cleaning project...
"%MAKE_CMD%" clean

echo.
echo [INFO] Generating build files...
py Build.py
if errorlevel 1 (
    echo.
    echo [ERROR] Build generation failed.
    pause
    exit /b 1
)

set "MAKE_FLAGS="
if /I "%BUILD_MODE%"=="-debug" set "MAKE_FLAGS=CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1"
if /I "%BUILD_MODE%"=="debug" set "MAKE_FLAGS=CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1"

if defined MAKE_FLAGS (
    echo [INFO] DEBUG build mode: AllocConsole + DBG traces enabled
) else (
    echo [INFO] RELEASE build mode
)

echo.
echo [INFO] Building project...
"%MAKE_CMD%" %MAKE_FLAGS%
set BUILD_RC=%ERRORLEVEL%

if not "%BUILD_RC%"=="0" (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b %BUILD_RC%
)

if /I "%BUILD_MODE%"=="trim" (
    echo.
    echo [INFO] Trimming intermediate files...
    "%MAKE_CMD%" trim
)

echo.
echo ========================================
echo Build successful.
echo ========================================
pause
exit /b 0