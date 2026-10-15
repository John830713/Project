param(
    [string]$Module,
    [switch]$Run,
    [switch]$Help
)

$root = "D:\Project"
$toolchain = "D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin"

if ($Help) {
    Write-Host @"
Usage: .\debug-build.ps1 [options]

Options:
  -Module <name>   Only build/load specific module (sets JSON config)
  -Run             Build then launch the exe
  -Help            Show this help

Examples:
  .\debug-build.ps1                         # Clean debug build all modules
  .\debug-build.ps1 -Module Template         # Build debug + select only Template
  .\debug-build.ps1 -Module RomSeparator -Run  # Build, select, launch
"@
    return
}

# make needs the toolchain on PATH, and the `clean` recipe uses cmd.exe syntax
# (`del`); without SHELL=cmd.exe make runs the MSYS sh, `del` fails with 127 and
# the recipe is a silent no-op. Force cmd.exe so the clean is real.
$env:PATH = "$toolchain;$env:PATH"
$makeArgs = @("SHELL=cmd.exe")

Push-Location $root

if ($Module) {
    & "$PSScriptRoot\select-modules.ps1" -Modules $Module
}

Write-Host "=== Debug build ===" -ForegroundColor Cyan
& mingw32-make @makeArgs clean
if ($LASTEXITCODE -ne 0) { Pop-Location; exit 1 }

& mingw32-make @makeArgs CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1
if ($LASTEXITCODE -ne 0) { Pop-Location; exit 1 }

Write-Host "Build complete." -ForegroundColor Green

if ($Run) {
    Write-Host "Launching Project.exe (debug mode)..." -ForegroundColor Cyan
    Start-Process -FilePath ".\Project.exe" -WindowStyle Normal
}

Pop-Location
