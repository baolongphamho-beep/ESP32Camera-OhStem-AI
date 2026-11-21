param(
    [string]$EspressifRoot,
    [string]$IDFPath,
    [string]$Port,
    [switch]$Flash,
    [switch]$ScaleOnly,
    [switch]$CameraContinuous,
    [ValidateSet(10, 25, 50, 100)]
    [int]$StandbyScanPercent = 100
)
$ErrorActionPreference = 'Stop'
if ($ScaleOnly -and $CameraContinuous) { throw 'Choose either -ScaleOnly or -CameraContinuous.' }

# Auto-detect Espressif root if not explicitly provided
if (!$EspressifRoot) {
    $candidateRoots = @(
        'D:\Workspace\Espressif',
        'C:\Espressif',
        'D:\Espressif',
        'C:\Workspace\Espressif'
    )
    foreach ($cand in $candidateRoots) {
        if (Test-Path -LiteralPath $cand) {
            $EspressifRoot = $cand
            break
        }
    }
    if (!$EspressifRoot) {
        $EspressifRoot = 'D:\Workspace\Espressif'
    }
}

if (!$IDFPath) {
    if ($env:IDF_PATH -and (Test-Path -LiteralPath (Join-Path $env:IDF_PATH 'export.ps1'))) {
        $IDFPath = $env:IDF_PATH
    } else {
        $frameworkDir = Get-ChildItem -Path (Join-Path $EspressifRoot 'frameworks') -Directory -Filter 'esp-idf-*' -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
        if ($frameworkDir) {
            $IDFPath = $frameworkDir.FullName
        } else {
            $eimVersionDir = Get-ChildItem -LiteralPath $EspressifRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'esp-idf/export.ps1') } |
                Sort-Object Name -Descending | Select-Object -First 1
            if ($eimVersionDir) {
                $IDFPath = Join-Path $eimVersionDir.FullName 'esp-idf'
            } else {
                $IDFPath = Join-Path $EspressifRoot 'esp-idf'
            }
        }
    }
}

$exportScript = Join-Path $IDFPath 'export.ps1'
if (!(Test-Path -LiteralPath $exportScript)) {
    throw "ESP-IDF not found at $IDFPath. Please install ESP-IDF into $EspressifRoot or specify -EspressifRoot / -IDFPath."
}

if (!$env:IDF_TOOLS_PATH) {
    $env:IDF_TOOLS_PATH = Join-Path $EspressifRoot 'tools'
}

Write-Host "[SmartScale] Using ESP-IDF from: $IDFPath" -ForegroundColor Cyan
Write-Host "[SmartScale] Using IDF Tools from: $env:IDF_TOOLS_PATH" -ForegroundColor Cyan

$env:PYTHONUTF8 = '1'
# A viewer venv inherited by this process carries _OLD_VIRTUAL_PATH. EIM adds
# toolchain directories before Activate.ps1, whose deactivate step would then
# restore that old PATH and silently remove the compiler directories again.
# Restore it before loading EIM so its additions survive Python activation.
if (Test-Path Env:_OLD_VIRTUAL_PATH) {
    $env:PATH = $env:_OLD_VIRTUAL_PATH
    Remove-Item Env:_OLD_VIRTUAL_PATH
}
$versionName = Split-Path (Split-Path $IDFPath -Parent) -Leaf
$eimProfile = Join-Path $EspressifRoot "tools/Microsoft.$versionName.PowerShell_profile.ps1"
if (Test-Path -LiteralPath $eimProfile) {
    . $eimProfile
} else {
    . $exportScript
}
if (!(Get-Command xtensa-esp32s3-elf-gcc -ErrorAction SilentlyContinue)) {
    throw 'ESP32-S3 compiler is missing from PATH after ESP-IDF activation. Check the selected ESP-IDF toolchain installation.'
}
Push-Location $PSScriptRoot
try {
    # Ensure ESP-DL YOLO26 component sources exist (auto-bootstrap on a new PC).
    $yoloCandidates = @(
        (Join-Path $PSScriptRoot '../../.edge-deps/esp-dl/models/yolo26/CMakeLists.txt'),
        (Join-Path $PSScriptRoot '../../../.edge-deps/esp-dl/models/yolo26/CMakeLists.txt'),
        (Join-Path $PSScriptRoot '../../../../.edge-deps/esp-dl/models/yolo26/CMakeLists.txt')
    )
    $foundYolo = $false
    foreach ($c in $yoloCandidates) {
        if (Test-Path -LiteralPath $c) { $foundYolo = $true; break }
    }
    if (!$foundYolo) {
        Write-Host "[SmartScale] Bootstrapping ESP-DL sources for first-time setup..." -ForegroundColor Yellow
        & (Join-Path $PSScriptRoot 'tools/prepare_sources.ps1')
    }

    # If dependencies.lock references a local path from another machine, refresh it.
    $lockFile = Join-Path $PSScriptRoot 'dependencies.lock'
    if (Test-Path -LiteralPath $lockFile) {
        $lockText = Get-Content -LiteralPath $lockFile -Raw
        if ($lockText -match 'path:\s*([^\r\n]+)') {
            $lockedPath = $Matches[1].Trim()
            if (!(Test-Path -LiteralPath $lockedPath)) {
                Write-Host "[SmartScale] Refreshing dependencies.lock for this PC..." -ForegroundColor Yellow
                Remove-Item -LiteralPath $lockFile -Force
                $managedDir = Join-Path $PSScriptRoot 'managed_components'
                if (Test-Path -LiteralPath $managedDir) {
                    Remove-Item -LiteralPath $managedDir -Recurse -Force
                }
            }
        }
    }

    # Ensure sdkconfig includes the latest CPU, FreeRTOS tick, dual-core, cache, NimBLE, and lwIP defaults.
    $sdkconfigFile = Join-Path $PSScriptRoot 'sdkconfig'
    if (Test-Path -LiteralPath $sdkconfigFile) {
        $sdkText = Get-Content -LiteralPath $sdkconfigFile -Raw
        $requiredSdkFlags = @(
            'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y',
            'CONFIG_LWIP_TCP_WND_DEFAULT=32768',
            'CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y',
            'CONFIG_FREERTOS_HZ=1000',
            'CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y',
            'CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y',
            'CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y',
            'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096',
            'CONFIG_ESP_WIFI_RX_BA_WIN=10',
            'CONFIG_LWIP_SO_LINGER=y',
            'CONFIG_LWIP_MAX_ACTIVE_TCP=32',
            'CONFIG_LWIP_TCP_MSL=5000',
            'CONFIG_LWIP_TCP_FIN_WAIT_TIMEOUT=5000',
            'CONFIG_LWIP_TCP_SACK_OUT=y',
            'CONFIG_PM_ENABLE=y',
            'CONFIG_PM_PROFILING=y',
            'CONFIG_FREERTOS_USE_TICKLESS_IDLE=y',
            'CONFIG_BT_CTRL_MODEM_SLEEP=y',
            'CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1=y',
            'CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y',
            'CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y'
        )
        $forbiddenSdkFlags = @(
            'CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y',
            'CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB=y'
        )
        $needsRegen = $false
        foreach ($flag in $requiredSdkFlags) {
            if ($sdkText -notmatch [regex]::Escape($flag)) {
                $needsRegen = $true
                break
            }
        }
        if (!$needsRegen) {
            foreach ($flag in $forbiddenSdkFlags) {
                if ($sdkText -match [regex]::Escape($flag)) {
                    $needsRegen = $true
                    break
                }
            }
        }
        if ($needsRegen) {
            Write-Host "[SmartScale] Updating sdkconfig from sdkconfig.defaults..." -ForegroundColor Yellow
            Remove-Item -LiteralPath $sdkconfigFile -Force
        }
    }

    $scaleOnlyValue = if ($ScaleOnly) { 'ON' } else { 'OFF' }
    $continuousValue = if ($CameraContinuous) { 'ON' } else { 'OFF' }
    idf.py -D IDF_TARGET=esp32s3 -D "SMARTSCALE_SCALE_ONLY=$scaleOnlyValue" -D "SMARTSCALE_CAMERA_CONTINUOUS=$continuousValue" -D "SMARTSCALE_STANDBY_SCAN_PERCENT=$StandbyScanPercent" build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Flash) {
        if (!$Port) { throw 'Specify -Port COMx to flash the YOLO UNO (e.g. -Port COM9).' }
        idf.py -p $Port flash
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
