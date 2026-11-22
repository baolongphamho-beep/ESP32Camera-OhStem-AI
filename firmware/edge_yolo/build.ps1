param([string]$IDFPath, [string]$EspressifRoot = 'C:\Espressif', [string]$Port,
      [switch]$Flash, [string]$BuildRoot = 'C:\esp\SmartScale',
      [ValidateSet(160,192,224,256)][int]$ModelSize = 192)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
if (!$IDFPath) {
    $candidates = @($env:IDF_PATH, (Join-Path $EspressifRoot 'v6.1/esp-idf'),
                    (Join-Path $EspressifRoot 'esp-idf'))
    $frameworks = Join-Path $EspressifRoot 'frameworks'
    if (Test-Path -LiteralPath $frameworks) {
        $candidates += @(Get-ChildItem -LiteralPath $frameworks -Directory -Filter 'esp-idf-*' |
            Sort-Object Name -Descending | ForEach-Object FullName)
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'export.ps1'))) {
            $IDFPath = $candidate; break
        }
    }
}
if (!$IDFPath -or !(Test-Path -LiteralPath (Join-Path $IDFPath 'export.ps1'))) {
    throw 'ESP-IDF SDK not found. Install ESP-IDF 6.1, then pass -IDFPath <esp-idf folder>. PlatformIO/Arduino alone cannot build firmware/edge_yolo.'
}
if (!$env:IDF_TOOLS_PATH) {
    if (Test-Path -LiteralPath (Join-Path $EspressifRoot 'python_env')) {
        $env:IDF_TOOLS_PATH = $EspressifRoot
    } else { $env:IDF_TOOLS_PATH = Join-Path $EspressifRoot 'tools' }
}
$env:IDF_PATH = $IDFPath
$activation = Join-Path $env:IDF_TOOLS_PATH 'activate_idf_v6.1.ps1'
if (Test-Path -LiteralPath $activation) {
    . $activation
} else {
    # Use the SDK's own Python rather than an application .venv/MSYS/Store alias.
    $basePythonDir = Join-Path $EspressifRoot 'tools/python'
    if (Test-Path -LiteralPath (Join-Path $basePythonDir 'python.exe')) {
        $env:PATH = $basePythonDir + ';' + $env:PATH
    }
    $env:PATH = 'C:\Program Files\Git\cmd;' + $env:PATH
    . (Join-Path $IDFPath 'export.ps1')
}
$idfPython = if ($env:IDF_PYTHON_ENV_PATH) { Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe' } else { 'python' }
$idfScript = Join-Path $IDFPath 'tools/idf.py'
$model = Join-Path $PSScriptRoot "models/yolo26n_${ModelSize}_s8_s3.espdl"
if (!(Test-Path -LiteralPath $model)) { throw 'Model missing: run firmware/edge_yolo/tools/fetch_model.ps1.' }
$provenance = Get-Content -LiteralPath (Join-Path $PSScriptRoot "models/yolo26n_${ModelSize}_s8_s3.build.json") -Raw | ConvertFrom-Json
if ($provenance.input_size -ne $ModelSize -or $provenance.target -ne 'esp32s3') {
    throw 'Model metadata does not match the selected input size/target.'
}
if ((Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -ne $provenance.sha256) {
    throw 'Model hash differs from build.json; regenerate model/provenance together or run fetch_model.ps1.'
}
$projectDir = $PSScriptRoot
if ($projectDir -match '\s') {
    $stagedRoot = & (Join-Path $PSScriptRoot 'tools/stage_build.ps1') -BuildRoot $BuildRoot
    $projectDir = Join-Path $stagedRoot 'firmware/edge_yolo'
}
$nativeRoot = [System.IO.Path]::GetFullPath((Join-Path $projectDir '../..'))
$source = Join-Path $nativeRoot '.edge-deps/esp-dl/models/yolo26/CMakeLists.txt'
if (!(Test-Path -LiteralPath $source)) {
    & (Join-Path $PSScriptRoot 'tools/prepare_sources.ps1') -DestinationRoot $nativeRoot -SkipDataset
}
Write-Host "ESP-IDF project: $projectDir" -ForegroundColor Cyan
Push-Location $projectDir
try {
    & $idfPython $idfScript -D IDF_TARGET=esp32s3 -D "SMARTSCALE_MODEL_SIZE=$ModelSize" build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Flash) {
        if (!$Port -or $Port -eq 'auto') {
            # Same rule as camera/serial_port.py: exactly one Espressif (VID 303A) USB port.
            $found = @(& $idfPython -c "from serial.tools import list_ports; [print(p.device) for p in list_ports.comports() if p.vid == 0x303A]")
            if ($found.Count -ne 1) {
                throw "Auto port: found $($found.Count) YOLO UNO port(s) [$($found -join ', ')]. Plug in one board or pass -Port COMx."
            }
            $Port = $found[0]
            Write-Host "Auto port: YOLO UNO on $Port" -ForegroundColor Cyan
        }
        & $idfPython $idfScript -p $Port flash
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
