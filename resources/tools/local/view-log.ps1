param(
    [string]$Module,
    [string]$Category,
    [int]$Tail = 20,
    [switch]$Watch,
    [switch]$List,
    [switch]$Help
)

$root = "D:\Project"
$logDir = "$root\Log"

if ($Help) {
    Write-Host @"
Usage: .\view-log.ps1 [options]

Options:
  -Module <name>    Show module log (e.g. RomSeparator, Template)
  -Category <name>  Show general log category (e.g. Main, Warning)
  -Tail <n>         Last N lines (default 20)
  -Watch            Continuously watch log (Get-Content -Wait)
  -List             List available logs
  -Help             Show this help

Examples:
  .\view-log.ps1 -Module RomSeparator
  .\view-log.ps1 -Module RomSeparator -Tail 50
  .\view-log.ps1 -Module RomSeparator -Watch
  .\view-log.ps1 -Category Main
  .\view-log.ps1 -List
"@
    return
}

if ($List) {
    Write-Host "Available logs:" -ForegroundColor Cyan
    if (Test-Path $logDir) {
        Get-ChildItem "$logDir\*.txt" | ForEach-Object { Write-Host "  $($_.Name)" }
    } else {
        Write-Host "  (no logs yet)"
    }
    return
}

$logFile = if ($Module) {
    "$logDir\Log_$Module.txt"
} elseif ($Category) {
    "$logDir\$Category.txt"
} else {
    Write-Host "Use -Module or -Category. See -Help." -ForegroundColor Yellow
    return
}

if (-not (Test-Path $logFile)) {
    Write-Host "Log not found: $logFile" -ForegroundColor Red
    exit 1
}

Write-Host "=== $logFile ===" -ForegroundColor Cyan
if ($Watch) {
    Get-Content $logFile -Wait -Tail $Tail
} else {
    Get-Content $logFile -Tail $Tail
}
