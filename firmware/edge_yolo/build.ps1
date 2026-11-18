param(
    [string]$EspressifRoot,
    [string]$IDFPath,
    [string]$Port,
    [switch]$Flash,
    [switch]$ScaleOnly,
    [switch]$CameraContinuous,
    [ValidateSet(10, 25, 50, 100)]
    [int]$StandbyScanPercent = 10
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
# EIM installations use a version-specific profile with their tool paths.
$versionName = Split-Path (Split-Path $IDFPath -Parent) -Leaf
$eimProfile = Join-Path $EspressifRoot "tools/Microsoft.$versionName.PowerShell_profile.ps1"
if (Test-Path -LiteralPath $eimProfile) {
    . $eimProfile
} else {
    . $exportScript
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

    # Migrate only the PM and camera-performance settings required by this
    # integration. Preserve other generated settings rather than deleting them.
    $pmKeys = @('CONFIG_PM_ENABLE', 'CONFIG_PM_PROFILING',
        'CONFIG_FREERTOS_USE_TICKLESS_IDLE', 'CONFIG_BT_CTRL_MODEM_SLEEP',
        'CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1', 'CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL',
        'CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP')
    $requiredValues = @{}
    foreach ($key in $pmKeys) { $requiredValues[$key] = 'y' }
    $cameraKeys = @('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240', 'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ',
        'CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM', 'CONFIG_ESP_WIFI_RX_BA_WIN',
        'CONFIG_LWIP_IRAM_OPTIMIZATION', 'CONFIG_LWIP_TCP_SND_BUF_DEFAULT',
        'CONFIG_LWIP_TCP_WND_DEFAULT', 'CONFIG_LWIP_TCP_RECVMBOX_SIZE')
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $PSScriptRoot 'sdkconfig.defaults'))) {
        if ($line -match '^(CONFIG_[A-Z0-9_]+)=(.+)$' -and $cameraKeys -contains $Matches[1]) {
            $requiredValues[$Matches[1]] = $Matches[2]
        }
    }
    $sdkconfigPath = Join-Path $PSScriptRoot 'sdkconfig'
    if (Test-Path -LiteralPath $sdkconfigPath) {
        $configText = [IO.File]::ReadAllText($sdkconfigPath)
        $originalText = $configText
        foreach ($key in $requiredValues.Keys) {
            $value = $requiredValues[$key]
            $pattern = '(?m)^(?:# ' + [regex]::Escape($key) + ' is not set|' + [regex]::Escape($key) + '=.*)\r?$'
            if ($configText -match $pattern) {
                $configText = [regex]::Replace($configText, $pattern, "$key=$value")
            } else {
                $configText += "`r`n$key=$value`r`n"
            }
        }
        if ($configText -ne $originalText) {
            [IO.File]::WriteAllText($sdkconfigPath, $configText, [Text.UTF8Encoding]::new($false))
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
