<#
.SYNOPSIS
    Registers Send-TeamsPresence.ps1 to run at login (hidden, background).

.DESCRIPTION
    Creates a Windows Task Scheduler task that launches the Teams Busy Light
    script automatically when you log in. The PowerShell window is hidden.
    Run this script once — it persists across reboots.

    To remove:  .\Install-Startup.ps1 -Remove
#>

param(
    [switch]$Remove
)

$TaskName = "TeamsBusyLight"
$ScriptPath = Join-Path $PSScriptRoot "Send-TeamsPresence.ps1"

if ($Remove) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "✓ Removed '$TaskName' scheduled task." -ForegroundColor Green
    return
}

if (-not (Test-Path $ScriptPath)) {
    Write-Host "✗ Cannot find Send-TeamsPresence.ps1 in the same folder as this script." -ForegroundColor Red
    return
}

# Pick whichever PowerShell is available (prefer pwsh, fall back to powershell.exe)
$PwshPath = if (Get-Command pwsh -ErrorAction SilentlyContinue) {
    (Get-Command pwsh).Source
} else {
    "powershell.exe"
}

$Action = New-ScheduledTaskAction `
    -Execute $PwshPath `
    -Argument "-WindowStyle Hidden -ExecutionPolicy Bypass -File `"$ScriptPath`""

$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME

$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -ExecutionTimeLimit ([TimeSpan]::Zero)   # no timeout — runs indefinitely

# Remove existing task if re-running
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue

Register-ScheduledTask `
    -TaskName $TaskName `
    -Action $Action `
    -Trigger $Trigger `
    -Settings $Settings `
    -Description "Sends Microsoft Teams presence to ESP32 busy light over USB serial" `
    | Out-Null

Write-Host "✓ Registered '$TaskName' to run at login (hidden)." -ForegroundColor Green
Write-Host "  Script: $ScriptPath" -ForegroundColor DarkGray
Write-Host "  Shell:  $PwshPath" -ForegroundColor DarkGray
Write-Host ""
Write-Host "To remove later:  .\Install-Startup.ps1 -Remove" -ForegroundColor Yellow
