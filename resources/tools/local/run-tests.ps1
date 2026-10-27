param(
    [string]$Filter,
    [switch]$Rebuild,
    [switch]$Help
)

$root = "D:\Project"
$toolchain = "D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin"

# Test executables as produced by the Makefile's `test` target.
# Keep this list in sync with TEST_TARGETS in the Makefile.
$tests = [ordered]@{
    AutoKey      = "Debug/AutoKey/AutoKeyTest.exe"
    Tooltip      = "Debug/UI/TooltipTest.exe"
    DebugState   = "Debug/Core/DebugStateTest.exe"
    MenuOrder    = "Debug/Core/MenuOrderTest.exe"
    UpdateManager= "Debug/Core/UpdateManagerTest.exe"
    Clipboard    = "Debug/RemoteControl/ClipboardTest.exe"
    Disconnect   = "Debug/RemoteControl/DisconnectTest.exe"
    Diagnostics  = "Debug/RemoteControl/DiagnosticsTest.exe"
    WebAgent     = "Debug/WebAgent/WebAgentTest.exe"
}

if ($Help) {
    Write-Host @"
Usage: .\run-tests.ps1 [options]

Options:
  -Filter <name>   Run only matching tests (AutoKey, Tooltip, DebugState,
                   MenuOrder, UpdateManager, Clipboard, Disconnect, Diagnostics,
                   WebAgent)
  -Rebuild         Rebuild test executables first (mingw32-make SHELL=cmd.exe test)
  -Help            Show this help

Examples:
  .\run-tests.ps1                          # Run all tests
  .\run-tests.ps1 -Filter RemoteControl    # no match -> lists available names
  .\run-tests.ps1 -Filter Clipboard        # Run only ClipboardTest
  .\run-tests.ps1 -Rebuild                 # Rebuild + run all
"@
    return
}

# The test exes are statically linked but need ucrt64 on PATH to start,
# and make needs it to find g++/windres.
$env:PATH = "$toolchain;$env:PATH"

Push-Location $root

if ($Rebuild) {
    Write-Host "=== Rebuilding tests ===" -ForegroundColor Cyan
    # The `test` recipe uses cmd.exe syntax (for %%t in ...), so make must use
    # cmd.exe as its shell; PowerShell otherwise hands it the MSYS sh.
    & mingw32-make SHELL=cmd.exe clean
    & mingw32-make SHELL=cmd.exe test
    Pop-Location
    return
}

$names = if ($Filter) {
    $matched = $tests.Keys | Where-Object { $_ -like "*$Filter*" }
    if (-not $matched) {
        Write-Host "No test matches filter '$Filter'. Available: $($tests.Keys -join ', ')" -ForegroundColor Red
        Pop-Location; exit 1
    }
    $matched
} else {
    $tests.Keys
}

Write-Host "=== Running tests ===" -ForegroundColor Cyan
$totalPass = $true
foreach ($n in $names) {
    $t = $tests[$n]
    if (-not (Test-Path $t)) {
        Write-Host "Missing: $t - build first with -Rebuild" -ForegroundColor Yellow
        $totalPass = $false
        continue
    }
    Write-Host "--- $n ($t) ---" -ForegroundColor Cyan
    & ".\$t"
    if ($LASTEXITCODE -ne 0) { $totalPass = $false }
}

if ($totalPass) {
    Write-Host "All tests passed." -ForegroundColor Green
} else {
    Write-Host "Some tests FAILED." -ForegroundColor Red
}

Pop-Location
