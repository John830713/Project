param(
    [string]$Filter,
    [switch]$Rebuild,
    [switch]$Help
)

$root = "D:\Project"
$tests = @{
    AutoKey    = "Tests/AutoKeyTest.exe"
    Tooltip    = "Tests/TooltipTest.exe"
    DebugState = "Tests/DebugStateTest.exe"
}

if ($Help) {
    Write-Host @"
Usage: .\run-tests.ps1 [options]

Options:
  -Filter <name>   Run only matching tests (AutoKey, Tooltip, DebugState)
  -Rebuild         Rebuild test executables first
  -Help            Show this help

Examples:
  .\run-tests.ps1                          # Run all tests
  .\run-tests.ps1 -Filter AutoKey          # Run only AutoKey test
  .\run-tests.ps1 -Rebuild                 # Rebuild + run all
"@
    return
}

Push-Location $root

if ($Rebuild) {
    Write-Host "=== Rebuilding tests ===" -ForegroundColor Cyan
    & mingw32-make clean
    & mingw32-make test
    Pop-Location
    return
}

$targets = if ($Filter) {
    $key = $tests.Keys | Where-Object { $_ -like "*$Filter*" }
    if (-not $key) {
        Write-Host "No test matches filter '$Filter'. Available: $($tests.Keys -join ', ')" -ForegroundColor Red
        Pop-Location; exit 1
    }
    $tests[$key]
} else {
    $tests.Values
}

Write-Host "=== Running tests ===" -ForegroundColor Cyan
$totalPass = $true
foreach ($t in $targets) {
    if (-not (Test-Path $t)) {
        Write-Host "Missing: $t — need to build first (use -Rebuild)" -ForegroundColor Yellow
        continue
    }
    Write-Host "--- $t ---" -ForegroundColor Cyan
    & ".\$t"
    if ($LASTEXITCODE -ne 0) { $totalPass = $false }
}

if ($totalPass) {
    Write-Host "All tests passed." -ForegroundColor Green
} else {
    Write-Host "Some tests FAILED." -ForegroundColor Red
}

Pop-Location
