$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$destination = Join-Path $projectRoot '.edge-deps/esp-dl'
$revision = '512ed0b0dc5f5320f3d9ca5a5aea189803f87f31'
if (!(Test-Path -LiteralPath (Join-Path $destination '.git'))) {
    git clone --filter=blob:none --no-checkout https://github.com/espressif/esp-dl.git $destination
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone ESP-DL' }
}
git -C $destination sparse-checkout set esp-dl models/yolo26
if ($LASTEXITCODE -ne 0) { throw 'Could not set sparse checkout' }
git -C $destination checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw 'Could not check out pinned ESP-DL revision' }
Write-Host "ESP-DL source ready at $destination ($revision)"
