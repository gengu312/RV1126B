param(
    [int]$Channel = 4,
    [double]$IntervalSeconds = 0.1,
    [int]$SampleCount = 0,
    [int]$BarWidth = 40
)

$ErrorActionPreference = 'Stop'

$boardScript = Join-Path $PSScriptRoot 'watch_adc.sh'
$remoteScript = '/tmp/rv1126b-watch-adc.sh'

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb was not found. Check the Android platform-tools installation and PATH.'
}

$deviceState = (& adb get-state 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $deviceState -ne 'device') {
    throw 'No connected board was detected. Check the USB cable and board power.'
}

& adb push $boardScript $remoteScript
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to copy the ADC sampler to the board.'
}

Write-Host 'ADC visualizer started. Turn the blue potentiometer slowly. Press Ctrl+C to stop.'
& adb shell sh $remoteScript $Channel $IntervalSeconds $SampleCount $BarWidth
