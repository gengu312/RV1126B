$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$binary = Join-Path $repoRoot "build\rv1126b-lab\rv1126blab"
$entryFile = Join-Path $PSScriptRoot "desktop-entry.txt"
$adcEntryFile = Join-Path $repoRoot "apps\adc_led_touch\desktop-entry.txt"
$boardInstaller = Join-Path $PSScriptRoot "install_board.sh"
$remoteBinary = "/opt/ui/src/apps/rv1126blab"

if ((& adb get-state 2>$null) -ne "device") {
    throw "ADB device is not connected."
}
if (-not (Test-Path $binary)) {
    throw "Build output is missing. Run build_wsl.sh first."
}

& adb push $binary "/tmp/rv1126blab.new"
if ($LASTEXITCODE -ne 0) { throw "Failed to upload the application." }

$dependencyOutput = @(& adb shell "chmod 0755 /tmp/rv1126blab.new && ldd /tmp/rv1126blab.new 2>&1")
$dependencyExitCode = $LASTEXITCODE
$dependencyText = $dependencyOutput -join "`n"
if ($dependencyExitCode -ne 0 -or $dependencyText -match "(?i)not found") {
    & adb shell "rm -f /tmp/rv1126blab.new" | Out-Null
    throw "Board dependency check failed. The installed application was not changed.`n$dependencyText"
}

& adb push $entryFile "/tmp/rv1126blab-entry.txt"
if ($LASTEXITCODE -ne 0) { throw "Failed to upload the launcher entry." }

& adb push $adcEntryFile "/tmp/adcled-entry.txt"
if ($LASTEXITCODE -ne 0) { throw "Failed to upload the ADC LED launcher entry." }

& adb push $boardInstaller "/tmp/rv1126blab-install.sh"
if ($LASTEXITCODE -ne 0) { throw "Failed to upload the board installer." }

& adb shell "mv -f /tmp/rv1126blab.new '$remoteBinary'"
if ($LASTEXITCODE -ne 0) { throw "Failed to install the application." }

& adb shell "sh /tmp/rv1126blab-install.sh"
if ($LASTEXITCODE -ne 0) { throw "Failed to update launcher config. The .bak file was preserved." }

Write-Host "The lab app is installed. Reboot the board to refresh the launcher."
