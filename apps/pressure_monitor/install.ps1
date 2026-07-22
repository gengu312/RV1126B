$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$binary = Join-Path $repoRoot "build\pressure-monitor\pressuremonitor"
$entryFile = Join-Path $PSScriptRoot "desktop-entry.txt"
$boardInstaller = Join-Path $PSScriptRoot "install_board.sh"
$defaultConfig = Join-Path $PSScriptRoot "config.ini.example"
$remoteBinary = "/opt/ui/src/apps/pressuremonitor"

if ((& adb get-state 2>$null) -ne "device") {
    throw "ADB device is not connected."
}
if (-not (Test-Path $binary)) {
    throw "Build output is missing. Run build_wsl.sh first."
}

try {
    & adb push $binary "/tmp/pressuremonitor.new"
    if ($LASTEXITCODE -ne 0) { throw "Failed to upload the application." }

    $dependencyOutput = @(& adb shell "chmod 0755 /tmp/pressuremonitor.new && ldd /tmp/pressuremonitor.new 2>&1")
    $dependencyExitCode = $LASTEXITCODE
    $dependencyText = $dependencyOutput -join "`n"
    if ($dependencyExitCode -ne 0 -or $dependencyText -match "(?i)not found") {
        throw "Board dependency check failed. The installed application was not changed.`n$dependencyText"
    }

    & adb push $entryFile "/tmp/pressuremonitor-entry.txt"
    if ($LASTEXITCODE -ne 0) { throw "Failed to upload the launcher entry." }

    & adb push $defaultConfig "/tmp/pressuremonitor-default.ini"
    if ($LASTEXITCODE -ne 0) { throw "Failed to upload the default configuration." }

    & adb push $boardInstaller "/tmp/pressuremonitor-install.sh"
    if ($LASTEXITCODE -ne 0) { throw "Failed to upload the board installer." }

    & adb shell "sh /tmp/pressuremonitor-install.sh"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install the application or update the launcher config."
    }

    $installedSizeOutput = @(& adb shell "wc -c < '$remoteBinary'")
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to verify the installed binary."
    }
    $installedSize = ($installedSizeOutput -join "").Trim()
    $localSize = (Get-Item $binary).Length.ToString()
    if ($installedSize -ne $localSize) {
        throw "Installed binary size does not match the local build."
    }
}
finally {
    & adb shell "rm -f /tmp/pressuremonitor.new /tmp/pressuremonitor-entry.txt /tmp/pressuremonitor-default.ini /tmp/pressuremonitor-install.sh" 2>$null | Out-Null
}

Write-Host "Pressure Monitor is installed. Reboot the board to refresh the launcher."
