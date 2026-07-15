$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$targetDir = Join-Path $repoRoot "build\board-sysroot\usr\lib"
$libraries = @(
    "libQt5Core.so.5.15.11",
    "libQt5Gui.so.5.15.11",
    "libQt5Widgets.so.5.15.11"
)

if ((& adb get-state 2>$null) -ne "device") {
    throw "ADB device is not connected."
}

New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
foreach ($library in $libraries) {
    & adb pull "/usr/lib/$library" (Join-Path $targetDir $library)
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to pull board Qt library: $library"
    }
}

Write-Host "Board Qt libraries are ready: $targetDir"
