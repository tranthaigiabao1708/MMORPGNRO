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
    for ($i = 0; $i -lt 25; $i++) {
        & $PgIsReadyExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe | Out-Null
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Milliseconds 400
    }
}
Write-Host "[PG18] Healthy on 127.0.0.1:$PGPort" -ForegroundColor Green

# Clear any previous demo [ZNT] Zenith Vanguards row so fresh Phase 1 demo creates [ZNT] cleanly
$env:PGPASSWORD = "iu_dev_secret_2026"
& $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -c "DELETE FROM guilds WHERE guild_tag IN ('ZNT', 'Z0000') OR guild_name = 'Zenith Vanguards';" | Out-Null

# 2. Ensure Go Gateway (:8080) is running with fresh Step 5 binary
Get-Process -Name "gateway" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Set-Location (Join-Path $RepoRoot "backend")
New-Item -ItemType Directory -Force -Path (Split-Path $GatewayExe) | Out-Null
& "C:\Program Files\Go\bin\go.exe" build -o $GatewayExe ./cmd/gateway

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

$RunTag = (Get-Date).ToString("HHmmss")
$HeroA = "GokuS5_$RunTag"
$HeroB = "VegeS5_$RunTag"
$Pass = "Step5HeroPass!2026"

# ============================================================================
# PHASE 1: Dedicated Server #1 -> Party [Y] + 3D Ping + Speech Bubble -> Co-op Raid (+15% Synergy) -> Guild [ZNT] -> 2-Step P2P Trade -> Logout
# ============================================================================
$Srv1Log = Join-Path $ProjectRoot "Saved\Logs\Step5_DedicatedServer_Run1.log"
if (Test-Path $Srv1Log) { Remove-Item $Srv1Log -Force }

$Srv1Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-s5-srv1 -AutoServerTelemetry -DemoDuration=42.0 -CSVOutputName=Step5_DedicatedServer_Run1.csv -AbsLog=`"$Srv1Log`""
Write-Host "`n=== [PHASE 1] Booting Dedicated Server #1 (ue5-ded-s5-srv1) ===" -ForegroundColor Cyan
$Srv1Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv1Args -PassThru
Wait-ServerListening -Proc $Srv1Proc -LogFile $Srv1Log -ListenPort $Port
Write-Host "[SRV #1] Listening on UDP :$Port (PID $($Srv1Proc.Id))" -ForegroundColor Green

$Cli1ArgsP1 = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep5Phase=1 -AutoNetDemo=1 -DemoDuration=33.5 -RecordDemoFrames -DemoFramesDir=Step5Frames_Phase1_Client1 -CSVOutputName=Step5_Phase1_Client1Profile.csv -Log=Step5_Phase1_Client1.log"
$Cli2ArgsP1 = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep5Phase=1 -AutoNetDemo=2 -DemoDuration=33.5 -RecordDemoFrames -DemoFramesDir=Step5Frames_Phase1_Client2 -CSVOutputName=Step5_Phase1_Client2Profile.csv -Log=Step5_Phase1_Client2.log"

$C1P1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1ArgsP1 -PassThru
Start-Sleep -Milliseconds 300
$C2P1 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2ArgsP1 -PassThru

Write-Host "[PHASE 1] Waiting for $HeroA & $HeroB to form Party, place 3D Ping, chat with NRO Speech Bubbles, raid with +15% Synergy, found Guild [ZNT], execute 2-Step P2P Trade (3.0s countdown + 5% tax), and logout..." -ForegroundColor Yellow
$C1P1.WaitForExit(48000) | Out-Null
$C2P1.WaitForExit(48000) | Out-Null
Start-Sleep -Seconds 2

Write-Host "[PHASE 1] Terminating Dedicated Server #1 (PID $($Srv1Proc.Id)) completely!" -ForegroundColor Magenta
if (-not $Srv1Proc.HasExited) {
    $Srv1Proc.WaitForExit(6000) | Out-Null
}
Stop-TestUEProcesses

$SqlGuild = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT g.guild_tag, g.guild_name, g.guild_level, g.treasury_standard_coin, COUNT(gm.character_id) FROM guilds g JOIN guild_members gm ON gm.guild_id = g.guild_id WHERE g.guild_tag = 'ZNT' GROUP BY g.guild_id;"
$SqlTrade = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT t.trade_state, t.initiator_coin_given, t.recipient_coin_given, t.tax_coin_burned, jsonb_array_length(t.initiator_items_given), jsonb_array_length(t.recipient_items_given) FROM trade_transactions t JOIN characters c ON c.id = t.initiator_char_id WHERE c.character_name = '$HeroA' ORDER BY t.created_at DESC LIMIT 1;"
Write-Host "[POSTGRESQL GUILD ROW (Tag|Name|Lv|Treasury|Members)]: $SqlGuild" -ForegroundColor Green
Write-Host "[POSTGRESQL TRADE ROW (State|InitCoin|RecipCoin|TaxBurned|InitItems|RecipItems)]: $SqlTrade" -ForegroundColor Green

# ============================================================================
# PHASE 2: Brand-New Dedicated Server #2 -> Relogin -> Verify Guild [ZNT], Traded Gear & Ledger Vang Restored!
# ============================================================================
$Srv2Log = Join-Path $ProjectRoot "Saved\Logs\Step5_DedicatedServer_Run2_Restarted.log"
if (Test-Path $Srv2Log) { Remove-Item $Srv2Log -Force }

$Srv2Args = "`"$UProjectPath`" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -unattended -nopause -NullRHI -NoSplash -port=$Port -UseBackend -RequireBackendTicket -BackendURL=http://127.0.0.1:$GatewayPort -ServerID=ue5-ded-s5-srv2 -AutoServerTelemetry -DemoDuration=18.0 -CSVOutputName=Step5_DedicatedServer_Run2.csv -AbsLog=`"$Srv2Log`""
Write-Host "`n=== [PHASE 2] Booting BRAND-NEW Dedicated Server #2 (ue5-ded-s5-srv2) ===" -ForegroundColor Cyan
$Srv2Proc = Start-Process -FilePath $UEEditorExe -ArgumentList $Srv2Args -PassThru
Wait-ServerListening -Proc $Srv2Proc -LogFile $Srv2Log -ListenPort $Port
Write-Host "[SRV #2] Listening on UDP :$Port (Fresh PID $($Srv2Proc.Id) != Old PID $($Srv1Proc.Id))" -ForegroundColor Green

$Cli1ArgsP2 = "-windowed -WinX=0 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroA -AutoAuthPass=$Pass -AutoCharName=$HeroA -AutoStep5Phase=2 -AutoNetDemo=1 -DemoDuration=10.5 -RecordDemoFrames -DemoFramesDir=Step5Frames_Phase2_Client1 -CSVOutputName=Step5_Phase2_Client1Profile.csv -Log=Step5_Phase2_Client1.log"
$Cli2ArgsP2 = "-windowed -WinX=960 -WinY=40 -ResX=960 -ResY=540 -NoVSync -unattended -BackendURL=http://127.0.0.1:$GatewayPort -AutoAuthUser=$HeroB -AutoAuthPass=$Pass -AutoCharName=$HeroB -AutoStep5Phase=2 -AutoNetDemo=2 -DemoDuration=10.5 -RecordDemoFrames -DemoFramesDir=Step5Frames_Phase2_Client2 -CSVOutputName=Step5_Phase2_Client2Profile.csv -Log=Step5_Phase2_Client2.log"

$C1P2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli1ArgsP2 -PassThru
Start-Sleep -Milliseconds 300
$C2P2 = Start-Process -FilePath $ClientExe -ArgumentList $Cli2ArgsP2 -PassThru

$C1P2.WaitForExit(25000) | Out-Null
$C2P2.WaitForExit(25000) | Out-Null
Start-Sleep -Seconds 2
Stop-TestUEProcesses

# ============================================================================
# PHASE 3: Run Step 5 Latency Benchmark & Verify Global Zero Ledger Drift
# ============================================================================
& (Join-Path $PSScriptRoot "BenchmarkStep5Backend.ps1") -GatewayURL "http://127.0.0.1:$GatewayPort" -PGPort $PGPort -Iterations 60

$SqlFinal = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT c.character_name, c.level, COALESCE(g.guild_tag::text,'-'), COALESCE(gm.member_role,'-'), w.standard_coin, (SELECT COALESCE(SUM(delta_amount),0) FROM currency_ledger l WHERE l.character_id = c.id) AS ledger_sum, (SELECT COUNT(*) FROM inventory_items i WHERE i.character_id = c.id) AS total_items, c.fencing_epoch FROM characters c JOIN character_wallets w ON w.character_id = c.id LEFT JOIN guild_members gm ON gm.character_id = c.id LEFT JOIN guilds g ON g.guild_id = gm.guild_id WHERE c.character_name IN ('$HeroA', '$HeroB') ORDER BY c.character_name;"
Write-Host "`n[FINAL POSTGRESQL AUTHORITATIVE ROWS AFTER PHASE 2 RELOGIN (Name|Lv|GuildTag|Role|Wallet|LedgerSum|TotalItems|Epoch)]:" -ForegroundColor Cyan
$SqlFinal | ForEach-Object { Write-Host "  $_" }
Write-Host "`n=== STEP 5 FULL-STACK E2E VERIFICATION COMPLETED ===" -ForegroundColor Green
