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
$rootProjectDir = Split-Path -Parent (Split-Path -Parent $scaleProject)
$rootVenvPython = Join-Path $rootProjectDir '.venv/Scripts/python.exe'
$localVenvPython = Join-Path $scaleProject '.venv-camera/Scripts/python.exe'
if (Test-Path -LiteralPath $rootVenvPython) {
    $pythonExecutable = $rootVenvPython
} elseif (Test-Path -LiteralPath $localVenvPython) {
    $pythonExecutable = $localVenvPython
} else {
    throw "Missing Python virtual environment at $rootVenvPython. Follow camera/README.md to install dependencies."
}
$cameraScript = Join-Path $scaleProject 'camera/smartscale_camera.py'
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
