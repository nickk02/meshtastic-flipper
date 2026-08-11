<#
Build, install and capture in one command, so a test does not depend on two
people getting their timing to line up.

    powershell -ExecutionPolicy Bypass -File tools\flipper-test.ps1

The Flipper must be on USB and qFlipper must be closed: the device exposes one
serial session and qFlipper holds it.
#>
param(
    [string]$Port = "",
    [int]$Seconds = 300,
    [string]$Out = "flipper-capture.log"
)

$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo

if (-not $Port) {
    $Port = [System.IO.Ports.SerialPort]::GetPortNames() | Select-Object -First 1
    if (-not $Port) { Write-Host "No serial port found. Is the Flipper plugged in?"; exit 1 }
}
Write-Host "Using $Port"
Write-Host "Building and installing..."

$job = Start-Job -ScriptBlock { param($r) Set-Location $r; python -m ufbt launch 2>&1 } -ArgumentList $repo

# ufbt stays attached after starting the app, so it is stopped once the install
# is done rather than waited on. Waiting on it is a hang, not a completion.
$deadline = (Get-Date).AddSeconds(90)
$installed = $false
while ((Get-Date) -lt $deadline) {
    $out = Receive-Job $job -Keep 2>$null
    if ($out -match "Launching app") { $installed = $true; break }
    if ($out -match "App Too old|App too new") {
        Write-Host "API mismatch. Point ufbt at your firmware's SDK index."
        break
    }
    Start-Sleep -Milliseconds 500
}
Stop-Job $job -ErrorAction SilentlyContinue
Remove-Job $job -Force -ErrorAction SilentlyContinue
Get-Process python -ErrorAction SilentlyContinue |
    Where-Object { $_.StartTime -gt (Get-Date).AddMinutes(-3) } |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

if (-not $installed) { Write-Host "Install did not complete. Nothing captured."; Pop-Location; exit 1 }

Write-Host ""
Write-Host "App running. Capturing for $Seconds seconds."
Write-Host ">>> Connect from the Meshtastic app on your phone now. <<<"
Write-Host ""

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, one
$sp.ReadTimeout = 500
$sp.DtrEnable = $true
$sp.RtsEnable = $true

try {
    $sp.Open()
    Start-Sleep -Milliseconds 400
    $sp.DiscardInBuffer()
    $sp.Write("log debug`r`n")

    $end = (Get-Date).AddSeconds($Seconds)
    $sb = New-Object System.Text.StringBuilder
    while ((Get-Date) -lt $end) {
        $chunk = $sp.ReadExisting()
        if ($chunk.Length -gt 0) { [void]$sb.Append($chunk); Write-Host -NoNewline $chunk }
        Start-Sleep -Milliseconds 100
    }
    $sb.ToString() | Out-File -FilePath $Out -Encoding utf8
    Write-Host "`n---- saved to $Out ----"
} catch {
    Write-Host ("ERROR: " + $_.Exception.Message)
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    Pop-Location
}
