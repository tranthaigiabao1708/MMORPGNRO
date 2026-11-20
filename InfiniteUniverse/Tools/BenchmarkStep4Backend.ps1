param(
    [string]$GatewayURL = "http://127.0.0.1:8080",
    [string]$HMACSecret = "iu_step3_internal_server_hmac_2026",
    [int]$PGPort = 5433,
    [int]$Iterations = 100
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

$Tag = (Get-Date).ToString("HHmmssfff")
$User = "S4Bench_$Tag"
$Pass = "BenchSecret!2026"

$RegBody = (@{ username = $User; email = "$User@iu.bench"; password = $Pass } | ConvertTo-Json -Compress)
$RegResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/auth/register" -ContentType "application/json" -Body $RegBody
$Token = $RegResp.access_token

$CharBody = (@{ character_name = "SB_$Tag"; race_id = "race.zenith_warrior" } | ConvertTo-Json -Compress)
$CharResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/characters" -Headers @{ Authorization = "Bearer $Token" } -ContentType "application/json" -Body $CharBody
$CharID = $CharResp.character_id

$EnterBody = (@{ character_id = $CharID } | ConvertTo-Json -Compress)
$EnterResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/world/enter" -Headers @{ Authorization = "Bearer $Token" } -ContentType "application/json" -Body $EnterBody

$ClaimBody = (@{ ticket_id = $EnterResp.ticket_id; server_id = "bench-s4"; zone_id = "zone.p01_west_hub" } | ConvertTo-Json -Compress)
$ClaimResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/lease/claim" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $ClaimBody) } -ContentType "application/json" -Body $ClaimBody
$Epoch = [int64]$ClaimResp.fencing_epoch

# Fetch initial inventory to get Starter Tunic UUID
$InitBody = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`"}"
$InitSnap = Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/inventory/get" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $InitBody) } -ContentType "application/json" -Body $InitBody
$TunicID = ($InitSnap.bag_items | Where-Object { $_.item_def_id -eq "item.body_recruit_tunic_01" } | Select-Object -First 1).instance_id

$Client = New-Object System.Net.Http.HttpClient
$GetInvTimes = New-Object System.Collections.Generic.List[double]
$LootTimes = New-Object System.Collections.Generic.List[double]
$EquipTimes = New-Object System.Collections.Generic.List[double]
$SellTimes = New-Object System.Collections.Generic.List[double]
$EnhanceTimes = New-Object System.Collections.Generic.List[double]

for ($i = 1; $i -le $Iterations; $i++) {
    # 1. GET INVENTORY SNAPSHOT
    $gb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`"}"
    $req1 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/inventory/get")
    $req1.Content = New-Object System.Net.Http.StringContent($gb, [System.Text.Encoding]::UTF8, "application/json")
    $req1.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $gb))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $resp1 = $Client.SendAsync($req1).GetAwaiter().GetResult()
    $sw.Stop()
    if (-not $resp1.IsSuccessStatusCode) { throw "inventory/get failed at iter $i" }
    $GetInvTimes.Add($sw.Elapsed.TotalMilliseconds)

    # 2. CSPRNG MONSTER LOOT + LEDGER COIN GRANT
    $lb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`",`"source_entity_id`":`"mob.prototype_raider`",`"zone_id`":`"zone.p01_west_hub`",`"idempotency_key`":`"bench_loot_${Tag}_$i`"}"
    $req2 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/loot/roll-mob")
    $req2.Content = New-Object System.Net.Http.StringContent($lb, [System.Text.Encoding]::UTF8, "application/json")
    $req2.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $lb))
    $sw2 = [System.Diagnostics.Stopwatch]::StartNew()
    $resp2 = $Client.SendAsync($req2).GetAwaiter().GetResult()
    $sw2.Stop()
    if (-not $resp2.IsSuccessStatusCode) { throw "loot/roll-mob failed at iter $i" }
    $LootTimes.Add($sw2.Elapsed.TotalMilliseconds)
    $lootJson = $resp2.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json

    # 3. EQUIP / UNEQUIP TOGGLE + STAT RECOMPILE
    if ($i % 2 -eq 1) {
        $eqb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`",`"item_instance_id`":`"$TunicID`"}"
        $req3 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/inventory/equip")
    } else {
        $eqb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`",`"slot_id`":`"slot.body`"}"
        $req3 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/inventory/unequip")
    }
    $req3.Content = New-Object System.Net.Http.StringContent($eqb, [System.Text.Encoding]::UTF8, "application/json")
    $req3.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $eqb))
    $sw3 = [System.Diagnostics.Stopwatch]::StartNew()
    $resp3 = $Client.SendAsync($req3).GetAwaiter().GetResult()
    $sw3.Stop()
    if (-not $resp3.IsSuccessStatusCode) { throw "equip/unequip failed at iter $i" }
    $EquipTimes.Add($sw3.Elapsed.TotalMilliseconds)

    # 4. SELL SPARE BAG LOOT TO KEEP BAG CLEAN & BENCHMARK CURRENCY LEDGER CREDIT
    $sellCandidate = $lootJson.inventory.bag_items | Where-Object { $_.instance_id -ne $TunicID -and $_.item_def_id -ne "item.mat_crimson_ore_t1" -and $_.sell_price_coin -gt 0 } | Select-Object -First 1
    if ($null -ne $sellCandidate) {
        $sb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`",`"item_instance_id`":`"$($sellCandidate.instance_id)`",`"quantity`":1,`"idempotency_key`":`"bench_sell_${Tag}_$i`"}"
        $req4 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/inventory/sell")
        $req4.Content = New-Object System.Net.Http.StringContent($sb, [System.Text.Encoding]::UTF8, "application/json")
        $req4.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $sb))
        $sw4 = [System.Diagnostics.Stopwatch]::StartNew()
        $resp4 = $Client.SendAsync($req4).GetAwaiter().GetResult()
        $sw4.Stop()
        if ($resp4.IsSuccessStatusCode) {
            $SellTimes.Add($sw4.Elapsed.TotalMilliseconds)
        }
    }

    # 5. SAFE ENHANCEMENT WHEN ORE & COINS AVAILABLE (UP TO +5 MAX ON STARTER TUNIC)
    if ($lootJson.inventory.crimson_ore_count -ge 3 -and $lootJson.inventory.standard_coin -ge 1000) {
        $enb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`",`"item_instance_id`":`"$TunicID`",`"idempotency_key`":`"bench_enh_${Tag}_$i`"}"
        $req5 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/economy/enhance")
        $req5.Content = New-Object System.Net.Http.StringContent($enb, [System.Text.Encoding]::UTF8, "application/json")
        $req5.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $enb))
        $sw5 = [System.Diagnostics.Stopwatch]::StartNew()
        $resp5 = $Client.SendAsync($req5).GetAwaiter().GetResult()
        $sw5.Stop()
        if ($resp5.IsSuccessStatusCode) {
            $EnhanceTimes.Add($sw5.Elapsed.TotalMilliseconds)
        }
    }
}

$RelBody = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-s4`"}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/lease/release" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $RelBody) } -ContentType "application/json" -Body $RelBody | Out-Null

$Results = @(
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/inventory/get (Snapshot)"; Stats = (Get-Percentiles $GetInvTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/loot/roll-mob (CSPRNG+Ledger)"; Stats = (Get-Percentiles $LootTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/inventory/equip+unequip (ACID)"; Stats = (Get-Percentiles $EquipTimes.ToArray()) }
)
if ($SellTimes.Count -gt 0) {
    $Results += [PSCustomObject]@{ Endpoint = "POST /internal/v1/inventory/sell (ACID+Ledger)"; Stats = (Get-Percentiles $SellTimes.ToArray()) }
}
if ($EnhanceTimes.Count -gt 0) {
    $Results += [PSCustomObject]@{ Endpoint = "POST /internal/v1/economy/enhance (CSPRNG+Ledger)"; Stats = (Get-Percentiles $EnhanceTimes.ToArray()) }
}

Write-Host "`n=== STEP 4 GO BACKEND + POSTGRESQL 18 LATENCY BENCHMARK ===" -ForegroundColor Cyan
foreach ($r in $Results) {
    Write-Host ("{0,-52} | N={1,-3} | Avg={2,5} ms | p50={3,5} ms | p95={4,5} ms | p99={5,5} ms" -f $r.Endpoint, $r.Stats.Count, $r.Stats.AvgMs, $r.Stats.P50Ms, $r.Stats.P95Ms, $r.Stats.P99Ms)
}

$PsqlExe = "C:\Program Files\PostgreSQL\18\bin\psql.exe"
if (Test-Path $PsqlExe) {
    $env:PGPASSWORD = "iu_dev_secret_2026"
    $Parity = & $PsqlExe -h 127.0.0.1 -p $PGPort -U iu_admin -d infinite_universe -t -A -c "SELECT w.standard_coin, COALESCE(SUM(l.delta_amount),0), (w.standard_coin - COALESCE(SUM(l.delta_amount),0)) AS drift, COUNT(l.character_id) FROM character_wallets w LEFT JOIN currency_ledger l ON l.character_id = w.character_id WHERE w.character_id = '$CharID' GROUP BY w.standard_coin;"
    Write-Host "[LEDGER PARITY CHECK FOR $CharID]: Wallet|LedgerSum|Drift|TxCount = $Parity" -ForegroundColor Green
}
