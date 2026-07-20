param(
    [string]$Modules,
    [switch]$All,
    [switch]$List,
    [switch]$Status,
    [switch]$Help
)

$root = "D:\Project"
$configPath = "$root\Config\Config_Loader.json"
$modulesDir = "$root\Modules"

function Get-AvailableModules {
    Get-ChildItem "$modulesDir\*" -Directory | Where-Object {
        (Get-ChildItem "$($_.FullName)\*.module.ini" -ErrorAction SilentlyContinue).Count -gt 0
    } | ForEach-Object { $_.Name } | Sort-Object
}

function Write-JsonConfig {
    param([string[]]$ModuleList, [string]$Mode)
    $body = @{
        mode = $Mode
        modules = $ModuleList
    } | ConvertTo-Json
    $body | Set-Content -Encoding ASCII $configPath
    Write-Host "Config saved: $configPath" -ForegroundColor Green
}

if ($Help) {
    Write-Host @"
Usage: .\select-modules.ps1 [options]

Options:
  -Modules <list>    Comma-separated module names to enable
                     Example: -Modules Template,RomSeparator
  -All               Reset to all modules (delete config)
  -List              Show available module names
  -Status            Show current config
  -Help              Show this help

Examples:
  .\select-modules.ps1 -Modules Template
  .\select-modules.ps1 -Modules Template,RomSeparator,AutoKey
  .\select-modules.ps1 -All
  .\select-modules.ps1 -List
"@
    return
}

if ($List) {
    Write-Host "Available modules:" -ForegroundColor Cyan
    Get-AvailableModules | ForEach-Object { Write-Host "  $_" }
    return
}

if ($Status) {
    if (Test-Path $configPath) {
        Write-Host "Current config ($configPath):" -ForegroundColor Cyan
        Get-Content $configPath
    } else {
        Write-Host "No config file. All 7 modules are loaded (default)." -ForegroundColor Yellow
    }
    return
}

if ($All) {
    if (Test-Path $configPath) {
        Remove-Item $configPath
        Write-Host "Config deleted. All modules will load on next start." -ForegroundColor Green
    } else {
        Write-Host "No config to delete. All modules already load by default." -ForegroundColor Yellow
    }
    return
}

if ($Modules) {
    $selected = $Modules.Split(',', [StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object { $_.Trim() }
    $available = Get-AvailableModules
    $unknown = $selected | Where-Object { $_ -notin $available }
    if ($unknown) {
        Write-Host "Unknown modules: $($unknown -join ', ')" -ForegroundColor Red
        Write-Host "Available: $($available -join ', ')" -ForegroundColor Yellow
        exit 1
    }
    Write-JsonConfig -ModuleList $selected -Mode "pet"
    Write-Host "Next start will load: $($selected -join ', ')" -ForegroundColor Cyan
    return
}

# No args
Write-Host "Use -Help for usage." -ForegroundColor Yellow
