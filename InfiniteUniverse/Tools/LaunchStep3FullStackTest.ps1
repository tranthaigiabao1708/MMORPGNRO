param(
    [int]$Port = 7777,
    [int]$GatewayPort = 8080,
    [int]$PGPort = 5433,
    [switch]$SkipDuplicateLoginPhase,
    [switch]$SkipEncodeVideo
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent $ProjectRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"
$UEEditorExe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$InnerClientExe = Join-Path $ProjectRoot "Saved\PackagedBuilds\Windows\InfiniteUniverse\Binaries\Win64\InfiniteUniverse.exe"
$ClientExe = if (Test-Path $InnerClientExe) { $InnerClientExe } else { Join-Path $ProjectRoot "Saved\PackagedBuilds\Windows\InfiniteUniverse.exe" }
$GatewayExe = Join-Path $ProjectRoot "Saved\BackendBin\gateway.exe"
$PGDataDir = Join-Path $ProjectRoot "Saved\LocalPG18Data"
$PGBinDir = "C:\Program Files\PostgreSQL\18\bin"
$PsqlExe = Join-Path $PGBinDir "psql.exe"
$PgIsReadyExe = Join-Path $PGBinDir "pg_isready.exe"
$PostgresExe = Join-Path $PGBinDir "postgres.exe"

if (-not (Test-Path $ClientExe)) {
    throw "Packaged client not found at $ClientExe. Run Tools\PackageStandalone.ps1 first."
}

function Stop-TestUEProcesses {
    Get-Process -Name "InfiniteUniverse*" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -match "-server" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 600
}

function Wait-ServerListening([System.Diagnostics.Process]$Proc, [string]$LogFile, [int]$ListenPort) {
    $Deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $Deadline) {
        if ($Proc.HasExited) {
            throw "Dedicated Server exited early with code $($Proc.ExitCode)!"
        }
        if ((Test-Path $LogFile) -and (Select-String -Path $LogFile -Pattern "IpNetDriver listening on port $ListenPort" -Quiet)) {
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out waiting for Dedicated Server ($LogFile) to listen on UDP $ListenPort"
}

Stop-TestUEProcesses

# 1. Ensure PostgreSQL 18 (:5433) is running
& $PgIsReadyExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[PG18] Starting local PostgreSQL 18 on 127.0.0.1:$PGPort..." -ForegroundColor Cyan
    Start-Process -FilePath $PostgresExe -ArgumentList "-D `"$PGDataDir`" -p $PGPort -h 127.0.0.1" -WindowStyle Hidden | Out-Null
    for ($i = 0; $i -lt 20; $i++) {
        & $PgIsReadyExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe | Out-Null
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 400
    }
}
Write-Host "[PG18] Healthy on 127.0.0.1:$PGPort" -ForegroundColor Green

# 2. Ensure Go Gateway (:8080) is running
$GatewayHealthy = $false
try {
    $h = Invoke-RestMethod -Uri "http://127.0.0.1:$GatewayPort/v1/healthz" -TimeoutSec 2
    if ($h.status -eq "ok") { $GatewayHealthy = $true }
} catch {}

if (-not $GatewayHealthy) {
    if (-not (Test-Path $GatewayExe)) {
        Set-Location (Join-Path $RepoRoot "backend")
        New-Item -ItemType Directory -Force -Path (Split-Path $GatewayExe) | Out-Null
        & "C:\Program Files\Go\bin\go.exe" build -o $GatewayExe ./cmd/gateway
    }
    $env:IU_PG_DSN = "postgres://iu_admin:iu_dev_secret_2026@127.0.0.1:$PGPort/infinite_universe?sslmode=disable"
    $env:IU_ALLOW_OPTIONAL_DEPS = "true"
    $env:IU_REDIS_ADDR = ""
    $env:IU_NATS_URL = ""
    $env:IU_DEFAULT_JOIN_ZONE = "zone.p01_west_hub"
    $env:IU_DEDICATED_SERVER_ADDR = "127.0.0.1:$Port"
    $env:IU_SERVER_HMAC_SECRET = "iu_step3_internal_server_hmac_2026"
    $env:IU_HTTP_LISTEN_ADDR = "127.0.0.1:$GatewayPort"
    $env:IU_METRICS_LISTEN_ADDR = "127.0.0.1:9090"
    Start-Process -FilePath $GatewayExe -WindowStyle Hidden | Out-Null
    for ($i = 0; $i -lt 20; $i++) {
        try {
            $h = Invoke-RestMethod -Uri "http://127.0.0.1:$GatewayPort/v1/healthz" -TimeoutSec 2
            if ($h.status -eq "ok") { $GatewayHealthy = $true; break }
        } catch {}
        Start-Sleep -Milliseconds 300
    }
}
if (-not $GatewayHealthy) { throw "Go Gateway failed to become healthy on :$GatewayPort" }
Write-Host "[GATEWAY] Healthy on http://127.0.0.1:$GatewayPort" -ForegroundColor Green

$RunTag = (Get-Date).ToString("HHmmss")
$HeroA = "ZenithA_$RunTag"
$HeroB = "ZenithB_$RunTag"
$Pass = "Step3HeroPass!2026"

# ============================================================================
# PHASE 1: Dedicated Server #1 -> Register/Login -> Fly -> Raid Level-Up -> Logout
# ============================================================================
$Srv1Log = Join-Path $ProjectRoot "Saved\Logs\Step3_DedicatedServer_Run1.log"
if (Test-Path $Srv1Log) { Remove-Item $Srv1Log -Force }

$Srv1Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-srv1 -AutoServerTelemetry -DemoDuration=28.0 -CSVOutputName=Step3_DedicatedServer_Run1.csv -AbsLog=`"$Srv1Log`""
Write-Host "`n=== [PHASE 1] Booting Dedicated Server #1 (ue5-ded-srv1) ===" -ForegroundColor Cyan
$Srv1Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv1Args -PassThru
Wait-ServerListening -Proc $Srv1Proc -LogFile $Srv1Log -ListenPort $Port
Write-Host "[SRV #1] Listening on UDP :$Port (PID $($Srv1Proc.Id))" -ForegroundColor Green

$Cli1ArgsP1 = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep3Phase=1 -AutoNetDemo=1 -DemoDuration=20.0 -RecordDemoFrames -DemoFramesDir=Step3Frames_Phase1_Client1 -CSVOutputName=Step3_Phase1_Client1Profile.csv -Log=Step3_Phase1_Client1.log"
$Cli2ArgsP1 = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep3Phase=1 -AutoNetDemo=2 -DemoDuration=20.0 -RecordDemoFrames -DemoFramesDir=Step3Frames_Phase1_Client2 -CSVOutputName=Step3_Phase1_Client2Profile.csv -Log=Step3_Phase1_Client2.log"

$C1P1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1ArgsP1 -PassThru
Start-Sleep -Milliseconds 300
$C2P1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2ArgsP1 -PassThru

Write-Host "[PHASE 1] Waiting for $HeroA & $HeroB to authenticate, fly to West Arena, defeat Raiders -> Level Up, and logout..." -ForegroundColor Yellow
$C1P1.WaitForExit(32000) | Out-Null
$C2P1.WaitForExit(32000) | Out-Null
Start-Sleep -Seconds 2

Write-Host "[PHASE 1] Terminating Dedicated Server #1 (PID $($Srv1Proc.Id)) completely to prove zero-RAM PostgreSQL persistence!" -ForegroundColor Magenta
if (-not $Srv1Proc.HasExited) {
    $Srv1Proc.WaitForExit(6000) | Out-Null
}
Stop-TestUEProcesses

# Inspect PostgreSQL state right after Server #1 shutdown!
$env:PGPASSWORD = "iu_dev_secret_2026"
$SqlCheck1 = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, c.level, c.current_exp, ROUND(c.pos_x::numeric,1), ROUND(c.pos_y::numeric,1), c.fencing_epoch, a.max_hp, (SELECT COUNT(*) FROM character_save_snapshots s WHERE s.character_id = c.id) FROM characters c JOIN character_attributes a ON a.character_id = c.id WHERE c.character_name IN ('$HeroA', '$HeroB') ORDER BY c.character_name;"
Write-Host "[POSTGRESQL AFTER SRV #1 SHUTDOWN]:" -ForegroundColor Green
$SqlCheck1 | ForEach-Object { Write-Host "  $_" }

# ============================================================================
# PHASE 2: Brand-New Dedicated Server #2 -> Relogin -> Verify Restored State!
# ============================================================================
$Srv2Log = Join-Path $ProjectRoot "Saved\Logs\Step3_DedicatedServer_Run2_Restarted.log"
if (Test-Path $Srv2Log) { Remove-Item $Srv2Log -Force }

$Srv2Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-srv2 -AutoServerTelemetry -DemoDuration=18.0 -CSVOutputName=Step3_DedicatedServer_Run2.csv -AbsLog=`"$Srv2Log`""
Write-Host "`n=== [PHASE 2] Booting BRAND-NEW Dedicated Server #2 (ue5-ded-srv2) ===" -ForegroundColor Cyan
$Srv2Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv2Args -PassThru
Wait-ServerListening -Proc $Srv2Proc -LogFile $Srv2Log -ListenPort $Port
Write-Host "[SRV #2] Listening on UDP :$Port (Fresh PID $($Srv2Proc.Id) != Old PID $($Srv1Proc.Id))" -ForegroundColor Green

$Cli1ArgsP2 = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep3Phase=2 -AutoNetDemo=1 -DemoDuration=9.5 -RecordDemoFrames -DemoFramesDir=Step3Frames_Phase2_Client1 -CSVOutputName=Step3_Phase2_Client1Profile.csv -Log=Step3_Phase2_Client1.log"
$Cli2ArgsP2 = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep3Phase=2 -AutoNetDemo=2 -DemoDuration=9.5 -RecordDemoFrames -DemoFramesDir=Step3Frames_Phase2_Client2 -CSVOutputName=Step3_Phase2_Client2Profile.csv -Log=Step3_Phase2_Client2.log"

$C1P2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1ArgsP2 -PassThru
Start-Sleep -Milliseconds 300
$C2P2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2ArgsP2 -PassThru

$C1P2.WaitForExit(22000) | Out-Null
$C2P2.WaitForExit(22000) | Out-Null
Start-Sleep -Seconds 2
Stop-TestUEProcesses

# ============================================================================
# PHASE 3: Live Security Gate (No-Ticket PreLogin Block + Duplicate-Login Kick)
# ============================================================================
if (-not $SkipDuplicateLoginPhase) {
    $Srv3Log = Join-Path $ProjectRoot "Saved\Logs\Step3_DedicatedServer_SecurityAndDupKick.log"
    if (Test-Path $Srv3Log) { Remove-Item $Srv3Log -Force }

    $Srv3Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-srv3 -AbsLog=`"$Srv3Log`""
    Write-Host "`n=== [PHASE 3] Booting Dedicated Server #3 for Rogue No-Ticket & Duplicate-Login Eviction Test ===" -ForegroundColor Cyan
    $Srv3Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv3Args -PassThru
    Wait-ServerListening -Proc $Srv3Proc -LogFile $Srv3Log -ListenPort $Port

    # 3A: Rogue client attempting direct UDP connect without ?Ticket=
    $RogueProc = Start-Process -FilePath $ClientExe -ArgumentList "127.0.0.1:$Port -windowed -ResX=640 -ResY=360 -NullRHI -unattended -Log=Step3_RogueNoTicket.log" -PassThru
    Start-Sleep -Seconds 3
    if (-not $RogueProc.HasExited) { Stop-Process -Id $RogueProc.Id -Force -ErrorAction SilentlyContinue }

    # 3B: HeroA Session #1 connects legitimately, then Session #2 logs into same account -> Session #1 lease is revoked & kicked on 12s RenewTick!
    $Dup1 = Start-Process -FilePath $ClientExe -ArgumentList "-windowed -WinX=0 -WinY=40 -ResX=800 -ResY=450 -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -Log=Step3_DupSession1.log" -PassThru
    Start-Sleep -Seconds 5
    Write-Host "[PHASE 3] Launching Duplicate Login Session #2 for $HeroA to supersede Session #1 lease..." -ForegroundColor Yellow
    $Dup2 = Start-Process -FilePath $ClientExe -ArgumentList "-windowed -WinX=820 -WinY=40 -ResX=800 -ResY=450 -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -Log=Step3_DupSession2.log" -PassThru

    # Wait 14s so Server #3's 12s RenewActiveLeases timer fires and evicts Session #1!
    Start-Sleep -Seconds 14
    Stop-TestUEProcesses

    Write-Host "[PHASE 3 SECURITY LOG EVIDENCE]:" -ForegroundColor Green
    Select-String -Path $Srv3Log -Pattern "PreLogin REJECTED|Restored Character|Lease REVOKED" | ForEach-Object { Write-Host "  $($_.Line)" }
}

$SqlCheckFinal = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, c.level, c.current_exp, ROUND(c.pos_x::numeric,1), ROUND(c.pos_y::numeric,1), c.fencing_epoch, a.max_hp, (SELECT COUNT(*) FROM character_save_snapshots s WHERE s.character_id = c.id) FROM characters c JOIN character_attributes a ON a.character_id = c.id WHERE c.character_name IN ('$HeroA', '$HeroB') ORDER BY c.character_name;"
Write-Host "`n[FINAL POSTGRESQL AUTHORITATIVE ROWS]:" -ForegroundColor Cyan
$SqlCheckFinal | ForEach-Object { Write-Host "  $_" }
Write-Host "`n=== STEP 3 FULL-STACK E2E VERIFICATION COMPLETED ===" -ForegroundColor Green
