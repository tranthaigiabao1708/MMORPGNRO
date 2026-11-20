param(
    [string]$GatewayURL = "http://127.0.0.1:8080",
    [string]$HMACSecret = "iu_step3_internal_server_hmac_2026",
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
$User = "Bench_$Tag"
$Pass = "BenchSecret!2026"

$RegBody = (@{ username = $User; email = "$User@iu.bench"; password = $Pass } | ConvertTo-Json -Compress)
$RegResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/auth/register" -ContentType "application/json" -Body $RegBody
$Token = $RegResp.access_token

$CharBody = (@{ character_name = "B_$Tag"; race_id = "race.zenith_warrior" } | ConvertTo-Json -Compress)
$CharResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/characters" -Headers @{ Authorization = "Bearer $Token" } -ContentType "application/json" -Body $CharBody
$CharID = $CharResp.character_id

$EnterBody = (@{ character_id = $CharID } | ConvertTo-Json -Compress)
$EnterResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/v1/world/enter" -Headers @{ Authorization = "Bearer $Token" } -ContentType "application/json" -Body $EnterBody

$ClaimBody = (@{ ticket_id = $EnterResp.ticket_id; server_id = "bench-srv"; zone_id = "zone.p01_west_hub" } | ConvertTo-Json -Compress)
$ClaimResp = Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/lease/claim" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $ClaimBody) } -ContentType "application/json" -Body $ClaimBody
$Epoch = [int64]$ClaimResp.fencing_epoch

$Client = New-Object System.Net.Http.HttpClient
$RenewTimes = New-Object System.Collections.Generic.List[double]
$SaveTimes = New-Object System.Collections.Generic.List[double]
$ExpTimes = New-Object System.Collections.Generic.List[double]
$PotTimes = New-Object System.Collections.Generic.List[double]

for ($i = 1; $i -le $Iterations; $i++) {
    # 1. Lease Renew
    $rb = "{`"server_id`":`"bench-srv`",`"leases`":[{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch}]}"
    $req = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/lease/renew")
    $req.Content = New-Object System.Net.Http.StringContent($rb, [System.Text.Encoding]::UTF8, "application/json")
    $req.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $rb))
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $resp = $Client.SendAsync($req).GetAwaiter().GetResult()
    $sw.Stop()
    if (-not $resp.IsSuccessStatusCode) { throw "Renew failed at iter $i" }
    $RenewTimes.Add($sw.Elapsed.TotalMilliseconds)

    # 2. Fenced Checkpoint Save (ACID characters + character_attributes + character_save_snapshots)
    $sb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-srv`",`"snapshot_reason`":`"PERIODIC_60S`",`"zone_id`":`"zone.p01_west_hub`",`"pos_x`":-2050.5,`"pos_y`":140.0,`"pos_z`":112.0,`"rot_yaw`":90.0,`"is_alive`":true,`"hp`":200.0,`"energy`":200.0,`"enemy_kills`":$i,`"pvp_kills`":0,`"deaths`":0}"
    $req2 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/character/save")
    $req2.Content = New-Object System.Net.Http.StringContent($sb, [System.Text.Encoding]::UTF8, "application/json")
    $req2.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $sb))
    $sw2 = [System.Diagnostics.Stopwatch]::StartNew()
    $resp2 = $Client.SendAsync($req2).GetAwaiter().GetResult()
    $sw2.Stop()
    if (-not $resp2.IsSuccessStatusCode) { throw "Save failed at iter $i" }
    $SaveTimes.Add($sw2.Elapsed.TotalMilliseconds)

    # 3. Fenced GrantEXP + Potential (ACID FOR UPDATE)
    $eb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-srv`",`"exp_gain`":15,`"potential_gain`":25000,`"idempotency_key`":`"bench_idem_${Tag}_$i`"}"
    $req3 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/character/exp")
    $req3.Content = New-Object System.Net.Http.StringContent($eb, [System.Text.Encoding]::UTF8, "application/json")
    $req3.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $eb))
    $sw3 = [System.Diagnostics.Stopwatch]::StartNew()
    $resp3 = $Client.SendAsync($req3).GetAwaiter().GetResult()
    $sw3.Stop()
    if (-not $resp3.IsSuccessStatusCode) { throw "Exp failed at iter $i" }
    $ExpTimes.Add($sw3.Elapsed.TotalMilliseconds)

    # 4. Fenced AllocatePotential (ACID FOR UPDATE escalating cost)
    $statCode = if ($i % 3 -eq 1) { "hp" } elseif ($i % 3 -eq 2) { "ki" } else { "atk" }
    $pb = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-srv`",`"stat_type`":`"$statCode`"}"
    $req4 = New-Object System.Net.Http.HttpRequestMessage([System.Net.Http.HttpMethod]::Post, "$GatewayURL/internal/v1/character/potential")
    $req4.Content = New-Object System.Net.Http.StringContent($pb, [System.Text.Encoding]::UTF8, "application/json")
    $req4.Headers.Add("X-IU-Server-HMAC", (Get-HMACSHA256Hex $HMACSecret $pb))
    $sw4 = [System.Diagnostics.Stopwatch]::StartNew()
    $resp4 = $Client.SendAsync($req4).GetAwaiter().GetResult()
    $sw4.Stop()
    if (-not $resp4.IsSuccessStatusCode) { throw "AllocatePotential failed at iter $i" }
    $PotTimes.Add($sw4.Elapsed.TotalMilliseconds)
}

$RelBody = "{`"character_id`":`"$CharID`",`"fencing_epoch`":$Epoch,`"server_id`":`"bench-srv`"}"
Invoke-RestMethod -Method Post -Uri "$GatewayURL/internal/v1/lease/release" -Headers @{ "X-IU-Server-HMAC" = (Get-HMACSHA256Hex $HMACSecret $RelBody) } -ContentType "application/json" -Body $RelBody | Out-Null

$Results = @(
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/lease/renew"; Stats = (Get-Percentiles $RenewTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/character/save (3-Table ACID)"; Stats = (Get-Percentiles $SaveTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/character/exp (LV+Potential ACID)"; Stats = (Get-Percentiles $ExpTimes.ToArray()) }
    [PSCustomObject]@{ Endpoint = "POST /internal/v1/character/potential (Stat Upgrade)"; Stats = (Get-Percentiles $PotTimes.ToArray()) }
)

foreach ($r in $Results) {
    Write-Host ("{0,-50} | N={1} | Avg={2} ms | p50={3} ms | p95={4} ms | p99={5} ms" -f $r.Endpoint, $r.Stats.Count, $r.Stats.AvgMs, $r.Stats.P50Ms, $r.Stats.P95Ms, $r.Stats.P99Ms)
}
