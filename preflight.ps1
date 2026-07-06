# preflight.ps1 - DWM pipeline pre-flight checks
# Run before the build -> export -> open loop (see RUNBOOK.md §1).
# Usage:  powershell -ExecutionPolicy Bypass -File .\preflight.ps1

# ---- paths (adjust here if the layout changes) ----
$UProject   = "C:\DreamWorldMaker\Repos\DWM_Dev\DWM_Dev.uproject"
$BuildCs    = "C:\DreamWorldMaker\Repos\DWM_Dev\Source\DWM_Dev\DWM_Dev.Build.cs"
$EnginePath = "C:\Program Files\Epic Games\UE_5.3"
$UBT        = Join-Path $EnginePath "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
$ModuleDll  = "C:\DreamWorldMaker\Repos\DWM_Dev\Binaries\Win64\UnrealEditor-DWM_Dev.dll"

$fail = 0; $warn = 0
function Pass($m) { Write-Host "  [PASS] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  [WARN] $m" -ForegroundColor Yellow; $script:warn++ }
function Fail($m) { Write-Host "  [FAIL] $m" -ForegroundColor Red;    $script:fail++ }

Write-Host "`n=== DWM pre-flight ===" -ForegroundColor Cyan

# 1. DWMStudio must NOT be running (it holds the SQLite file handle)
if (Get-Process -Name "DWMStudio" -ErrorAction SilentlyContinue) {
  Fail "DWMStudio is RUNNING - it holds the .db handle; UE will throw disk I/O error. Close it."
} else { Pass "DWMStudio is not running (file handle free)." }

# 2. Warn if an Unreal Editor is already up (stale editors caused ghost failures)
if (Get-Process -Name "UnrealEditor" -ErrorAction SilentlyContinue) {
  Warn "An UnrealEditor process is already running - stale editors hold DLLs and cause 'module could not be loaded'. Close it before building."
} else { Pass "No Unreal Editor process running." }

# 3. .uproject exists, engine 5.3, required plugins enabled, USQLite disabled
if (-not (Test-Path $UProject)) { Fail ".uproject not found at $UProject" }
else {
  try {
    $up = Get-Content $UProject -Raw | ConvertFrom-Json
    if ($up.EngineAssociation -eq "5.3") { Pass "EngineAssociation is 5.3." }
    else { Fail "EngineAssociation is '$($up.EngineAssociation)' - must be 5.3 (MATLAB R2025b co-sim requirement)." }

    foreach ($name in @("SQLiteCore","SQLiteSupport")) {
      $p = $up.Plugins | Where-Object { $_.Name -eq $name }
      if ($p -and $p.Enabled) { Pass "Plugin $name enabled in .uproject." }
      else { Fail "Plugin $name NOT enabled in .uproject - module builds but will NOT load ('plugin included in the build has not been turned on')." }
    }
    $usq = $up.Plugins | Where-Object { $_.Name -eq "USQLite" }
    if ($usq -and $usq.Enabled) { Warn "USQLite plugin is ENABLED - project convention is built-in SQLiteCore; USQLite should be disabled." }
    else { Pass "USQLite disabled (built-in SQLiteCore path is authoritative)." }
  } catch { Fail ".uproject could not be parsed as JSON: $($_.Exception.Message)" }
}

# 4. Build.cs lists the SQLite modules
if (-not (Test-Path $BuildCs)) { Fail "Build.cs not found at $BuildCs" }
else {
  $bc = Get-Content $BuildCs -Raw
  foreach ($m in @("SQLiteCore","SQLiteSupport")) {
    if ($bc -match $m) { Pass "Build.cs references $m." }
    else { Fail "Build.cs does not reference $m - the reader code will not compile/link." }
  }
}

# 5. Engine + UnrealBuildTool present (a missing UBT broke everything once)
if (Test-Path $UBT) { Pass "UnrealBuildTool.exe present." }
else { Fail "UnrealBuildTool.exe MISSING at $UBT - run Epic Launcher > UE 5.3 > Verify." }

# 6. .NET 8 SDK
$sdks = & dotnet --list-sdks 2>$null
if ($LASTEXITCODE -eq 0 -and ($sdks -match "^8\.")) { Pass ".NET 8 SDK found." }
elseif ($sdks) { Warn ".NET SDK found but no 8.x line: $($sdks -join '; ')" }
else { Fail "dotnet not on PATH - DWMStudio build will fail." }

# 7. Module DLL freshness (informational)
if (Test-Path $ModuleDll) {
  $age = (Get-Date) - (Get-Item $ModuleDll).LastWriteTime
  if ($age.TotalHours -lt 24) { Pass ("UnrealEditor-DWM_Dev.dll present ({0:N1}h old)." -f $age.TotalHours) }
  else { Warn ("UnrealEditor-DWM_Dev.dll is {0:N0}h old - rebuild before trusting it." -f $age.TotalHours) }
} else { Warn "UnrealEditor-DWM_Dev.dll not built yet - build DWM_Dev (Development Editor / Win64) before launching." }

# ---- summary ----
Write-Host ""
if ($fail -gt 0)      { Write-Host "RESULT: $fail FAIL / $warn warn - fix FAILs before running the pipeline (see RUNBOOK.md §4)." -ForegroundColor Red; exit 1 }
elseif ($warn -gt 0)  { Write-Host "RESULT: clear to proceed with $warn warning(s)." -ForegroundColor Yellow; exit 0 }
else                  { Write-Host "RESULT: all checks passed - clear for build > export > open." -ForegroundColor Green; exit 0 }
