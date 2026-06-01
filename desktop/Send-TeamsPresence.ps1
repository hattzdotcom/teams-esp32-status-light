<#
.SYNOPSIS
    Teams Busy Light — Local Presence Monitor

.DESCRIPTION
    Tails the Microsoft Teams (new) log file for presence status changes
    and sends single-byte commands to the ESP32 over USB serial.

    No Azure App Registration needed — purely local, zero network calls.

    Presence is extracted from two log patterns:
      - CloudStateChanged  (real-time on status transitions)
      - BroadcastGlobalState (periodic every ~5 min)

    Both contain: "availability: <Status>"

.NOTES
    Serial protocol (115200 baud):
      0x00 = Offline       0x01 = Available     0x02 = Busy
      0x03 = In a Meeting  0x04 = Be Right Back 0x05 = Away
      0x06 = Do Not Disturb 0x07 = Unknown

    Run at startup: create a shortcut in shell:startup pointing to
      pwsh -WindowStyle Hidden -File "path\to\Send-TeamsPresence.ps1"
#>

$ScriptVersion = "1.0.0"

[CmdletBinding()]
param(
    [int]$PollIntervalMs = 2000,     # How often to check the log file
    [int]$BaudRate = 115200,
    [string]$ComPort = ""            # Auto-detect if empty
)

$ErrorActionPreference = "Stop"

# ─── Presence → byte command mapping ──────────────────────────
$PresenceMap = @{
    "Available"       = [byte]0x01
    "AvailableIdle"   = [byte]0x01
    "Busy"            = [byte]0x02
    "BusyIdle"        = [byte]0x02
    "InACall"         = [byte]0x03
    "InAMeeting"      = [byte]0x03
    "InAConferenceCall" = [byte]0x03
    "Presenting"      = [byte]0x03
    "BeRightBack"     = [byte]0x04
    "Away"            = [byte]0x05
    "AwayIdle"        = [byte]0x05
    "DoNotDisturb"    = [byte]0x06
    "Offline"         = [byte]0x00
    "OffWork"         = [byte]0x00
    "PresenceUnknown" = [byte]0x07
    "Unknown"         = [byte]0x07
}

$StatusNames = @{
    0 = "Offline"; 1 = "Available"; 2 = "Busy"; 3 = "In a Meeting"
    4 = "Be Right Back"; 5 = "Away"; 6 = "Do Not Disturb"; 7 = "Unknown"
}

# ─── Find the latest Teams log file ──────────────────────────
function Find-TeamsLog {
    $logDir = Join-Path $env:LOCALAPPDATA "Packages\MSTeams_8wekyb3d8bbwe\LocalCache\Microsoft\MSTeams\Logs"
    if (-not (Test-Path $logDir)) {
        Write-Warning "Teams log directory not found: $logDir"
        return $null
    }
    $log = Get-ChildItem $logDir -Filter "MSTeams_*.log" |
           Sort-Object LastWriteTime -Descending |
           Select-Object -First 1
    if ($log) { return $log.FullName }
    Write-Warning "No MSTeams_*.log files found"
    return $null
}

# ─── Auto-detect ESP32 COM port ───────────────────────────────
function Find-ESP32Port {
    # Look for common ESP32 USB-serial chips
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    foreach ($p in $ports) {
        try {
            $wmiPort = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
                       Where-Object { $_.Name -match $p -and $_.Name -match "(?i)USB|CH340|CP210|FTDI|ESP" } |
                       Select-Object -First 1
            if ($wmiPort) {
                Write-Host "[INFO] Found ESP32 on $p ($($wmiPort.Name))" -ForegroundColor Cyan
                return $p
            }
        } catch {}
    }
    # Fallback: just return the highest COM port (ESP32 often last)
    if ($ports.Count -gt 0) {
        $last = $ports | Sort-Object { [int]($_ -replace '\D','') } | Select-Object -Last 1
        Write-Host "[INFO] No ESP32 signature found, trying $last" -ForegroundColor Yellow
        return $last
    }
    return $null
}

# ─── Extract presence from a log line ─────────────────────────
function Get-PresenceFromLine {
    param([string]$Line)
    if ($Line -match "availability:\s*(\w+)") {
        return $Matches[1]
    }
    return $null
}

# ─── Main ─────────────────────────────────────────────────────
Write-Host "╔══════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║   Teams Busy Light — Local Log Monitor   ║" -ForegroundColor Cyan
Write-Host "║              v$($ScriptVersion.PadRight(30))║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════════╝" -ForegroundColor Cyan

# Find Teams log
$logPath = Find-TeamsLog
while (-not $logPath) {
    Write-Host "[WAIT] Teams log not found, retrying in 10s..." -ForegroundColor Yellow
    Start-Sleep -Seconds 10
    $logPath = Find-TeamsLog
}
Write-Host "[OK] Monitoring: $logPath" -ForegroundColor Green

# Find or use specified COM port
if (-not $ComPort) {
    $ComPort = Find-ESP32Port
    while (-not $ComPort) {
        Write-Host "[WAIT] ESP32 not found, retrying in 5s..." -ForegroundColor Yellow
        Start-Sleep -Seconds 5
        $ComPort = Find-ESP32Port
    }
} else {
    Write-Host "[OK] Using specified port: $ComPort" -ForegroundColor Green
}

# Open serial port
$serial = $null
try {
    $serial = New-Object System.IO.Ports.SerialPort $ComPort, $BaudRate
    $serial.ReadTimeout = 1000
    $serial.WriteTimeout = 1000
    $serial.Open()
    Write-Host "[OK] Serial connected: $ComPort @ $BaudRate" -ForegroundColor Green

    # Wait for READY from ESP32
    Start-Sleep -Seconds 2
    while ($serial.BytesToRead -gt 0) {
        $line = $serial.ReadLine()
        Write-Host "  ESP32: $line" -ForegroundColor DarkGray
        if ($line -match "READY") {
            Write-Host "[OK] ESP32 handshake complete" -ForegroundColor Green
        }
    }
} catch {
    Write-Error "Failed to open serial port $ComPort : $_"
    exit 1
}

# Send last known presence on startup (so light is correct immediately)
$lastStatus       = [byte]0xFF
$lastKeepAlive    = [DateTime]::Now
$KeepAliveSeconds = 1800           # 30 minutes
$lastLogPath = $logPath
$lastFileSize = (Get-Item $logPath).Length

$startupPresence = $null
$stream = [System.IO.File]::Open($logPath, 'Open', 'Read', 'ReadWrite')
$sr = New-Object System.IO.StreamReader($stream)
$allText = $sr.ReadToEnd()
$sr.Close(); $stream.Close()
foreach ($line in $allText -split "`n") {
    if ($line -match "(?:CloudStateChanged|BroadcastGlobalState).*availability:\s*(\w+)") {
        $startupPresence = $Matches[1]
    }
}
if ($startupPresence) {
    $cmd = $PresenceMap[$startupPresence]
    if ($null -eq $cmd) { $cmd = [byte]0x07 }
    $name = $StatusNames[[int]$cmd]
    Write-Host "[INIT] Last known status: $startupPresence → 0x$($cmd.ToString('X2')) ($name)" -ForegroundColor White
    try {
        $serial.Write([byte[]]@($cmd), 0, 1)
        $lastStatus = $cmd
        Start-Sleep -Milliseconds 100
        while ($serial.BytesToRead -gt 0) {
            $ack = $serial.ReadLine()
            Write-Host "  ESP32: $ack" -ForegroundColor DarkGray
        }
    } catch { Write-Warning "Failed to send initial status: $_" }
} else {
    Write-Host "[INIT] No prior presence found in log" -ForegroundColor Yellow
}

Write-Host "[OK] Watching for presence changes... (Ctrl+C to stop)"-ForegroundColor Green
Write-Host ""

try {
    while ($true) {
        $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

        # ── Keep-alive: resend current status every 30 minutes ──
        if ($lastStatus -ne 0xFF -and ([DateTime]::Now - $lastKeepAlive).TotalSeconds -ge $KeepAliveSeconds) {
            $lastKeepAlive = [DateTime]::Now
            $kaName = $StatusNames[[int]$lastStatus]
            Write-Host "[$ts] [KA] Keep-alive: resending 0x$($lastStatus.ToString('X2')) ($kaName)" -ForegroundColor DarkCyan
            try {
                $serial.Write([byte[]]@($lastStatus), 0, 1)
                Start-Sleep -Milliseconds 100
                while ($serial.BytesToRead -gt 0) {
                    $ack = $serial.ReadLine()
                    Write-Host "  ESP32: $ack" -ForegroundColor DarkGray
                }
            } catch { Write-Warning "Keep-alive write failed: $_" }
        }

        # Check if Teams started a new log file
        $currentLog = Find-TeamsLog
        if ($currentLog -and $currentLog -ne $lastLogPath) {
            Write-Host "[$ts] [INFO] Teams rotated log → $currentLog" -ForegroundColor Yellow
            $lastLogPath = $currentLog
            $logPath = $currentLog
            $lastFileSize = 0  # read new file from start
        }

        # Read new lines since last check
        $currentSize = (Get-Item $logPath -ErrorAction SilentlyContinue).Length
        if ($currentSize -gt $lastFileSize) {
            $stream = [System.IO.File]::Open($logPath, 'Open', 'Read', 'ReadWrite')
            $stream.Seek($lastFileSize, 'Begin') | Out-Null
            $sr = New-Object System.IO.StreamReader($stream)
            $newText = $sr.ReadToEnd()
            $sr.Close()
            $stream.Close()
            $lastFileSize = $currentSize

            # Parse all new lines for presence
            $latestPresence = $null
            foreach ($line in $newText -split "`n") {
                if ($line -match "(?:CloudStateChanged|BroadcastGlobalState).*availability:\s*(\w+)") {
                    $latestPresence = $Matches[1]
                }
            }

            if ($latestPresence) {
                $cmd = $PresenceMap[$latestPresence]
                if ($null -eq $cmd) { $cmd = [byte]0x07 }  # unknown

                if ($cmd -ne $lastStatus) {
                    $name = $StatusNames[[int]$cmd]
                    Write-Host "[$ts] $latestPresence → 0x$($cmd.ToString('X2')) ($name)" -ForegroundColor White

                    # Send to ESP32
                    try {
                        $serial.Write([byte[]]@($cmd), 0, 1)

                        # Read ACK
                        Start-Sleep -Milliseconds 100
                        while ($serial.BytesToRead -gt 0) {
                            $ack = $serial.ReadLine()
                            Write-Host "  ESP32: $ack" -ForegroundColor DarkGray
                        }
                    } catch {
                        Write-Warning "Serial write failed: $_"
                        # Try to reconnect
                        try { $serial.Close() } catch {}
                        Start-Sleep -Seconds 2
                        $serial.Open()
                        Write-Host "[$ts] [INFO] Serial reconnected" -ForegroundColor Yellow
                    }

                    $lastStatus = $cmd
                }
            }
        }

        Start-Sleep -Milliseconds $PollIntervalMs
    }
} finally {
    Write-Host "`n[INFO] Shutting down..." -ForegroundColor Yellow
    if ($serial -and $serial.IsOpen) {
        try {
            $serial.Write([byte[]]@(0x00), 0, 1)  # turn off LEDs
            Start-Sleep -Milliseconds 100
        } catch {}
        $serial.Close()
    }
    Write-Host "[OK] Goodbye" -ForegroundColor Green
}
