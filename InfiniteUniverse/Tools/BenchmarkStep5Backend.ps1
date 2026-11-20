param(
    [string]$GatewayURL = "http://127.0.0.1:8080",
    [string]$HMACSecret = "iu_step3_internal_server_hmac_2026",
    [int]$PGPort = 5433,
    [int]$Iterations = 60
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http

function Get-HMACSHA256Hex([string]$Secret, [string]$Body) {
    $hmac = New-Object System.Security.Cryptography.HMACSHA256
    $hmac.Key = [System.Text.Encoding]::UTF8.GetBytes($Secret)
    $bytes = $hmac.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Body))
    return ($bytes | ForEach-Object { $_.ToString("x2") }) -join ""
}

function Get-Percentiles([double[]]$Samples) {
    $sorted = $Samples | Sort-Object
    $n = $sorted.Count
    $avg = ($sorted | Measure-Object -Average).Average
    $p50 = $sorted[[math]::Floor($n * 0.50)]
    $p95 = $sorted[[math]::Min($n - 1, [math]::Floor($n * 0.95))]
    $p99 = $sorted[[math]::Min($n - 1, [math]::Floor($n * 0.99))]
    return [PSCustomObject]@{
        Count = $n
        AvgMs = [math]::Round($avg, 2)
        P50Ms = [math]::Round($p50, 2)
        P95Ms = [math]::Round($p95, 2)
        P99Ms = [math]::Round($p99, 2)
    }
}

function New-ClaimedHero([string]$UName, [string]$CName) {
    $Pass = "BenchStep5!2026"
    $RegBody = (@{ username = $UName; email = "$UName@iu.s5bench"; password = $Pass } | ConvertTo-Json -Compress)
    $RegResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/auth/register" -ContentType "application/json" -Body $RegBody
    $Tok = $RegResp.access_token

    $CharBody = (@{ character_name = $CName; race_id = "race.zenith_warrior" } | ConvertTo-Json -Compress)
    $CharResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/characters" -Headers @{ Authorization = "Bearer $Tok" } -ContentType "application/json" -Body $CharBody
    $CID = $CharResp.character_id

    $EnterBody = (@{ character_id = $CID } | ConvertTo-Json -Compress)
    $EnterResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/world/enter" -Headers @{ Authorization = "Bearer $Tok" } -ContentType "application/json" -Body $EnterBody

    $ClaimBody = (@{ ticket_id = $EnterResp.ticket_id; server_id = "bench-s5"; zone_id = "zone.p01_west_hub" } | ConvertTo-Json -Compress)
    $ClaimResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/lease/claim" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $ClaimBody) } -ContentType "application/json" -Body $ClaimBody
    return [PSCustomObject]@{
        CharID = $CID
        Name   = $CName
        Epoch  = [int64]$ClaimResp.fencing_epoch
    }
}

$Tag = (Get-Date).ToString("mmssff")
$HeroA = New-ClaimedHero "S5BenchA_$Tag" "BA_$Tag"
$HeroB = New-ClaimedHero "S5BenchB_$Tag" "BB_$Tag"

# Form 2-player Party
$InvBody = "{`"character_id`":`"$($HeroA.CharID)`",`"character_name`":`"$($HeroA.Name)`",`"level`":2,`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"target_char_id`":`"$($HeroB.CharID)`",`"target_name`":`"$($HeroB.Name)`"}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/party/invite" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $InvBody) } -ContentType "application/json" -Body $InvBody | Out-Null
$AccBody = "{`"character_id`":`"$($HeroB.CharID)`",`"character_name`":`"$($HeroB.Name)`",`"level`":2,`"fencing_epoch`":$($HeroB.Epoch),`"server_id`":`"bench-s5`",`"accept`":true}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/party/respond" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $AccBody) } -ContentType "application/json" -Body $AccBody | Out-Null

# Level HeroA to Lv.2 & grant Vang via loot rolls so HeroA can create a Guild & Donate!
$ExpUp = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"outgoing_damage`":450.0,`"nearby_party_members`":2,`"killed_target`":true,`"idempotency_key`":`"bench_lv2_$Tag`"}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/character/exp" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $ExpUp) } -ContentType "application/json" -Body $ExpUp | Out-Null

for ($m = 1; $m -le 12; $m++) {
    $Lb = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"source_entity_id`":`"mob.prototype_raider`",`"zone_id`":`"zone.p01_west_hub`",`"idempotency_key`":`"bench_seed_${Tag}_$m`"}"
    Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/loot/roll-mob" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $Lb) } -ContentType "application/json" -Body $Lb | Out-Null
}

$GTag = "B" + $Tag.Substring(0, 4)
$GCreate = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"guild_name`":`"BenchGuild_$Tag`",`"guild_tag`":`"$GTag`",`"idempotency_key`":`"bench_gc_$Tag`"}"
$GCreatedResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/guild/create" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $GCreate) } -ContentType "application/json" -Body $GCreate
$GuildID = $GCreatedResp.guild.guild_id

$GJoin = "{`"character_id`":`"$($HeroB.CharID)`",`"fencing_epoch`":$($HeroB.Epoch),`"server_id`":`"bench-s5`",`"guild_id`":`"$GuildID`"}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/guild/join" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $GJoin) } -ContentType "application/json" -Body $GJoin | Out-Null

$Client = New-Object System.Net.Http.HttpClient
$PartyTimes = New-Object System.Collections.Generic.List[double]
$SynergyHitTimes = New-Object System.Collections.Generic.List[double]
$GuildGetTimes = New-Object System.Collections.Generic.List[double]
$GuildDonateTimes = New-Object System.Collections.Generic.List[double]
$ChatFetchTimes = New-Object System.Collections.Generic.List[double]

function Send-BenchPost([string]$Path, [string]$Body) {
    $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL$Path")
    $req.Content = New-Object System.Net.Http.StringContent($Body, [System.Text.Encoding]::UTF8, "application/json")
    $req.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $Body))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $resp = $Client.SendAsync($req).GetAwaiter().GetResult()
    $sw.Stop()
    if (-not $resp.IsSuccessStatusCode) {
        $errTxt = $resp.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        throw "$Path failed ($($resp.StatusCode)): $errTxt"
    }
    return $sw.Elapsed.TotalMilliseconds
}

for ($i = 1; $i -le $Iterations; $i++) {
    $b1 = "{`"character_id`":`"$($HeroA.CharID)`",`"character_name`":`"$($HeroA.Name)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"level`":2,`"hp`":200,`"max_hp`":200,`"energy`":200,`"max_energy`":200}"
    $PartyTimes.Add((Send-BenchPost "/internal/v1/party/get" $b1))

    $b2 = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"outgoing_damage`":25.0,`"nearby_party_members`":2,`"killed_target`":false,`"idempotency_key`":`"bench_syn_${Tag}_$i`"}"
    $SynergyHitTimes.Add((Send-BenchPost "/internal/v1/character/exp" $b2))

    $b3 = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`"}"
    $GuildGetTimes.Add((Send-BenchPost "/internal/v1/guild/get" $b3))

    $b4 = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"amount_coin`":5,`"idempotency_key`":`"bench_don_${Tag}_$i`"}"
    $GuildDonateTimes.Add((Send-BenchPost "/internal/v1/guild/donate" $b4))

    $b5 = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"zone_id`":`"zone.p01_west_hub`",`"has_position`":true}"
    $ChatFetchTimes.Add((Send-BenchPost "/internal/v1/social/chat/fetch" $b5))
}

# Measure 1 Complete Atomic 2-Step P2P Trade Settlement (after 3.0s safety window)
$TReq = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"recipient_char_id`":`"$($HeroB.CharID)`",`"zone_id`":`"zone.p01_west_hub`"}"
$TR = Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/trade/request" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $TReq) } -ContentType "application/json" -Body $TReq
$TID = $TR.trade_id
$TAcc = "{`"character_id`":`"$($HeroB.CharID)`",`"fencing_epoch`":$($HeroB.Epoch),`"server_id`":`"bench-s5`",`"trade_id`":`"$TID`",`"accept`":true}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/trade/respond" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $TAcc) } -ContentType "application/json" -Body $TAcc | Out-Null
$TOff = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"trade_id`":`"$TID`",`"coin_offer`":40,`"item_instance_ids`":[]}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/trade/offer" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $TOff) } -ContentType "application/json" -Body $TOff | Out-Null
$TLkA = "{`"character_id`":`"$($HeroA.CharID)`",`"fencing_epoch`":$($HeroA.Epoch),`"server_id`":`"bench-s5`",`"trade_id`":`"$TID`"}"
$TLkB = "{`"character_id`":`"$($HeroB.CharID)`",`"fencing_epoch`":$($HeroB.Epoch),`"server_id`":`"bench-s5`",`"trade_id`":`"$TID`"}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/trade/lock" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $TLkA) } -ContentType "application/json" -Body $TLkA | Out-Null
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/trade/lock" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $TLkB) } -ContentType "application/json" -Body $TLkB | Out-Null
Start-Sleep -Milliseconds 3150
Send-BenchPost "/internal/v1/trade/confirm" $TLkA | Out-Null
$SettleMs = [math]::Round((Send-BenchPost "/internal/v1/trade/confirm" $TLkB), 2)

foreach ($H in @($HeroA, $HeroB)) {
    $Rel = "{`"character_id`":`"$($H.CharID)`",`"fencing_epoch`":$($H.Epoch),`"server_id`":`"bench-s5`"}"
    Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/lease/release" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $Rel) } -ContentType "application/json" -Body $Rel | Out-Null
}

$Results = @(
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/party/get (5-Player Coord)"; Stats = (Get-Percentiles $PartyTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/character/exp (+15% Synergy)"; Stats = (Get-Percentiles $SynergyHitTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/guild/get (PG18 Roster+Dir)"; Stats = (Get-Percentiles $GuildGetTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/guild/donate (ACID+Ledger)"; Stats = (Get-Percentiles $GuildDonateTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/social/chat/fetch (60m Cull)"; Stats = (Get-Percentiles $ChatFetchTimes.ToArray()) }
)

Write-Host "`n=== STEP 5 GO BACKEND + POSTGRESQL 18 LATENCY BENCHMARK ===" -ForegroundColor Cyan
foreach ($r in $Results) {
    Write-Host ("{0,-50} | N={1,-3} | Avg={2,5} ms | p50={3,5} ms | p95={4,5} ms | p99={5,5} ms" -f $r.Endpoint, $r.Stats.Count, $r.Stats.AvgMs, $r.Stats.P50Ms, $r.Stats.P95Ms, $r.Stats.P99Ms)
}
Write-Host ("POST /internal/v1/trade/confirm (Ordered UUID ACID Settle + 5% Tax) : {0} ms" -f $SettleMs) -ForegroundColor Yellow

$PsqlExe = "C:\Program Files\PostgreSQL\18\bin\psql.exe"
if (Test-Path $PsqlExe) {
    $env:PGPASSWORD = "iu_dev_secret_2026"
    $Drift = (& $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT COUNT(*) FROM character_wallets w WHERE w.standard_coin <> COALESCE((SELECT SUM(l.delta_amount) FROM currency_ledger l WHERE l.character_id = w.character_id), 0);").Trim()
    Write-Host "[GLOBAL LEDGER DRIFT CHECK]: DriftCount = $Drift (Expected: 0)" -ForegroundColor Green
}
