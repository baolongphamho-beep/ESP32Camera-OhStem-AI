param(
    [string]$EspressifRoot,
    [string]$IDFPath,
    [string]$Port,
    [switch]$Flash
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
            $IDFPath = Join-Path $EspressifRoot 'esp-idf'
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

. $exportScript
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

    # Ensure sdkconfig includes the 240 MHz CPU and 32 KB lwIP TCP window defaults.
    $sdkconfigFile = Join-Path $PSScriptRoot 'sdkconfig'
    if (Test-Path -LiteralPath $sdkconfigFile) {
        $sdkText = Get-Content -LiteralPath $sdkconfigFile -Raw
        if ($sdkText -notmatch 'CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y' -or
            $sdkText -notmatch 'CONFIG_LWIP_TCP_WND_DEFAULT=32768') {
            Write-Host "[SmartScale] Updating sdkconfig from sdkconfig.defaults..." -ForegroundColor Yellow
            Remove-Item -LiteralPath $sdkconfigFile -Force
        }
    }

    idf.py -D IDF_TARGET=esp32s3 build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Flash) {
        if (!$Port) { throw 'Specify -Port COMx to flash the YOLO UNO (e.g. -Port COM9).' }
        idf.py -p $Port flash
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
