$ErrorActionPreference = 'Stop'
$workspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$destination = Join-Path $workspaceRoot '.edge-deps/esp-dl'
$revision = '512ed0b0dc5f5320f3d9ca5a5aea189803f87f31'
if (!(Test-Path -LiteralPath (Join-Path $destination '.git'))) {
    git clone --filter=blob:none --no-checkout https://github.com/espressif/esp-dl.git $destination
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone ESP-DL' }
}
git -C $destination sparse-checkout set esp-dl models/yolo26 examples/tutorial/how_to_quantize_model/quantize_yolo26
if ($LASTEXITCODE -ne 0) { throw 'Could not set sparse checkout' }
git -C $destination checkout --detach $revision
if ($LASTEXITCODE -ne 0) { throw 'Could not check out pinned ESP-DL revision' }
Write-Host "ESP-DL source ready at $destination ($revision)"
$coco8Dir = Join-Path $workspaceRoot '.edge-deps/datasets/coco8'
if (!(Test-Path -LiteralPath (Join-Path $coco8Dir 'images/train'))) {
    $datasetsRoot = Join-Path $workspaceRoot '.edge-deps/datasets'
    New-Item -ItemType Directory -Path $datasetsRoot -Force | Out-Null
    $zipPath = Join-Path $datasetsRoot 'coco8.zip'
    Invoke-WebRequest -Uri 'https://github.com/ultralytics/assets/releases/download/v0.0.0/coco8.zip' -OutFile $zipPath
    Expand-Archive -LiteralPath $zipPath -DestinationPath $datasetsRoot -Force
    Remove-Item -LiteralPath $zipPath -Force
    Write-Host "COCO8 calibration dataset ready at $coco8Dir"
}
