[CmdletBinding()]
param(
    [ValidateSet('Enable', 'Connect', 'Check', 'Disable')]
    [string]$Action = 'Check',

    [Alias('IP')]
    [string]$BoardIp,

    [ValidateRange(1, 65535)]
    [int]$Port = 5555,

    [string]$Interface = 'wlan0',

    [string]$UsbSerial,

    [string]$Target,

    [string]$ProbeHost,

    [string]$ProbeUrl,

    [switch]$AcceptLanRisk
)

$ErrorActionPreference = 'Stop'

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$AdbArgs
    )

    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& $script:AdbPath @AdbArgs 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }

    [pscustomobject]@{
        ExitCode = $exitCode
        Text = (($output | ForEach-Object { $_.ToString() }) -join "`n").Trim()
    }
}

function Assert-AdbSuccess {
    param(
        [Parameter(Mandatory = $true)]
        [psobject]$Result,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if ($Result.ExitCode -ne 0) {
        $details = if ($Result.Text) { "`n$($Result.Text)" } else { '' }
        throw "$Message$details"
    }
}

function Get-AdbDevices {
    $result = Invoke-Adb -AdbArgs @('devices')
    Assert-AdbSuccess -Result $result -Message 'adb devices failed.'

    $devices = @()
    foreach ($line in ($result.Text -split "`r?`n")) {
        if ($line -match '^([^\s]+)\s+(device|offline|unauthorized|no permissions)$') {
            $devices += [pscustomobject]@{
                Serial = $Matches[1]
                State = $Matches[2]
                IsNetwork = ($Matches[1] -match ':\d+$')
            }
        }
    }
    return $devices
}

function Assert-DeviceReady {
    param([Parameter(Mandatory = $true)][string]$Serial)

    $result = Invoke-Adb -AdbArgs @('-s', $Serial, 'get-state')
    if ($result.ExitCode -ne 0 -or $result.Text.Trim() -ne 'device') {
        $details = if ($result.Text) { "`n$($result.Text)" } else { '' }
        throw "ADB target '$Serial' is not in device state.$details"
    }
}

function Select-UsbDevice {
    if ($UsbSerial) {
        Assert-DeviceReady -Serial $UsbSerial
        return $UsbSerial
    }

    $candidates = @(Get-AdbDevices | Where-Object { $_.State -eq 'device' -and -not $_.IsNetwork })
    if ($candidates.Count -eq 1) {
        return $candidates[0].Serial
    }
    if ($candidates.Count -eq 0) {
        throw 'No USB ADB target is connected. Connect USB first, or supply -UsbSerial if adb uses an unusual serial.'
    }

    $serials = ($candidates.Serial -join ', ')
    throw "More than one USB ADB target is connected: $serials. Select one with -UsbSerial."
}

function Select-ConnectedTarget {
    param([switch]$PreferNetwork)

    if ($Target) {
        Assert-DeviceReady -Serial $Target
        return $Target
    }
    if ($BoardIp) {
        $endpoint = '{0}:{1}' -f (Assert-Ipv4Address -Address $BoardIp), $Port
        Assert-DeviceReady -Serial $endpoint
        return $endpoint
    }

    $devices = @(Get-AdbDevices | Where-Object { $_.State -eq 'device' })
    if ($PreferNetwork) {
        $networkDevices = @($devices | Where-Object { $_.IsNetwork })
        if ($networkDevices.Count -eq 1) {
            return $networkDevices[0].Serial
        }
        if ($networkDevices.Count -gt 1) {
            throw "More than one wireless ADB target is connected: $($networkDevices.Serial -join ', '). Select one with -Target."
        }
    }
    if ($devices.Count -eq 1) {
        return $devices[0].Serial
    }
    if ($devices.Count -eq 0) {
        throw 'No ADB target is connected. Supply -Target, or use Connect with -BoardIp first.'
    }

    throw "More than one ADB target is connected: $($devices.Serial -join ', '). Select one with -Target."
}

function Assert-Ipv4Address {
    param([Parameter(Mandatory = $true)][string]$Address)

    $parsed = $null
    if (-not [System.Net.IPAddress]::TryParse($Address, [ref]$parsed) -or
        $parsed.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
        throw "'$Address' is not a valid IPv4 address."
    }
    $normalized = $parsed.ToString()
    if ($normalized -eq '0.0.0.0' -or $normalized -eq '255.255.255.255' -or $normalized.StartsWith('127.')) {
        throw "'$Address' resolves to '$normalized' and cannot be used as a board LAN address."
    }
    return $normalized
}

function Invoke-Remote {
    param(
        [Parameter(Mandatory = $true)][string]$Serial,
        [Parameter(Mandatory = $true)][string]$Command
    )

    return Invoke-Adb -AdbArgs @('-s', $Serial, 'shell', $Command)
}

function ConvertTo-ShSingleQuoted {
    param([Parameter(Mandatory = $true)][string]$Value)

    if ($Value.Contains("'")) {
        throw 'Probe values containing a single quote are not supported.'
    }
    return "'" + $Value + "'"
}

function Get-InterfaceIpv4 {
    param(
        [Parameter(Mandatory = $true)][string]$Serial,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $result = Invoke-Remote -Serial $Serial -Command "ip -4 addr show dev $Name"
    if ($result.ExitCode -ne 0) {
        return @()
    }

    $addresses = @()
    foreach ($match in [regex]::Matches($result.Text, '(?m)\binet\s+([0-9]+(?:\.[0-9]+){3})/\d+')) {
        $addresses += $match.Groups[1].Value
    }
    return @($addresses | Select-Object -Unique)
}

function Connect-WirelessAdb {
    param([Parameter(Mandatory = $true)][string]$Endpoint)

    $lastConnectText = ''
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $connectResult = Invoke-Adb -AdbArgs @('connect', $Endpoint)
        $lastConnectText = $connectResult.Text

        $stateResult = Invoke-Adb -AdbArgs @('-s', $Endpoint, 'get-state')
        if ($stateResult.ExitCode -eq 0 -and $stateResult.Text.Trim() -eq 'device') {
            Write-Host "Wireless ADB verified: $Endpoint"
            return
        }
        if ($attempt -lt 3) {
            Start-Sleep -Seconds 1
        }
    }

    $details = if ($lastConnectText) { "`n$lastConnectText" } else { '' }
    throw "Could not verify wireless ADB target '$Endpoint'.$details"
}

function Write-CheckResult {
    param(
        [Parameter(Mandatory = $true)][bool]$Passed,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Details
    )

    if ($Passed) {
        $script:PassedChecks++
        Write-Host "[PASS] $Name - $Details"
    }
    else {
        $script:FailedChecks++
        Write-Host "[FAIL] $Name - $Details"
    }
}

function Test-BoardNetwork {
    param(
        [Parameter(Mandatory = $true)][string]$Serial,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $script:PassedChecks = 0
    $script:FailedChecks = 0

    Write-Host ''
    Write-Host "Network readiness check on $Serial"

    $stateResult = Invoke-Adb -AdbArgs @('-s', $Serial, 'get-state')
    $stateOk = $stateResult.ExitCode -eq 0 -and $stateResult.Text.Trim() -eq 'device'
    Write-CheckResult -Passed $stateOk -Name 'ADB transport' -Details $(if ($stateOk) { 'device' } else { $stateResult.Text })
    if (-not $stateOk) {
        Write-Host "Summary: $script:PassedChecks passed, $script:FailedChecks failed."
        return $false
    }

    $addresses = @(Get-InterfaceIpv4 -Serial $Serial -Name $Name)
    Write-CheckResult -Passed ($addresses.Count -gt 0) -Name "IPv4 on $Name" -Details $(if ($addresses.Count -gt 0) { $addresses -join ', ' } else { 'no IPv4 address found' })

    $routeResult = Invoke-Remote -Serial $Serial -Command 'ip route show default'
    $routeOk = $routeResult.ExitCode -eq 0 -and $routeResult.Text -match '(?m)^default(?:\s|$)'
    Write-CheckResult -Passed $routeOk -Name 'Default route' -Details $(if ($routeOk) { $routeResult.Text } else { 'no default route found' })

    $dnsResult = Invoke-Remote -Serial $Serial -Command 'cat /etc/resolv.conf 2>/dev/null'
    $dnsServers = @([regex]::Matches($dnsResult.Text, '(?m)^\s*nameserver\s+(\S+)') | ForEach-Object { $_.Groups[1].Value })
    $dnsOk = $dnsResult.ExitCode -eq 0 -and $dnsServers.Count -gt 0
    Write-CheckResult -Passed $dnsOk -Name 'DNS configuration' -Details $(if ($dnsOk) { $dnsServers -join ', ' } else { 'no nameserver in /etc/resolv.conf' })

    $timeResult = Invoke-Remote -Serial $Serial -Command 'date +%s'
    $boardEpoch = 0L
    $timeParsed = $timeResult.ExitCode -eq 0 -and [long]::TryParse($timeResult.Text.Trim(), [ref]$boardEpoch)
    if ($timeParsed) {
        $hostEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        $difference = [Math]::Abs($hostEpoch - $boardEpoch)
        $timeOk = $difference -le 300
        $timeDetails = "difference from PC: $difference seconds"
    }
    else {
        $timeOk = $false
        $timeDetails = 'could not read Unix time'
    }
    Write-CheckResult -Passed $timeOk -Name 'System time' -Details $timeDetails

    $httpsResult = Invoke-Remote -Serial $Serial -Command 'for x in curl wget; do command -v "$x" 2>/dev/null && echo "$x"; done; exit 0'
    $httpsTools = @(($httpsResult.Text -split "`r?`n") | Where-Object { $_ -match '(^|/)(curl|wget)$' } | ForEach-Object { Split-Path -Leaf $_ } | Select-Object -Unique)
    $httpsOk = $httpsTools.Count -gt 0
    Write-CheckResult -Passed $httpsOk -Name 'HTTPS client' -Details $(if ($httpsOk) { $httpsTools -join ', ' } else { 'neither curl nor wget was found' })

    if ($ProbeHost) {
        if ($ProbeHost -notmatch '^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$') {
            Write-CheckResult -Passed $false -Name 'DNS probe' -Details "invalid probe host: $ProbeHost"
        }
        else {
            $quotedHost = ConvertTo-ShSingleQuoted -Value $ProbeHost
            $dnsProbe = Invoke-Remote -Serial $Serial -Command (
                "if command -v getent >/dev/null 2>&1; then getent hosts $quotedHost; " +
                "elif command -v nslookup >/dev/null 2>&1; then nslookup $quotedHost; " +
                "else exit 127; fi"
            )
            $dnsProbeOk = $dnsProbe.ExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($dnsProbe.Text)
            $dnsProbeDetails = if ($dnsProbeOk) { ($dnsProbe.Text -split "`r?`n")[0] } else { 'resolution failed or no resolver tool is installed' }
            Write-CheckResult -Passed $dnsProbeOk -Name "DNS probe ($ProbeHost)" -Details $dnsProbeDetails
        }
    }

    if ($ProbeUrl) {
        $parsedUrl = $null
        $urlValid = [Uri]::TryCreate($ProbeUrl, [UriKind]::Absolute, [ref]$parsedUrl) -and $parsedUrl.Scheme -eq 'https'
        if (-not $urlValid) {
            Write-CheckResult -Passed $false -Name 'HTTPS probe' -Details 'ProbeUrl must be an absolute https:// URL'
        }
        elseif (-not $httpsOk) {
            Write-CheckResult -Passed $false -Name "HTTPS probe ($ProbeUrl)" -Details 'no curl or wget client is installed'
        }
        else {
            $quotedUrl = ConvertTo-ShSingleQuoted -Value $ProbeUrl
            $httpsProbe = Invoke-Remote -Serial $Serial -Command (
                "if command -v curl >/dev/null 2>&1; then curl -fsS --max-time 12 -o /dev/null $quotedUrl; " +
                "else wget -q -T 12 -O /dev/null $quotedUrl; fi"
            )
            $httpsProbeOk = $httpsProbe.ExitCode -eq 0
            $httpsProbeDetails = if ($httpsProbeOk) { 'request succeeded' } elseif ($httpsProbe.Text) { $httpsProbe.Text } else { 'request failed' }
            Write-CheckResult -Passed $httpsProbeOk -Name "HTTPS probe ($ProbeUrl)" -Details $httpsProbeDetails
        }
    }

    Write-Host "Summary: $script:PassedChecks passed, $script:FailedChecks failed."
    if ($script:FailedChecks -eq 0) {
        if ($ProbeHost -or $ProbeUrl) {
            Write-Host 'All requested configuration and external reachability probes passed.'
        }
        else {
            Write-Host 'The configuration checks passed. This does not by itself prove public Internet or HTTPS reachability.'
        }
        return $true
    }

    Write-Host 'One or more checks failed. Fix them and run -Action Check again.'
    return $false
}

function Show-LanRiskWarning {
    Write-Warning 'TCP ADB may expose a root shell to other hosts on the same LAN. Use only a trusted, isolated network and run -Action Disable when finished.'
}

if ($Interface -notmatch '^[A-Za-z0-9_.-]+$') {
    throw "Invalid interface name '$Interface'."
}

$adbCommand = Get-Command adb -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $adbCommand) {
    throw 'adb was not found. Install Android platform-tools and add adb to PATH.'
}
$script:AdbPath = $adbCommand.Source

try {
    switch ($Action) {
        'Enable' {
            if (-not $AcceptLanRisk) {
                throw 'Enabling TCP ADB requires -AcceptLanRisk. Read scripts/wireless/README.md before continuing.'
            }
            Show-LanRiskWarning

            $usbTarget = Select-UsbDevice
            Write-Host "USB ADB target: $usbTarget"

            $detectedAddresses = @(Get-InterfaceIpv4 -Serial $usbTarget -Name $Interface)
            if ($BoardIp) {
                $selectedIp = Assert-Ipv4Address -Address $BoardIp
                if ($detectedAddresses.Count -gt 0 -and $selectedIp -notin $detectedAddresses) {
                    throw "The supplied address $selectedIp was not found on $Interface. Detected: $($detectedAddresses -join ', '). Choose the correct address or interface."
                }
            }
            else {
                if ($detectedAddresses.Count -eq 0) {
                    throw "No IPv4 address was found on $Interface. Connect the board to Wi-Fi first, choose another -Interface, or supply -BoardIp."
                }
                if ($detectedAddresses.Count -gt 1) {
                    throw "More than one IPv4 address was found on ${Interface}: $($detectedAddresses -join ', '). Select one with -BoardIp."
                }
                $selectedIp = Assert-Ipv4Address -Address $detectedAddresses[0]
            }

            $endpoint = '{0}:{1}' -f $selectedIp, $Port
            Write-Host "Board endpoint: $endpoint"

            $tcpResult = Invoke-Adb -AdbArgs @('-s', $usbTarget, 'tcpip', $Port.ToString())
            Assert-AdbSuccess -Result $tcpResult -Message "Failed to enable TCP ADB on port $Port."
            Write-Host $tcpResult.Text

            Connect-WirelessAdb -Endpoint $endpoint
            if (-not (Test-BoardNetwork -Serial $endpoint -Name $Interface)) {
                exit 2
            }
        }

        'Connect' {
            if (-not $BoardIp) {
                throw 'Connect requires -BoardIp. It does not require a USB connection.'
            }
            Show-LanRiskWarning
            $selectedIp = Assert-Ipv4Address -Address $BoardIp
            $endpoint = '{0}:{1}' -f $selectedIp, $Port
            Connect-WirelessAdb -Endpoint $endpoint
            if (-not (Test-BoardNetwork -Serial $endpoint -Name $Interface)) {
                exit 2
            }
        }

        'Check' {
            $selectedTarget = Select-ConnectedTarget
            if (-not (Test-BoardNetwork -Serial $selectedTarget -Name $Interface)) {
                exit 2
            }
        }

        'Disable' {
            $selectedTarget = Select-ConnectedTarget -PreferNetwork
            $identityResult = Invoke-Remote -Serial $selectedTarget -Command 'getprop ro.serialno 2>/dev/null'
            $boardSerial = if ($identityResult.ExitCode -eq 0) { $identityResult.Text.Trim() } else { '' }
            $usbBefore = @(Get-AdbDevices | Where-Object { $_.State -eq 'device' -and -not $_.IsNetwork } | ForEach-Object { $_.Serial })
            Write-Host "Requesting USB ADB mode through $selectedTarget ..."
            $usbResult = Invoke-Adb -AdbArgs @('-s', $selectedTarget, 'usb')
            Assert-AdbSuccess -Result $usbResult -Message 'Failed to request USB ADB mode.'
            if ($usbResult.Text) {
                Write-Host $usbResult.Text
            }

            if ($selectedTarget -match ':\d+$') {
                Invoke-Adb -AdbArgs @('disconnect', $selectedTarget) | Out-Null
            }

            $verifiedSerial = $null
            for ($attempt = 1; $attempt -le 10 -and -not $verifiedSerial; $attempt++) {
                Start-Sleep -Seconds 1
                $usbDevices = @(Get-AdbDevices | Where-Object { $_.State -eq 'device' -and -not $_.IsNetwork })
                foreach ($candidate in $usbDevices) {
                    if ($boardSerial -and $candidate.Serial -eq $boardSerial) {
                        $verifiedSerial = $candidate.Serial
                        break
                    }
                    if ($boardSerial) {
                        $candidateIdentity = Invoke-Remote -Serial $candidate.Serial -Command 'getprop ro.serialno 2>/dev/null'
                        if ($candidateIdentity.ExitCode -eq 0 -and $candidateIdentity.Text.Trim() -eq $boardSerial) {
                            $verifiedSerial = $candidate.Serial
                            break
                        }
                    }
                    elseif ($selectedTarget -notmatch ':\d+$' -and $candidate.Serial -eq $selectedTarget) {
                        $verifiedSerial = $candidate.Serial
                        break
                    }
                }
            }

            if ($verifiedSerial) {
                Write-Host "USB ADB verified for the same board: $verifiedSerial"
            }
            else {
                $newUsbDevices = @($usbDevices | Where-Object { $_.Serial -notin $usbBefore })
                $details = if ($newUsbDevices.Count -gt 0) {
                    " New USB target(s) appeared but could not be matched to the board: $($newUsbDevices.Serial -join ', ')."
                }
                elseif (-not $boardSerial) {
                    ' The board did not expose ro.serialno, so a different connected USB device cannot be accepted as proof.'
                }
                else { '' }
                Write-Host "USB mode was requested, but the same board was not verified over USB within 10 seconds.$details"
                exit 2
            }
        }
    }
}
catch {
    Write-Error $_.Exception.Message -ErrorAction Continue
    exit 1
}
