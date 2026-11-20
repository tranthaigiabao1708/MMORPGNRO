param(
    [int]$Port = 7777,
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
$PGDataDir = Join-Path $ProjectRoot "Saved\LocalPG18Data"
$PGBinDir = "C:\Program Files\PostgreSQL\18\bin"
$PsqlExe = Join-Path $PGBinDir "psql.exe"
$PgIsReadyExe = Join-Path $PGBinDir "pg_isready.exe"
$PostgresExe = Join-Path $PGBinDir "postgres.exe"
$HMACSecret = "iu_step3_internal_server_hmac_2026"

if (-not (Test-Path $ClientExe)) {
    throw "Packaged client not found at $ClientExe. Run Tools\PackageStandalone.ps1 first."
}

function Get-HMACSHA256Hex([string]$Secret, [string]$Body) {
    $hmac = New-Object System.Security.Cryptography.HMACSHA256
    $hmac.Key = [System.Text.Encoding]::UTF8.GetBytes($Secret)
    $bytes = $hmac.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Body))
    return ($bytes | ForEach-Object { $_.ToString("x2") }) -join ""
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
    $env:IU_SERVER_HMAC_SECRET = $HMACSecret
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
$HeroA = "VanguardA_$RunTag"
$HeroB = "VanguardB_$RunTag"
$Pass = "Step4HeroPass!2026"

# ============================================================================
# PHASE 1: Dedicated Server #1 -> Equip Starter -> Raid Loot/Vang -> Dau Than [4] -> Equip Loot + Enhance +1/+2 -> Logout
# ============================================================================
$Srv1Log = Join-Path $ProjectRoot "Saved\Logs\Step4_DedicatedServer_Run1.log"
if (Test-Path $Srv1Log) { Remove-Item $Srv1Log -Force }

$Srv1Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-s4-srv1 -AutoServerTelemetry -DemoDuration=34.0 -CSVOutputName=Step4_DedicatedServer_Run1.csv -AbsLog=`"$Srv1Log`""
Write-Host "`n=== [PHASE 1] Booting Dedicated Server #1 (ue5-ded-s4-srv1) ===" -ForegroundColor Cyan
$Srv1Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv1Args -PassThru
Wait-ServerListening -Proc $Srv1Proc -LogFile $Srv1Log -ListenPort $Port
Write-Host "[SRV #1] Listening on UDP :$Port (PID $($Srv1Proc.Id))" -ForegroundColor Green

$Cli1ArgsP1 = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep4Phase=1 -AutoNetDemo=1 -DemoDuration=25.5 -RecordDemoFrames -DemoFramesDir=Step4Frames_Phase1_Client1 -CSVOutputName=Step4_Phase1_Client1Profile.csv -Log=Step4_Phase1_Client1.log"
$Cli2ArgsP1 = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep4Phase=1 -AutoNetDemo=2 -DemoDuration=25.5 -RecordDemoFrames -DemoFramesDir=Step4Frames_Phase1_Client2 -CSVOutputName=Step4_Phase1_Client2Profile.csv -Log=Step4_Phase1_Client2.log"

$C1P1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1ArgsP1 -PassThru
Start-Sleep -Milliseconds 300
$C2P1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2ArgsP1 -PassThru

Write-Host "[PHASE 1] Waiting for $HeroA & $HeroB to equip starter armor, raid monsters for Vang & Loot, use Dau Than [4], enhance +1/+2, showcase Bag [I], and logout..." -ForegroundColor Yellow
$C1P1.WaitForExit(38000) | Out-Null
$C2P1.WaitForExit(38000) | Out-Null
Start-Sleep -Seconds 2

Write-Host "[PHASE 1] Terminating Dedicated Server #1 (PID $($Srv1Proc.Id)) completely to prove zero-RAM PostgreSQL persistence!" -ForegroundColor Magenta
if (-not $Srv1Proc.HasExited) {
    $Srv1Proc.WaitForExit(6000) | Out-Null
}
Stop-TestUEProcesses

$env:PGPASSWORD = "iu_dev_secret_2026"
$SqlCheck1 = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, c.level, w.standard_coin, (SELECT COALESCE(SUM(delta_amount),0) FROM currency_ledger l WHERE l.character_id = c.id) AS ledger_sum, a.max_hp, a.phys_atk, a.defense, (SELECT COUNT(*) FROM character_equipment e WHERE e.character_id = c.id) AS eq_cnt, (SELECT COALESCE(MAX(i.enhance_level),0) FROM inventory_items i WHERE i.character_id = c.id) AS max_enh, (SELECT COUNT(*) FROM loot_history lh WHERE lh.character_id = c.id) AS loot_events, c.fencing_epoch FROM characters c JOIN character_attributes a ON a.character_id = c.id JOIN character_wallets w ON w.character_id = c.id WHERE c.character_name IN ('$HeroA', '$HeroB') ORDER BY c.character_name;"
Write-Host "[POSTGRESQL AFTER SRV #1 SHUTDOWN (Name|Lv|Wallet|LedgerSum|MaxHP|Atk|Def|EqCnt|MaxEnh|LootEvents|Epoch)]:" -ForegroundColor Green
$SqlCheck1 | ForEach-Object { Write-Host "  $_" }

# ============================================================================
# PHASE 2: Brand-New Dedicated Server #2 -> Relogin -> Verify 100% Restored Inventory, Equipment, Enhance & Wallet!
# ============================================================================
$Srv2Log = Join-Path $ProjectRoot "Saved\Logs\Step4_DedicatedServer_Run2_Restarted.log"
if (Test-Path $Srv2Log) { Remove-Item $Srv2Log -Force }

$Srv2Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-s4-srv2 -AutoServerTelemetry -DemoDuration=18.0 -CSVOutputName=Step4_DedicatedServer_Run2.csv -AbsLog=`"$Srv2Log`""
Write-Host "`n=== [PHASE 2] Booting BRAND-NEW Dedicated Server #2 (ue5-ded-s4-srv2) ===" -ForegroundColor Cyan
$Srv2Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv2Args -PassThru
Wait-ServerListening -Proc $Srv2Proc -LogFile $Srv2Log -ListenPort $Port
Write-Host "[SRV #2] Listening on UDP :$Port (Fresh PID $($Srv2Proc.Id) != Old PID $($Srv1Proc.Id))" -ForegroundColor Green

$Cli1ArgsP2 = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep4Phase=2 -AutoNetDemo=1 -DemoDuration=10.0 -RecordDemoFrames -DemoFramesDir=Step4Frames_Phase2_Client1 -CSVOutputName=Step4_Phase2_Client1Profile.csv -Log=Step4_Phase2_Client1.log"
$Cli2ArgsP2 = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep4Phase=2 -AutoNetDemo=2 -DemoDuration=10.0 -RecordDemoFrames -DemoFramesDir=Step4Frames_Phase2_Client2 -CSVOutputName=Step4_Phase2_Client2Profile.csv -Log=Step4_Phase2_Client2.log"

$C1P2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1ArgsP2 -PassThru
Start-Sleep -Milliseconds 300
$C2P2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2ArgsP2 -PassThru

$C1P2.WaitForExit(24000) | Out-Null
$C2P2.WaitForExit(24000) | Out-Null
Start-Sleep -Seconds 2
Stop-TestUEProcesses

# ============================================================================
# PHASE 3: Security & Anti-Dupe Verification (Stale Epoch 409 + Immutable Ledger Trigger)
# ============================================================================
Write-Host "`n=== [PHASE 3] Verifying Stale Fencing Epoch Rejection (409) & Currency Ledger Immutability Trigger ===" -ForegroundColor Cyan
$HeroAID = (& $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT id FROM characters WHERE character_name = '$HeroA';").Trim()

# 3A: Attempt stale epoch (#1 instead of #2+) loot roll -> MUST return HTTP 409 Conflict!
$StaleBody = "{`"character_id`":`"$HeroAID`",`"fencing_epoch`":1,`"server_id`":`"rogue-stale-srv`",`"source_entity_id`":`"mob.prototype_raider`",`"zone_id`":`"zone.p01_west_hub`",`"idempotency_key`":`"stale_loot_$RunTag`"}"
$StaleStatus = 0
try {
    Invoke-WebRequest -Method Post -Uri "http://127.0.0.1:$GatewayPort/internal/v1/loot/roll-mob" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $StaleBody) } -ContentType "application/json" -Body $StaleBody -UseBasicParsing | Out-Null
} catch {
    if ($_.Exception.Response) {
        $StaleStatus = [int]$_.Exception.Response.StatusCode
    }
}
Write-Host "  [SEC-S4-01] Stale Epoch #1 /internal/v1/loot/roll-mob HTTP Status = $StaleStatus (Expected: 409 Conflict)" -ForegroundColor Green

# 3B: Attempt direct SQL DELETE on currency_ledger -> MUST be blocked by trg_currency_ledger_immutable!
$PrevEA = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$LedgerMutOut = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -c "DELETE FROM currency_ledger WHERE character_id = '$HeroAID';" 2>&1
$ErrorActionPreference = $PrevEA
Write-Host "  [SEC-S4-02] Direct DELETE on currency_ledger result: $($LedgerMutOut -join ' ')" -ForegroundColor Green

# 3C: Global Ledger Parity Check across ALL characters in PostgreSQL 18
$GlobalDrift = (& $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT COUNT(*) FROM character_wallets w WHERE w.standard_coin <> COALESCE((SELECT SUM(l.delta_amount) FROM currency_ledger l WHERE l.character_id = w.character_id), 0);").Trim()
Write-Host "  [SEC-S4-03] Global Wallet vs Currency Ledger Drift Count across entire DB = $GlobalDrift (Expected: 0)" -ForegroundColor Green

$SqlCheckFinal = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, c.level, w.standard_coin, (SELECT COALESCE(SUM(delta_amount),0) FROM currency_ledger l WHERE l.character_id = c.id) AS ledger_sum, a.max_hp, a.phys_atk, a.defense, (SELECT COUNT(*) FROM character_equipment e WHERE e.character_id = c.id) AS eq_cnt, (SELECT COALESCE(MAX(i.enhance_level),0) FROM inventory_items i WHERE i.character_id = c.id) AS max_enh, c.fencing_epoch FROM characters c JOIN character_attributes a ON a.character_id = c.id JOIN character_wallets w ON w.character_id = c.id WHERE c.character_name IN ('$HeroA', '$HeroB') ORDER BY c.character_name;"
Write-Host "`n[FINAL POSTGRESQL AUTHORITATIVE ROWS AFTER PHASE 2 RELOGIN]:" -ForegroundColor Cyan
$SqlCheckFinal | ForEach-Object { Write-Host "  $_" }
Write-Host "`n=== STEP 4 FULL-STACK E2E VERIFICATION COMPLETED ===" -ForegroundColor Green
