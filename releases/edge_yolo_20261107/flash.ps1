param([Parameter(Mandatory=$true)][string]$Port, [string]$Python)
$ErrorActionPreference = 'Stop'
if (!$Python) { $Python = Join-Path $PSScriptRoot '../../smartscale/.venv-camera/Scripts/python.exe' }
if (!(Test-Path -LiteralPath $Python)) { throw 'Create smartscale/.venv-camera first, or pass -Python with the Python executable path.' }
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
foreach ($entry in $manifest.files.PSObject.Properties) {
    $file = Join-Path $PSScriptRoot $entry.Name
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $entry.Value.sha256) { throw "SHA256 mismatch: $($entry.Name)" }
}
& $Python -m esptool version
if ($LASTEXITCODE -ne 0) { throw 'Install esptool in this Python environment: python -m pip install esptool' }
& $Python -m esptool --chip esp32s3 --port $Port --baud 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 (Join-Path $PSScriptRoot 'bootloader.bin') 0x8000 (Join-Path $PSScriptRoot 'partition-table.bin') 0x10000 (Join-Path $PSScriptRoot 'smartscale_edge_yolo.bin')
exit $LASTEXITCODE
