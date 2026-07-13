param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BoardScript = Join-Path $PSScriptRoot 'collect_board_info.sh'
$RemoteScript = '/tmp/rv1126b-collect-board-info.sh'

if (-not (Test-Path -LiteralPath $BoardScript)) {
    throw "Board-side collector not found: $BoardScript"
}

$AdbCommand = Get-Command adb -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $AdbCommand) {
    throw 'adb was not found. Install Android platform-tools and add adb to PATH.'
}

$Devices = & $AdbCommand.Source devices
if ($LASTEXITCODE -ne 0 -or -not ($Devices -match "`tdevice$")) {
    throw "No ADB target is in device state. Current output:`n$($Devices -join "`n")"
}

if (-not $OutputPath) {
    $Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputPath = Join-Path $ProjectRoot "output/board-info-$Timestamp.txt"
}

$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$OutputDirectory = Split-Path -Parent $OutputPath
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

& $AdbCommand.Source push $BoardScript $RemoteScript | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to push the collector to the board.'
}

$ReportLines = & $AdbCommand.Source exec-out sh $RemoteScript
if ($LASTEXITCODE -ne 0) {
    throw 'Board information collection failed.'
}

$Report = ($ReportLines -join "`n") + "`n"
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($OutputPath, $Report, $Utf8NoBom)

Write-Host "Collection completed: $OutputPath"
