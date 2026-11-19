param(
    [int]$Port = 7777,
    [int]$ClientCount = 2,
    [switch]$AutomatedDemoCapture,
    [switch]$RecordVideoFrames,
    [switch]$SimulateWANLag,
    [double]$DurationSeconds = 27.5,
    [string]$CustomSuffix = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"
$UEEditorExe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$ClientExe = Join-Path $ProjectRoot "Saved\PackagedBuilds\Windows\InfiniteUniverse.exe"

if (-not (Test-Path $UEEditorExe)) {
    throw "UnrealEditor.exe not found at $UEEditorExe."
}
if (-not (Test-Path $ClientExe)) {
    throw "Packaged client executable not found at $ClientExe. Run Tools\PackageStandalone.ps1 first."
}

# Stop any stale packaged clients or headless -server processes holding port 7777 (leave interactive GUI Editor untouched!)
Get-Process -Name "InfiniteUniverse*" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -match "-server" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 500

$Suffix = if ($CustomSuffix) { $CustomSuffix } elseif ($SimulateWANLag) { "_WANLag" } elseif ($RecordVideoFrames) { "_Video" } elseif ($ClientCount -gt 2) { "_${ClientCount}Clients" } else { "" }
$ServerDur = [math]::Round($DurationSeconds + 6.5, 1)

# Simulate realistic 80ms WAN RTT (+/- 15ms jitter, 2% packet loss): 40ms each direction = 80ms RTT!
$ServerLagExec = if ($SimulateWANLag) { '-ExecCmds="Net PktLag=40, Net PktLagVariance=8, Net PktLoss=1"' } else { "" }
$ClientLagExec = if ($SimulateWANLag) { '-ExecCmds="Net PktLag=40, Net PktLagVariance=7, Net PktLoss=1"' } else { "" }

$ServerLogPath = Join-Path $ProjectRoot "Saved\Logs\Step2_DedicatedServer$Suffix.log"
if (Test-Path $ServerLogPath) { Remove-Item $ServerLogPath -Force }

# 1. Launch True Headless Dedicated Server (NM_DedicatedServer, NetMode=1, 60Hz) on UE 5.8 Installed Build
$ServerArgs = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -AbsLog=`"$ServerLogPath`""
if ($AutomatedDemoCapture) {
    $ServerArgs += " -AutoServerTelemetry -DemoDuration=$ServerDur -CSVOutputName=Step2_DedicatedServerProfile$Suffix.csv"
}
if ($ServerLagExec) {
    $ServerArgs += " $ServerLagExec"
}

Write-Host "[SERVER] Booting Headless Dedicated Server (NM_DedicatedServer) on UDP Port $Port..." -ForegroundColor Cyan
$ServerProc = Start-Process -FilePath $UEEditorExe -ArgumentList $ServerArgs -PassThru

# Poll server log until IpNetDriver binds UDP :7777 so clients connect on their very first handshake packet!
$Deadline = (Get-Date).AddSeconds(20)
$BoundReady = $false
while ((Get-Date) -lt $Deadline) {
    if ($ServerProc.HasExited) {
        throw "Dedicated Server exited prematurely with code $($ServerProc.ExitCode)!"
    }
    if ((Test-Path $ServerLogPath) -and (Select-String -Path $ServerLogPath -Pattern "IpNetDriver listening on port $Port" -Quiet)) {
        $BoundReady = $true
        break
    }
    Start-Sleep -Milliseconds 250
}
if (-not $BoundReady) {
    throw "Timed out waiting for Dedicated Server to bind UDP port $Port!"
}
Write-Host "[SERVER] IpNetDriver listening on 0.0.0.0:$Port (PID $($ServerProc.Id))" -ForegroundColor Green

# 2. Launch Packaged Cooked D3D12 Clients (Side-by-Side 960x540 for 2 clients, or 2x2 Quad Grid 960x500 for 4 clients)
$ClientProcs = @()
for ($i = 1; $i -le $ClientCount; $i++) {
    if ($ClientCount -le 2) {
        $WinX = ($i - 1) * 960
        $WinY = 40
        $ResW = 960
        $ResH = 540
    } else {
        $Col = ($i - 1) % 2
        $Row = [math]::Floor(($i - 1) / 2)
        $WinX = $Col * 960
        $WinY = 20 + ($Row * 510)
        $ResW = 960
        $ResH = 500
    }
    $ClientArgs = "127.0.0.1:$Port -windowed -WinX=$WinX -WinY=$WinY -ResX=$ResW -ResY=$ResH -NoVSync -Log=Step2_Client${i}${Suffix}.log"
    if ($AutomatedDemoCapture) {
        $ClientArgs += " -unattended -AutoNetDemo=$i -DemoDuration=$DurationSeconds -CSVOutputName=Step2_Client${i}Profile${Suffix}.csv"
        if ($RecordVideoFrames) {
            $ClientArgs += " -RecordDemoFrames -DemoFramesDir=Step2Frames_Client$i"
        }
    }
    if ($ClientLagExec) {
        $ClientArgs += " $ClientLagExec"
    }

    Write-Host "[CLIENT $i] Connecting cooked D3D12 client to 127.0.0.1:$Port (WinX=$WinX, WANLag=$SimulateWANLag, RecordVideo=$RecordVideoFrames)..." -ForegroundColor Green
    $Proc = Start-Process -FilePath $ClientExe -ArgumentList $ClientArgs -PassThru
    $ClientProcs += $Proc
    Start-Sleep -Milliseconds 250
}

if ($AutomatedDemoCapture) {
    Write-Host "[WAIT] Running live 3-process multiplayer session ($DurationSeconds s)..." -ForegroundColor Yellow
    foreach ($cp in $ClientProcs) {
        $cp.WaitForExit()
        Write-Host "  -> Client PID $($cp.Id) exited with code $($cp.ExitCode)"
    }
    if (-not $ServerProc.HasExited) {
        $ServerProc.WaitForExit(10000) | Out-Null
    }
    if (-not $ServerProc.HasExited) {
        Stop-Process -Id $ServerProc.Id -Force -ErrorAction SilentlyContinue
    }
    Write-Host "[DONE] Dedicated Server + $ClientCount Clients run finished cleanly!" -ForegroundColor Green
} else {
    Write-Host "[LIVE] 1 Headless Dedicated Server (PID $($ServerProc.Id)) + $ClientCount Cooked Clients are running!" -ForegroundColor Magenta
    Write-Host "       Close client windows or run 'Stop-Process -Name InfiniteUniverse*,UnrealEditor* -Force' when finished."
}
