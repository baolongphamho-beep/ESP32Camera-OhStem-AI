param(
    [string]$EspressifRoot,
    [string]$IDFPath,
    [string]$Port,
    [switch]$Flash,
    [switch]$ScaleOnly,
    [ValidateSet(10, 25, 50, 100)]
    [int]$StandbyScanPercent = 10
)
$ErrorActionPreference = 'Stop'

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
    # Defaults do not override an existing sdkconfig. Migrate only the PM keys
    # this firmware requires, preserving every other generated setting.
    $pmKeys = @('CONFIG_PM_ENABLE', 'CONFIG_PM_PROFILING',
        'CONFIG_FREERTOS_USE_TICKLESS_IDLE', 'CONFIG_BT_CTRL_MODEM_SLEEP',
        'CONFIG_BT_CTRL_MODEM_SLEEP_MODE_1', 'CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL',
        'CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP')
    $sdkconfigPath = Join-Path $PSScriptRoot 'sdkconfig'
    if (Test-Path -LiteralPath $sdkconfigPath) {
        $configText = [IO.File]::ReadAllText($sdkconfigPath)
        $originalText = $configText
        foreach ($key in $pmKeys) {
            $pattern = '(?m)^(?:# ' + [regex]::Escape($key) + ' is not set|' + [regex]::Escape($key) + '=.*)\r?$'
            if ($configText -match $pattern) {
                $configText = [regex]::Replace($configText, $pattern, "$key=y")
            } else {
                $configText += "`r`n$key=y`r`n"
            }
        }
        if ($configText -ne $originalText) {
            [IO.File]::WriteAllText($sdkconfigPath, $configText, [Text.UTF8Encoding]::new($false))
        }
    }
    $scaleOnlyValue = if ($ScaleOnly) { 'ON' } else { 'OFF' }
    idf.py -D IDF_TARGET=esp32s3 -D "SMARTSCALE_SCALE_ONLY=$scaleOnlyValue" -D "SMARTSCALE_STANDBY_SCAN_PERCENT=$StandbyScanPercent" build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Flash) {
        if (!$Port) { throw 'Specify -Port COMx to flash the YOLO UNO (e.g. -Port COM9).' }
        idf.py -p $Port flash
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
