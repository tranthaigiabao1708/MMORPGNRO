param(
    [int]$Port = 7777
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"
$UEEditorExe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$ClientExe = Join-Path $ProjectRoot "Saved\PackagedBuilds\Windows\InfiniteUniverse.exe"

Get-Process -Name "InfiniteUniverse*","UnrealEditor*" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$SrvLog = Join-Path $ProjectRoot "Saved\Logs\Step2Audit_Server_Reconnect.log"
if (Test-Path $SrvLog) { Remove-Item $SrvLog -Force }

Write-Host "[RECONNECT AUDIT] Starting Headless Dedicated Server on :$Port..." -ForegroundColor Cyan
$SrvArgs = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -AutoServerTelemetry -DemoDuration=37 -CSVOutputName=Step2Audit_Server_Reconnect.csv -AbsLog=`"$SrvLog`""
$SrvProc = Start-Process -FilePath $UEEditorExe -ArgumentList $SrvArgs -PassThru

$Deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $Deadline) {
    if ((Test-Path $SrvLog) -and (Select-String -Path $SrvLog -Pattern "IpNetDriver listening on port $Port" -Quiet)) { break }
    Start-Sleep -Milliseconds 250
}

Write-Host "[RECONNECT AUDIT] Connecting Client 1 (continuous 30s) & Client 2 Session A (disconnects at 9s)..." -ForegroundColor Green
$C1 = Start-Process -FilePath $ClientExe -ArgumentList "127.0.0.1:$Port -windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -AutoNetDemo=1 -DemoDuration=30 -CSVOutputName=Step2Audit_Client1_Stay.csv -Log=Step2Audit_Client1_Stay.log" -PassThru
Start-Sleep -Milliseconds 250
$C2A = Start-Process -FilePath $ClientExe -ArgumentList "127.0.0.1:$Port -windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -AutoNetDemo=2 -DemoDuration=9 -CSVOutputName=Step2Audit_Client2_Disconnect.csv -Log=Step2Audit_Client2_Disconnect.log" -PassThru

$C2A.WaitForExit()
Write-Host "[RECONNECT AUDIT] Client 2 Session A disconnected cleanly (ExitCode=$($C2A.ExitCode)). Waiting 3s before reconnect..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

Write-Host "[RECONNECT AUDIT] Reconnecting Client 2 Session B to running Dedicated Server :$Port..." -ForegroundColor Green
$C2B = Start-Process -FilePath $ClientExe -ArgumentList "127.0.0.1:$Port -windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -AutoNetDemo=2 -DemoDuration=17 -CSVOutputName=Step2Audit_Client2_Reconnect.csv -Log=Step2Audit_Client2_Reconnect.log" -PassThru

$C1.WaitForExit()
$C2B.WaitForExit()
if (-not $SrvProc.HasExited) { $SrvProc.WaitForExit(10000) | Out-Null }
if (-not $SrvProc.HasExited) { Stop-Process -Id $SrvProc.Id -Force -ErrorAction SilentlyContinue }

Write-Host "[RECONNECT AUDIT] Completed! ExitCodes -> C1=$($C1.ExitCode), C2A=$($C2A.ExitCode), C2B=$($C2B.ExitCode)" -ForegroundColor Green
Select-String -Path $SrvLog -Pattern "\[GameMode\]|Join succeeded" | ForEach-Object { $_.Line }
