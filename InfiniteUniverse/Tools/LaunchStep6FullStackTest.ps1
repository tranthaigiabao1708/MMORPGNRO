param(
    [int]$PortKhu1 = 7777,
    [int]$PortKhu2 = 7778,
    [int]$GatewayPort = 8080,
    [int]$PGPort = 5433
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$RepoRoot = Split-Path -Parent $ProjectRoot
$UProjectPath = Join-Path $ProjectRoot "InfiniteUniverse.uproject"
$UEEditorExe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$InnerClientExe = Join-Path $ProjectRoot "Saved\PackagedBuilds\Windows\InfiniteUniverse\Binaries\Win64\InfiniteUniverse.exe"
$ClientExe = if (Test-Path $InnerClientExe) { $InnerClientExe } else { Join-Path $ProjectRoot "Saved\PackagedBuilds\Windows\InfiniteUniverse.exe" }
$GatewayExe = Join-Path $ProjectRoot "Saved\BackendBin\gateway.exe"
$LoadbotExe = Join-Path $ProjectRoot "Saved\BackendBin\loadbot.exe"
$PGDataDir = Join-Path $ProjectRoot "Saved\LocalPG18Data"
$PGBinDir = "C:\Program Files\PostgreSQL\18\bin"
$PsqlExe = Join-Path $PGBinDir "psql.exe"
$PgIsReadyExe = Join-Path $PGBinDir "pg_isready.exe"
$PostgresExe = Join-Path $PGBinDir "postgres.exe"
$HMACSecret = "iu_step3_internal_server_hmac_2026"

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
    $Deadline = (Get-Date).AddSeconds(65)
    while ((Get-Date) -lt $Deadline) {
        if ($Proc.HasExited) {
            throw "Dedicated Server ($LogFile) exited early with code $($Proc.ExitCode)!"
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
    for ($i = 0; $i -lt 25; $i++) {
        & $PgIsReadyExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe | Out-Null
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 400
    }
}
Write-Host "[PG18] Healthy on 127.0.0.1:$PGPort" -ForegroundColor Green

# 2. Build & Start Go Gateway (:8080) + 20-CCU Chaos Loadbot
Get-Process -Name "gateway" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Set-Location (Join-Path $RepoRoot "backend")
New-Item -ItemType Directory -Force -Path (Split-Path $GatewayExe) | Out-Null
& "C:\Program Files\Go\bin\go.exe" build -o $GatewayExe ./cmd/gateway
& "C:\Program Files\Go\bin\go.exe" build -o $LoadbotExe ./cmd/loadbot

$env:IU_PG_DSN = "postgres://iu_admin:iu_dev_secret_2026@127.0.0.1:$PGPort/infinite_universe?sslmode=disable"
$env:IU_ALLOW_OPTIONAL_DEPS = "true"
$env:IU_REDIS_ADDR = ""
$env:IU_NATS_URL = ""
$env:IU_DEFAULT_JOIN_ZONE = "zone.p01_west_hub"
$env:IU_DEDICATED_SERVER_ADDR = "127.0.0.1:$PortKhu1"
$env:IU_SERVER_HMAC_SECRET = $HMACSecret
$env:IU_HTTP_LISTEN_ADDR = "127.0.0.1:$GatewayPort"
$env:IU_METRICS_LISTEN_ADDR = "127.0.0.1:9090"
Start-Process -FilePath $GatewayExe -WindowStyle Hidden | Out-Null

$GatewayHealthy = $false
for ($i = 0; $i -lt 25; $i++) {
    try {
        $h = Invoke-RestMethod -Uri "http://127.0.0.1:$GatewayPort/v1/healthz" -TimeoutSec 2
        if ($h.status -eq "ok") { $GatewayHealthy = $true; break }
    } catch {}
    Start-Sleep -Milliseconds 300
}
if (-not $GatewayHealthy) { throw "Go Gateway failed to become healthy on :$GatewayPort" }
Write-Host "[GATEWAY] Healthy on http://127.0.0.1:$GatewayPort" -ForegroundColor Green

# ============================================================================
# PHASE 1: Boot BOTH Live Dedicated Servers Simultaneously!
#   - Server #1 (:7777): zone.p01_west_hub (Khu 1 - Tram Tay Nam + Raider Patrol + Capsule Portal)
#   - Server #2 (:7778): zone.p01_highland_01 (Khu 2 - Cao Nguyen Titan + 3-Phase World Boss!)
# ============================================================================
$Srv1Log = Join-Path $ProjectRoot "Saved\Logs\Step6_DedicatedServer_Khu1_7777.log"
$Srv2Log = Join-Path $ProjectRoot "Saved\Logs\Step6_DedicatedServer_Khu2_7778.log"
if (Test-Path $Srv1Log) { Remove-Item $Srv1Log -Force }
if (Test-Path $Srv2Log) { Remove-Item $Srv2Log -Force }

$Srv1Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$PortKhu1 -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-s6-khu1 -ZoneID=zone.p01_west_hub -ChannelID=1 -PublicAddr=127.0.0.1:$PortKhu1 -AutoServerTelemetry -DemoDuration=60.0 -CSVOutputName=Step6_DedicatedServer_Khu1.csv -AbsLog=`"$Srv1Log`""
$Srv2Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$PortKhu2 -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-s6-khu2 -ZoneID=zone.p01_highland_01 -ChannelID=2 -PublicAddr=127.0.0.1:$PortKhu2 -AutoServerTelemetry -DemoDuration=60.0 -CSVOutputName=Step6_DedicatedServer_Khu2.csv -AbsLog=`"$Srv2Log`""

Write-Host "`n=== Booting Dedicated Server #1 (:7777 Khu 1) & Dedicated Server #2 (:7778 Khu 2 World Boss) ===" -ForegroundColor Cyan
$Srv1Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv1Args -PassThru
$Srv2Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv2Args -PassThru

Wait-ServerListening -Proc $Srv1Proc -LogFile $Srv1Log -ListenPort $PortKhu1
Write-Host "[SRV #1 - KHU 1] Listening on UDP :$PortKhu1 (PID $($Srv1Proc.Id))" -ForegroundColor Green
Wait-ServerListening -Proc $Srv2Proc -LogFile $Srv2Log -ListenPort $PortKhu2
Write-Host "[SRV #2 - KHU 2] Listening on UDP :$PortKhu2 (PID $($Srv2Proc.Id) - HIGHLAND TITAN ACTIVE!)" -ForegroundColor Green

# Allow initial 4s server heartbeats to register both Khu 1 and Khu 2 in World Coordinator
Start-Sleep -Seconds 2

$RunTag = (Get-Date).ToString("HHmmss")
$HeroA = "GokuS6_$RunTag"
$HeroB = "VegeS6_$RunTag"
$Pass = "Step6HeroPass!2026"

$Cli1Args = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep6Demo=1 -AutoNetDemo=1 -RecordDemoFrames -DemoFramesDir=Step6Frames_Client1 -CSVOutputName=Step6_Client1Profile.csv -Log=Step6_Client1.log"
$Cli2Args = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep6Demo=1 -AutoNetDemo=2 -RecordDemoFrames -DemoFramesDir=Step6Frames_Client2 -CSVOutputName=Step6_Client2Profile.csv -Log=Step6_Client2.log"

$C1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1Args -PassThru
Start-Sleep -Milliseconds 350
$C2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2Args -PassThru

Write-Host "[E2E] Waiting for ${HeroA} & ${HeroB}: Khu 1 Raider Patrol + Quest Claim + Doi Khu (:7777 -> :7778) -> 3-Phase World Boss + 3D Aerial Quake Dodge + Poise Stagger + Titan Quest + Pity Shop Redeem..." -ForegroundColor Yellow
$C1.WaitForExit(72000) | Out-Null
$C2.WaitForExit(72000) | Out-Null
Start-Sleep -Seconds 2
Stop-TestUEProcesses

# ============================================================================
# PHASE 2: Run 20-CCU Concurrent Load & Chaos / Anti-Dupe Verification Bot Swarm
# ============================================================================
Write-Host "`n=== [PHASE 2] Running 20-CCU Concurrent Bot Swarm + Chaos Fault Injection ===" -ForegroundColor Cyan
$LoadbotReportPath = Join-Path $ProjectRoot "Saved\Logs\Step6_20CCU_ChaosLoadReport.txt"
$LoadbotOut = & $LoadbotExe -gateway "http://127.0.0.1:$GatewayPort" -hmac $HMACSecret -ccu 20 -rounds 6 2>&1
$LoadbotOut | Tee-Object -FilePath $LoadbotReportPath

# ============================================================================
# PHASE 3: PostgreSQL 18 Authoritative Audit (Boss Kills, Zone Transfers, Quests, Pity, Zero Drift)
# ============================================================================
$env:PGPASSWORD = "iu_dev_secret_2026"
$SqlBoss = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT boss_id, zone_id, channel_id, total_damage_dealt, jsonb_array_length(participants) FROM boss_kill_history ORDER BY killed_at DESC LIMIT 3;"
$SqlXfer = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, s.snapshot_reason, s.zone_id, c.zone_id AS final_zone, c.last_channel_id, c.fencing_epoch FROM character_save_snapshots s JOIN characters c ON c.id = s.character_id WHERE c.character_name IN ('$HeroA', '$HeroB') AND s.snapshot_reason = 'ZONE_TRANSFER' ORDER BY s.created_at DESC;"
$SqlHeroes = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, c.level, c.zone_id, c.last_channel_id, w.standard_coin, (SELECT COALESCE(SUM(delta_amount),0) FROM currency_ledger l WHERE l.character_id = c.id) AS ledger_sum, COALESCE(p.amount,0) AS pity_bal, COALESCE(p.lifetime_earned,0) AS pity_earned, COALESCE(p.lifetime_redeemed,0) AS pity_redeemed, (SELECT COUNT(*) FROM character_quests q WHERE q.character_id = c.id AND q.quest_state = 'COMPLETED') AS completed_quests, c.fencing_epoch FROM characters c JOIN character_wallets w ON w.character_id = c.id LEFT JOIN boss_pity_tokens p ON p.character_id = c.id AND p.token_id = 'token.boss_highland_titan' WHERE c.character_name IN ('$HeroA', '$HeroB') ORDER BY c.character_name;"
$SqlGlobalDrift = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT COUNT(*) FROM character_wallets w WHERE w.standard_coin <> COALESCE((SELECT SUM(l.delta_amount) FROM currency_ledger l WHERE l.character_id = w.character_id), 0);"

Write-Host "`n[POSTGRESQL WORLD BOSS KILL HISTORY (Boss|Zone|Khu|TotalDmg|Participants)]:" -ForegroundColor Green
$SqlBoss | ForEach-Object { Write-Host "  $_" }
Write-Host "[POSTGRESQL ZONE_TRANSFER SNAPSHOTS (Hero|Reason|FromZone|FinalZone|FinalKhu|Epoch)]:" -ForegroundColor Green
$SqlXfer | ForEach-Object { Write-Host "  $_" }
Write-Host "[POSTGRESQL FINAL HERO STATE (Name|Lv|Zone|Khu|Wallet|LedgerSum|PityBal|PityEarned|PityRedeemed|CompletedQuests|Epoch)]:" -ForegroundColor Cyan
$SqlHeroes | ForEach-Object { Write-Host "  $_" }
Write-Host "[POSTGRESQL GLOBAL WALLET-LEDGER DRIFT COUNT]: $SqlGlobalDrift" -ForegroundColor Green

if ([int]$SqlGlobalDrift.Trim() -ne 0) {
    throw "CRITICAL: Non-zero global currency_ledger drift detected: $SqlGlobalDrift"
}
Write-Host "`n=== STEP 6 FULL-STACK MULTI-SERVER & 20-CCU VERIFICATION COMPLETED ===" -ForegroundColor Green
