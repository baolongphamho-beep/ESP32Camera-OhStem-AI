param(
    [Parameter(Mandatory = $true)][string]$CameraIP,
    [double]$ReferenceCm,
    [double]$DistanceM,
    [double]$FocalPx,
    [double]$CameraHeightCm,
    [double]$TiltDeg,
    [ValidateSet('fixed', 'ground')][string]$Mode,
    [switch]$Configure,
    [switch]$Recalibrate
)
$ErrorActionPreference = 'Stop'
# Works from this camera/ directory or a copy in ESP32Camera-OhStem-AI.
$parentDirectory = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath (Join-Path $parentDirectory 'platformio.ini')) {
    $scaleProject = $parentDirectory
} else {
    $scaleProject = Join-Path $parentDirectory 'NimBLE_SmartScale_Cp'
}
$pythonExecutable = Join-Path $scaleProject '.venv-camera/Scripts/python.exe'
$cameraScript = Join-Path $scaleProject 'camera/smartscale_camera.py'
if (!(Test-Path -LiteralPath $pythonExecutable)) {
    throw 'Missing .venv-camera. Follow camera/README.md to install dependencies.'
}
$cameraArguments = @($cameraScript, '--ip', $CameraIP, '--send-uart')
$argumentMap = @{ReferenceCm='--reference-cm'; DistanceM='--distance-m'; FocalPx='--focal-px'; CameraHeightCm='--camera-height-cm'; TiltDeg='--tilt-deg'; Mode='--mode'}
foreach ($key in $argumentMap.Keys) {
    if ($PSBoundParameters.ContainsKey($key)) {
        $cameraArguments += @($argumentMap[$key], [string]$PSBoundParameters[$key])
    }
}
if ($Configure) { $cameraArguments += '--configure' }
if ($Recalibrate) { $cameraArguments += '--recalibrate' }
& $pythonExecutable @cameraArguments
exit $LASTEXITCODE
