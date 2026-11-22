$ErrorActionPreference = 'Stop'
$modelDir = Join-Path $PSScriptRoot '../models'
New-Item -ItemType Directory -Force -Path $modelDir | Out-Null
$model = Join-Path $modelDir 'yolo26n_224_s8_s3.espdl'
$expected = '76699d828b929755efc8b854a7cf313fd287a6e0d7ca21fbc82c998688b8955a'
$revision = 'c2f71b65c7e8e76567cf5d14f6b8240a50ab70a7'
if ((Test-Path -LiteralPath $model) -and (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash -eq $expected) {
    Write-Host 'Pinned YOLO26n model already present and verified.'
    return
}
$download = "$model.download"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/baolongphamho-beep/ESP32Camera-OhStem-AI/$revision/firmware/edge_yolo/models/yolo26n_224_s8_s3.espdl" -OutFile $download
if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $expected) { throw 'Model SHA256 mismatch; existing model was kept.' }
Move-Item -LiteralPath $download -Destination $model -Force
Write-Host 'Downloaded and verified pinned YOLO26n model (3.08 MB).'
