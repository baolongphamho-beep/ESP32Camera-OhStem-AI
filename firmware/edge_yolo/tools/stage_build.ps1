param([string]$BuildRoot = 'C:\esp\SmartScale')
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../..')).Path
$targetRoot = [System.IO.Path]::GetFullPath($BuildRoot).TrimEnd('\')
if ($targetRoot -match '\s') { throw 'BuildRoot must not contain spaces.' }
if ($targetRoot -eq [System.IO.Path]::GetPathRoot($targetRoot)) { throw 'BuildRoot must be a project subdirectory, not a drive root.' }
if ($targetRoot.Equals($sourceRoot, [System.StringComparison]::OrdinalIgnoreCase)) { return $sourceRoot }
New-Item -ItemType Directory -Force -Path $targetRoot | Out-Null
$marker = Join-Path $targetRoot '.smartscale-source.txt'
if (Test-Path -LiteralPath $marker) {
    if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne $sourceRoot) {
        throw "BuildRoot belongs to another source folder: $targetRoot"
    }
} elseif (@(Get-ChildItem -LiteralPath $targetRoot -Force).Count) {
    throw "BuildRoot is not empty and has no SmartScale source marker: $targetRoot"
}
Set-Content -LiteralPath $marker -Value $sourceRoot -Encoding utf8
$relativeFiles = @('firmware/edge_yolo/CMakeLists.txt', 'firmware/edge_yolo/partitions.csv',
    'firmware/edge_yolo/sdkconfig.defaults', 'firmware/edge_yolo/dependencies.lock')
foreach ($folder in @('firmware/edge_yolo/main', 'firmware/edge_yolo/models', 'lib/EdgeSweep', 'lib/Measurements')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceRoot $folder) -File) {
        if ($file.Extension -in @('.h', '.hpp', '.cpp', '.txt', '.espdl') -or $file.Name -like '*.build.json') {
            $relativeFiles += "$folder/$($file.Name)"
        }
    }
}
foreach ($relative in $relativeFiles) {
    $source = Join-Path $sourceRoot $relative
    if (!(Test-Path -LiteralPath $source)) { continue }
    $target = Join-Path $targetRoot $relative
    if ($relative -eq 'firmware/edge_yolo/dependencies.lock' -and (Test-Path -LiteralPath $target)) { continue }
    if ((Test-Path -LiteralPath $target) -and
        (Get-FileHash -LiteralPath $source).Hash -eq (Get-FileHash -LiteralPath $target).Hash) { continue }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
    # Copy-Item keeps the source timestamp; an edit made while a build was running can be
    # older than the object file, and ninja would then silently keep the old code.
    (Get-Item -LiteralPath $target).LastWriteTime = Get-Date
}
Write-Host "Native build source synchronized: $targetRoot" -ForegroundColor Cyan
Write-Output $targetRoot
