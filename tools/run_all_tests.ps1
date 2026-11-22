# TV5-5: run every PC test (C++ g++ + Python unittest) and print one PASS/FAIL table.
#   powershell -ExecutionPolicy Bypass -File tools\run_all_tests.ps1            # PC tests
#   powershell -ExecutionPolicy Bypass -File tools\run_all_tests.ps1 -Firmware  # + PlatformIO builds
# Exit code 0 only when nothing failed. Nothing here touches the board or the camera.
param([switch]$Firmware)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force .pio | Out-Null
$results = New-Object System.Collections.Generic.List[object]

function Add-Result($name, $status, $detail) {
    $results.Add([pscustomobject]@{ Test = $name; Status = $status; Detail = $detail })
    $color = @{ PASS = 'Green'; FAIL = 'Red'; SKIP = 'Yellow' }[$status]
    Write-Host ("[{0}] {1} {2}" -f $status, $name, $detail) -ForegroundColor $color
}

function Invoke-Native([string]$exe, [string[]]$argList) {
    # Returns @(exitCode, lastOutputLine) without turning stderr into PowerShell errors.
    $out = & $exe @argList 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $last = ($out | Where-Object { $_.Trim() } | Select-Object -Last 1)
    return @($code, $last, ($out -join "`n"))
}

# ---------------------------------------------------------------- C++ (g++)
$gpp = Get-Command g++ -ErrorAction SilentlyContinue
$lcd = @('-I', 'tests/lcd_stubs', '-I', 'variants/yolo_uno', '-I', 'lib/Config', '-I', 'lib/Measurements', '-I', 'lib/Display')
$cpp = @(
    @{ n = 'measurement_state'; src = 'tests/measurement_state_test.cpp'; a = @('-I', 'lib/Measurements') },
    @{ n = 'scale_presence'; src = 'tests/scale_presence_test.cpp'; a = @('-I', 'lib/Measurements') },
    @{ n = 'pedestrian_state';  src = 'tests/pedestrian_state_test.cpp';  a = @('-I', 'lib/Measurements') },
    @{ n = 'scale_power';       src = 'tests/scale_power_test.cpp';       a = @('-I', 'lib/ScalePower') },
    @{ n = 'edge_sweep_logic';  src = 'tests/edge_sweep_logic_test.cpp';       a = @('-I', 'lib/EdgeSweep') },
    @{ n = 'edge_sweep_fsm';    src = 'tests/edge_sweep_fsm_test.cpp';         a = @('-I', 'lib/EdgeSweep', '-I', 'tests') },
    @{ n = 'servo_motion';      src = 'tests/servo_motion_test.cpp';           a = @('-I', 'lib/EdgeSweep') },
    @{ n = 'display_camera_lcd'; src = 'tests/display_probe_test.cpp';
       a = @('-DLCD_ENABLED=1', '-DCAMERA_ENABLED=1', '-DSCALE_ENABLE_QR=0', '-DSCALE_ENABLE_FORCE=0',
             '-DCAMERA_RX_PIN=9', '-DCAMERA_TX_PIN=8') + $lcd },
    @{ n = 'display_servo_sweep'; src = 'tests/display_probe_test.cpp';
       a = @('-DLCD_ENABLED=1', '-DSERVO_SWEEP_MODE=1', '-DSCALE_ENABLE_QR=0', '-DSCALE_ENABLE_FORCE=0') + $lcd },
    @{ n = 'edge_geometry'; src = 'tests/edge_geometry_test.cpp'; a = @();
       needs = 'firmware/edge_yolo/main/edge_geometry.h' }
    @{ n = 'edge_native_plan2'; src = 'tests/edge_native_plan2_test.cpp'; a = @() }
    @{ n = 'http_capture_parser'; src = 'tests/http_capture_parser_test.cpp'; a = @() }
    @{ n = 'wifi_reconnect';    src = 'tests/wifi_reconnect_test.cpp';    a = @('-I', 'firmware/edge_yolo/main') }
    @{ n = 'wifi_credentials';  src = 'tests/wifi_credentials_test.cpp';  a = @('-I', 'firmware/edge_yolo/main') }
    @{ n = 'ble_scan_retry';    src = 'tests/ble_scan_retry_test.cpp';    a = @('-I', 'firmware/edge_yolo/main') }
    @{ n = 'servo_math (GitHub)'; src = 'tests/servo_ctrl_test.cpp'; a = @(); needs = 'firmware/edge_yolo/main/servo_math.h' }
    @{ n = 'standby_schedule (GitHub)'; src = 'tests/standby_schedule_test.cpp'; a = @('-I', 'firmware/edge_yolo/main'); needs = 'firmware/edge_yolo/main/standby_schedule.h' }
)
foreach ($t in $cpp) {
    $name = "C++ $($t.n)"
    if (-not $gpp) { Add-Result $name 'SKIP' 'g++ not found in PATH'; continue }
    if ($t.needs -and -not (Test-Path $t.needs)) { Add-Result $name 'SKIP' "missing $($t.needs)"; continue }
    $exe = ".pio/qa_$($t.n).exe"
    $r = Invoke-Native 'g++' (@('-std=c++17') + $t.a + @($t.src, '-o', $exe))
    if ($r[0] -ne 0) { Add-Result $name 'FAIL' 'compile error'; Write-Host $r[2]; continue }
    $r = Invoke-Native $exe @()
    if ($r[0] -eq 0) { Add-Result $name 'PASS' $r[1] } else { Add-Result $name 'FAIL' "exit $($r[0]): $($r[1])"; Write-Host $r[2] }
}

# ---------------------------------------------------------------- Python
$py = $null; $pyArgs = @()
if (Test-Path '.venv-camera/Scripts/python.exe') { $py = (Resolve-Path '.venv-camera/Scripts/python.exe').Path }
elseif (Get-Command py -ErrorAction SilentlyContinue) { $py = 'py'; $pyArgs = @('-3') }
elseif (Test-Path "$env:USERPROFILE/.platformio/penv/Scripts/python.exe") {
    $py = "$env:USERPROFILE/.platformio/penv/Scripts/python.exe"
}
elseif (Get-Command python -ErrorAction SilentlyContinue) { $py = 'python' }

foreach ($suite in @(@{ n = 'tests'; d = 'tests' }, @{ n = 'integration'; d = 'integration' })) {
    $name = "Python $($suite.n)"
    if (-not $py) { Add-Result $name 'SKIP' 'no .venv-camera and no py launcher'; continue }
    $r = Invoke-Native $py ($pyArgs + @('-m', 'unittest', 'discover', '-s', $suite.d, '-p', 'test_*.py'))
    $summary = ($r[2] -split "`n" | Where-Object { $_ -match '^Ran \d+ tests' } | Select-Object -Last 1)
    if ($r[0] -eq 0) { Add-Result $name 'PASS' "$summary, OK" } else { Add-Result $name 'FAIL' "$summary, see output above"; Write-Host $r[2] }
}

# ---------------------------------------------------------------- QA log pipeline (sim -> checker)
if ($gpp -and $py) {
    $r = Invoke-Native 'g++' @('-std=c++17', '-I', 'lib/EdgeSweep', '-I', 'tests', 'tests/qa_log_sim.cpp', '-o', '.pio/qa_log_sim.exe')
    if ($r[0] -ne 0) { Add-Result 'QA log sim -> checker' 'FAIL' 'compile error'; Write-Host $r[2] }
    else {
        & .\.pio\qa_log_sim.exe | Out-File -Encoding utf8 .pio/qa_sim.log
        if ($LASTEXITCODE -ne 0) { Add-Result 'QA log sim -> checker' 'FAIL' "simulator exit $LASTEXITCODE" }
        else {
            $r = Invoke-Native $py ($pyArgs + @('tools/qa_log_check.py', '.pio/qa_sim.log', '--csv', '.pio/qa_sim.csv'))
            $na = ($r[2] -split "`n" | Where-Object { $_ -match '^\[N/A' }).Count
            if ($r[0] -eq 0 -and $na -eq 0) { Add-Result 'QA log sim -> checker' 'PASS' 'all 6 QA checks PASS on simulated log' }
            else { Add-Result 'QA log sim -> checker' 'FAIL' "exit $($r[0]), $na N/A"; Write-Host $r[2] }
        }
    }
} else { Add-Result 'QA log sim -> checker' 'SKIP' 'needs g++ and Python' }

# ---------------------------------------------------------------- Firmware builds (optional, slow)
if ($Firmware) {
    $pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
    $envs = Select-String -Path platformio.ini -Pattern '^\[env:(.+)\]' | ForEach-Object { $_.Matches[0].Groups[1].Value }
    foreach ($e in $envs) {
        if (-not (Test-Path $pio)) { Add-Result "build $e" 'SKIP' 'PlatformIO not installed'; continue }
        $r = Invoke-Native $pio @('run', '-e', $e)
        if ($r[0] -eq 0) { Add-Result "build $e" 'PASS' '' } else { Add-Result "build $e" 'FAIL' $r[1] }
    }
}

# ---------------------------------------------------------------- Summary
$fail = @($results | Where-Object Status -eq 'FAIL').Count
$pass = @($results | Where-Object Status -eq 'PASS').Count
$skip = @($results | Where-Object Status -eq 'SKIP').Count
Write-Host ""
$results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
Write-Host ("TOTAL: {0} PASS, {1} FAIL, {2} SKIP  ({3})" -f $pass, $fail, $skip, (Get-Date -Format 'yyyy-MM-dd HH:mm'))
if ($fail -gt 0) { exit 1 } else { exit 0 }
