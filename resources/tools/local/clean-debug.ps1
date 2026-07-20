param([switch]$Help)

$root = "D:\Project"

if ($Help) {
    Write-Host @"
Usage: .\clean-debug.ps1

Cleans: build artifacts, logs, temp files, JSON config, debug_log.txt
Does NOT clean Config/*.ini (module configs) or GeneratedBuild.mk.
"@
    return
}

Push-Location $root

Write-Host "=== Cleaning debug artifacts ===" -ForegroundColor Cyan

# Build artifacts
Write-Host "  Build..." -NoNewline
& mingw32-make clean 2>$null
Write-Host " done" -ForegroundColor Green

# Logs
$logDir = "$root\Log"
if (Test-Path $logDir) {
    Write-Host "  Logs..." -NoNewline
    Remove-Item "$logDir\*" -Recurse -Force 2>$null
    Write-Host " done" -ForegroundColor Green
}

# debug_log.txt
if (Test-Path "$root\debug_log.txt") {
    Write-Host "  debug_log.txt..." -NoNewline
    Remove-Item "$root\debug_log.txt" -Force
    Write-Host " done" -ForegroundColor Green
}

# JSON loader config
$configPath = "$root\Config\Config_Loader.json"
if (Test-Path $configPath) {
    Write-Host "  Config_Loader.json..." -NoNewline
    Remove-Item $configPath -Force
    Write-Host " done" -ForegroundColor Green
}

# .o and .d files
Write-Host "  Object files..." -NoNewline
Get-ChildItem -Recurse -Filter "*.o" -Path $root | Remove-Item -Force 2>$null
Get-ChildItem -Recurse -Filter "*.d" -Path $root | Remove-Item -Force 2>$null
Write-Host " done" -ForegroundColor Green

Write-Host "Clean complete." -ForegroundColor Green
Pop-Location
