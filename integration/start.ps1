param([string]$CameraUrl = '', [string]$SerialPort = '', [ValidateSet('', 'kg', 'lb')][string]$LegacyUnit = '')
$candidates = @(
    "$env:LOCALAPPDATA\Programs\Python\Python314\python.exe",
    "$env:LOCALAPPDATA\Programs\Python\Python313\python.exe",
    "$env:USERPROFILE\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
)
$pythonPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $pythonPath) { throw 'Không tìm thấy Python. Cài Python 3.10+ hoặc chạy: py .\integration\app.py' }
$appArgs = @((Join-Path $PSScriptRoot 'app.py'))
if ($CameraUrl) { $appArgs += @('--camera-url', $CameraUrl) }
if ($SerialPort) { $appArgs += @('--serial-port', $SerialPort) }
if ($LegacyUnit) { $appArgs += @('--legacy-unit', $LegacyUnit) }
& $pythonPath @appArgs
