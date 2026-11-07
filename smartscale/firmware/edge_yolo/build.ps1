param([string]$IDFPath = 'C:\Espressif\v6.1\esp-idf', [string]$Port, [switch]$Flash)
$ErrorActionPreference = 'Stop'
$env:IDF_PATH = $IDFPath
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.1\venv'
$env:PYTHONUTF8 = '1'
$env:ESP_IDF_VERSION = '6.1.0'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\esp-rom-elfs\20260528'
$toolDirectories = @(
    'C:\Espressif\tools\python\v6.1\venv\Scripts',
    'C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin',
    'C:\Espressif\tools\cmake\4.0.3\bin',
    'C:\Espressif\tools\ninja\1.12.1'
)
$env:PATH = ($toolDirectories -join ';') + ';' + $env:PATH
$idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
$idfScript = Join-Path $IDFPath 'tools/idf.py'
Push-Location $PSScriptRoot
try {
    & $idfPython $idfScript -D IDF_TARGET=esp32s3 build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($Flash) {
        if (!$Port) { throw 'Specify -Port COMx to flash the YOLO UNO.' }
        & $idfPython $idfScript -p $Port flash
        exit $LASTEXITCODE
    }
} finally { Pop-Location }
