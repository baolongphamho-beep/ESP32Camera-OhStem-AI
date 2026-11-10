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
    idf.py -D IDF_TARGET=esp32s3 build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Flash) {
        if (!$Port) { throw 'Specify -Port COMx to flash the YOLO UNO (e.g. -Port COM9).' }
        idf.py -p $Port flash
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
