# Step 3 Gatekeeper Acceptance Audit & Backend Performance Report (`Step3_Stable`)

**Project:** `InfiniteUniverse` (`c:\MMORPGNRO\InfiniteUniverse` + `c:\MMORPGNRO\backend`)  
**Engine & Stack:** Unreal Engine `5.8.4` (`Win64 Development` + `NM_DedicatedServer`) + Go `1.27.3` + PostgreSQL `18.1`  
**Previous Frozen Baselines (Untouched):** `Step1_Stable` (`08ed581`), `Step2_Stable` (`d96a8be`)  
**Audit Date:** `2026-11-20`  
**Final Recommendation:** **`PASS` (`70 / 70` Points across 8 Gatekeeper Groups — `0` Open `S1/S2` Defects)**

---

## 1. Executive Summary & Acceptance Scorecard (`8 Groups / 70 Points`)

| Group | Audit Domain | Max Score | Earned | Status | Primary Executed Proof |
| :---: | :--- | :---: | :---: | :---: | :--- |
| **3.1** | **Database Stack, SQL Migrations & Go Modular Architecture** | `8` | **`8`** | **PASS** | `TestCoreSchemaMigrationRoundTrip` (`up -> down -> up`, 19/19 tables, 12 triggers, 18 indexes) + `34/34` UE5 Tests (`0` regression on 29 Step 1–2 tests). |
| **3.2** | **Authentication & Session Security (`Argon2id` + `Ed25519 JWT`)** | `8` | **`8`** | **PASS** | `TestStep3AuthSecurityAndRateLimit` (`-race`): Argon2id PHC (`m=64MB, t=2, p=2`), Ed25519 JWT tamper `401`, Refresh rotation, Single-Active-Session `ERR_SESSION_SUPERSEDED`, SQLi immunity, Per-IP `429` Rate Limit. |
| **3.3** | **Character Management & One-Time World Connect Tickets** | `8` | **`8`** | **PASS** | `TestStep3CharactersAndOneTimeWorldTickets` + Live Server #3: `CITEXT` uniqueness (`409`), 4-slot cap (`422`), cross-account `403`, Ticket replay `401`, and `PreLogin` `ERR_MISSING_WORLD_TICKET` rejection. |
| **3.4** | **Anti-Dupe & Duplicate-Login Eviction (`Lease Fencing`)** | `12` | **`12`** | **PASS** | `TestStep3SplitBrainLeaseFencing` (`-race`) + Live Phase 3 Duplicate Login: stale Server A writes rejected `409 Conflict` (`WHERE fencing_epoch = $2`), and Server #3 evicted superseded Session #1 (`Epoch #3 -> #4`) *without* saving. |
| **3.5** | **Persistence Across Complete Dedicated Server Restart** | `10` | **`10`** | **PASS** | `LaunchStep3FullStackTest.ps1`: Killed Dedicated Server #1 (`PID 20148`) -> Booted fresh Server #2 (`PID 27180`) -> Restored `ZenithA_141714` at exact `Pos=(-402.8, -171.8, 112.2)`, `Lv.2 (10/225)`, `HP=300/300`, `Ki=300/300`, `Atk=110`, `Potential=50` with identical `SHA-256` (`3ef6479c98fb4405`). |
| **3.6** | **NRO Dual-Track Level (`LV EXP`) & Potential Stat Allocation** | `8` | **`8`** | **PASS** | Base stats `200 HP / 200 Ki / 100 Sức đánh`; Raider defeats grant `+55 LV EXP` (determines `Level` **ONLY** — `0` auto stats!) + `+350 Potential Points`; manual upgrades (`P` panel / `1,2,3` RPC) apply `+100 HP`, `+100 Ki`, `+10 Sức đánh` with independent escalating costs (`200->300->400...`, `250->400->550...`). |
| **3.7** | **Concurrency (`50 Goroutines -race`) & Async Bridge Fault Isolation** | `8` | **`8`** | **PASS** | `TestStep3PersistenceAcrossRestartAndConcurrent50Goroutines` (`CGO_ENABLED=1 go test -race`, `0` races, `inFlightKill` idempotency + `SELECT ... FOR UPDATE` Potential upgrades); UE5 `FHttpModule` async callbacks kept Server tick at `34.1 Hz`. |
| **3.8** | **End-to-End Performance & Complete Deliverable Package** | `8` | **`8`** | **PASS** | Bridge `p50 = 0.33–1.83 ms`, **`p95 = 0.87–2.87 ms`** (`< 10 ms` budget); Gameplay Client `124.1 Avg FPS` (`64.4` 1% low); Server `34.1 Hz`; Video `Step3_Persistence_Demo.mp4` (`18.07 MB`). |
| **TOTAL** | **All Step 3 Acceptance Criteria** | **`70`** | **`70`** | **`PASS`** | **Ready to freeze `Step3_Stable` & advance to Step 4** |

---

## 2. Test Case Execution Matrix

### 2.1 Go Backend `-race` Automated Suite (`CGO_ENABLED=1 go test -race ./cmd/... ./internal/... ./pkg/...`)

| Test ID | Package & Function | Result | Duration | Verified Invariant |
| :--- | :--- | :---: | :---: | :--- |
| `GO-S3-01` | `internal/platform/postgres::TestCoreSchemaMigrationRoundTrip` | **PASS** | `0.58s` | `000001_init_core_schema` `up -> down -> up` creates all 19 tables, 2 functions, 12 triggers, 18 indexes cleanly. |
| `GO-S3-02` | `internal/platform/postgres::TestCharactersFencingEpochDefaultAndCheck` | **PASS** | `0.72s` | `characters.fencing_epoch` defaults to `0`, rejects `< 0` (`SQLSTATE 23514`), enforces `>= 1` on `currency_ledger`. |
| `GO-S3-03` | `internal/httpapi::TestStep3AuthSecurityAndRateLimit` | **PASS** | `0.97s` | Argon2id PHC verification, CITEXT duplicate `409`, SQLi `401`, tampered Ed25519 JWT `401`, refresh rotation, session superseding, and `429` rate limit. |
| `GO-S3-04` | `internal/httpapi::TestStep3CharactersAndOneTimeWorldTickets` | **PASS** | `0.52s` | 4-character slot limit (`422`), CITEXT name check (`409`), cross-account `EnterWorld` (`403`), bad HMAC (`403`), one-time ticket replay (`401`). |
| `GO-S3-05` | `internal/httpapi::TestStep3SplitBrainLeaseFencing` | **PASS** | `0.87s` | Server A (`epoch=1`) superseded by Server B (`epoch=2`): `RenewLeases` reports `revoked_character_ids`, late Server A `Save` & `GrantEXP` rejected `409 Conflict`. |
| `GO-S3-06` | `internal/httpapi::TestStep3PersistenceAcrossRestartAndConcurrent50Goroutines` | **PASS** | `0.76s` | `50` concurrent goroutines under `-race` verify `Lv.3 (225 EXP)` leaves base `200 HP / 200 Ki / 100 Atk` untouched, verifies escalating Potential costs (`hp: 200,300`, `ki: 200`, `atk: 250,400,550,700,850`), `422 ERR_INSUFFICIENT_POTENTIAL`, and post-restart restoration (`400 HP, 300 Ki, 150 Atk`). |
| `GO-BASE` | `cmd/datacompiler`, `cmd/gameapi`, `cmd/coordinator`, `config`, `rediskeys`, `gamedata` | **PASS** | `9.5s` | Zero regressions across all existing Go data compiler, configuration, and Redis key-space tests. |

### 2.2 Unreal Engine `5.8.4` Headless + Loopback Automation Suite (`34 / 34 PASS`)

| Test ID | Automation Path | Result | Verified Invariant |
| :--- | :--- | :---: | :--- |
| `UE-S1-01..23` | `InfiniteUniverse.Milestone1..Milestone7.*` (`23` tests) | **PASS** | `100%` Step 1 locomotion, 2-stage momentum flight, combo/Ki/Mirage evade, AI, camera, and memory stability preserved. |
| `UE-S2-01..06` | `InfiniteUniverse.Step2.*` & `Step2Audit.*` (`6` tests) | **PASS** | `100%` Step 2 multi-spawn, Co-op/PvP rules, `FSavedMove` anti-speedhack, simultaneous 4-player single death, and `NaN` RPC guards preserved. |
| `UE-S3-01` | `InfiniteUniverse.Step3.CryptoHMACSHA256Vectors` | **PASS** | Verifies `UIU_BackendSubsystem::ComputeSHA256` & `ComputeHMACSHA256Hex` against NIST FIPS 180-4 and RFC 4231 Case 2 vectors. |
| `UE-S3-02` | `InfiniteUniverse.Step3.BackendJsonSerializationRoundTrip` | **PASS** | Verifies `ParseCharacterSummaryFromJson` & `SerializeSavePayloadToJson` across coordinates, `fencing_epoch`, `potential_points`, `attack_power`, and escalating next-upgrade costs. |
| `UE-S3-03` | `InfiniteUniverse.Step3.ProgressionPlayerStateAndVitals` | **PASS** | Verifies NRO rules: Level-Up via `ApplyAuthoritativeEXPUpdate` keeps `MaxHP=200` & `AttackPower=100` unchanged; `TryOfflineAllocatePotential` escalates `NextHPCost 200->300->400` independently of `NextKiCost=200` and rejects overspend. |
| `UE-S3-04` | `InfiniteUniverse.Step3.PreLoginTicketGateAndAuthWidget` | **PASS** | Verifies `AIU_GameMode::PreLogin` rejects connections missing `?Ticket=` (`ERR_MISSING_WORLD_TICKET`) and constructs both `UIU_LoginCharacterWidget` & `UIU_PotentialUpgradeWidget`. |
| `UE-S3-05` | `InfiniteUniverse.Step3.BackendSubsystemLoopbackRoundTrip` | **PASS** | Executes live UE `5.8` C++ -> Go `:8080` -> PostgreSQL 18 `:5433` round-trip (`Register -> CreateCharacter (200 HP/Ki, 100 Atk) -> ClaimLease -> GrantEXP (Lv.2 + 700 TN, stats still 200/100) -> AllocatePotential ('hp'->300 HP, 'atk'->110 Atk) -> ReleaseLease`). |

---

## 3. Live Multi-Process E2E Persistence & Security Evidence (`LaunchStep3FullStackTest.ps1`)

### 3.1 PostgreSQL `characters` + `character_attributes` + `character_potential` + `character_save_snapshots` (`ZenithA_141714`)
```text
 character_name | level | current_exp | potential_exp | max_hp | max_energy | phys_atk | hp_upgrades | ki_upgrades | atk_upgrades 
----------------+-------+-------------+---------------+--------+------------+----------+-------------+-------------+--------------
 ZenithA_141714 |     2 |          10 |            50 |    300 |        300 |      110 |           1 |           1 |            1
 ZenithB_141714 |     2 |          10 |            50 |    300 |        300 |      110 |           1 |           1 |            1

 snapshot_reason | fencing_epoch |   px   |   py   |    sha256_16     |  server_id   
-----------------+---------------+--------+--------+------------------+--------------
 PERIODIC_60S    |             1 |    0.0 |    0.0 | 814483729b6752b4 | ue5-ded-srv1
 LEVEL_UP        |             1 |    0.0 |    0.0 | 619d1d40f42ae76d | ue5-ded-srv1
 LOGOUT          |             1 | -402.8 | -171.8 | 3ef6479c98fb4405 | ue5-ded-srv1
 LOGOUT          |             2 | -402.8 | -171.8 | 3ef6479c98fb4405 | ue5-ded-srv2
```

### 3.2 Dedicated Server Dual-Track Progression, Potential Allocation & Security Logs
```text
[Progression] Character 'ZenithA_141714' gained +55 LV EXP & +350 Potential -> Lv.1 (55/100 EXP, Potential=350, MaxHP=200, Atk=100)
[Progression] Character 'ZenithA_141714' gained +55 LV EXP & +350 Potential -> Lv.2 (10/225 EXP, Potential=700, MaxHP=200, Atk=100)
[Potential] Character 'ZenithA_141714' spent 200 TN on 'ki' -> HP 200/200, Ki 300/300, SucDanh 100 (x1.00), RemainingTN=500
[Potential] Character 'ZenithA_141714' spent 200 TN on 'hp' -> HP 300/300, Ki 300/300, SucDanh 100 (x1.00), RemainingTN=300
[Potential] Character 'ZenithA_141714' spent 250 TN on 'atk' -> HP 300/300, Ki 300/300, SucDanh 110 (x1.10), RemainingTN=50
[Security] PreLogin REJECTED connection from '127.0.0.1': missing required one-time ?Ticket= parameter.
[Persistence] Restored Character 'ZenithA_141714' (ID=8cb1f9a3-ca6a-4940-8e3e-6dbb4e1ce1e4, Epoch=#3, Lv=2, EXP=10/225, HP=300/300, Pos=(-402.8, -171.8, 112.2), HasSavedPos=1)
[LeaseFence] Lease REVOKED for Character 'ZenithA_141714' (8cb1f9a3-ca6a-4940-8e3e-6dbb4e1ce1e4) — Duplicate login / epoch superseded! Evicting stale session WITHOUT saving.
```

---

## 4. End-to-End Backend & UE5 Performance Report

### 4.1 Go Backend (`:8080`) + PostgreSQL `18` (`:5433`) Bridge Latency (`N = 100` Measured Samples)

| Internal Bridge Endpoint | Database Work Performed | Avg (ms) | `p50` (ms) | `p95` (ms) | `p99` (ms) | Gate (`p95 < 10 ms`) |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| `POST /internal/v1/lease/renew` | HMAC-SHA256 verify + Coordinator lease TTL extension | `0.52` | **`0.33`** | **`0.87`** | `13.02` | **PASS** |
| `POST /internal/v1/character/save` | HMAC-SHA256 + 3-Table ACID Tx (`characters`, `character_attributes`, `character_save_snapshots` SHA-256) | `1.97` | **`1.83`** | **`2.87`** | `9.48` | **PASS** |
| `POST /internal/v1/character/exp` | HMAC-SHA256 + `SELECT ... FOR UPDATE` ACID (`LV EXP` + `PotentialPoints`) | `1.64` | **`1.54`** | **`2.47`** | `4.46` | **PASS** |
| `POST /internal/v1/character/potential` | HMAC-SHA256 + `SELECT ... FOR UPDATE` ACID escalating Potential stat upgrade | `1.78` | **`1.69`** | **`2.69`** | `3.51` | **PASS** |

### 4.2 Live Multi-Process UE `5.8.4` Runtime Telemetry (`2` Cooked D3D12 Clients + `1` Headless Dedicated Server + Go + PG18)

| Process | Avg FPS / Tick Hz | `1% Low` FPS / Hz | Avg Frame (ms) | Peak RAM (MB) | Avg Net In / Out (KB/s) | Ping (ms) | Gate Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Cooked Client 1 (`D3D12 960×540` Gameplay)** | **`124.1 FPS`** | **`64.4 FPS`** | `8.058 ms` | `1,189.3 MB` | `3.25 / 4.56 KB/s` | `22.9 ms` | **PASS (`>= 60 FPS`)** |
| **Headless Dedicated Server (`:7777`)** | **`34.1 Hz`** | **`30.9 Hz`** | `29.356 ms` | `1,510.9 MB` | `3.28 / 5.32 KB/s` | `0.0 ms` | **PASS (`>= 30 Hz`)** |

---

## 5. Known Limitations & Locked Boundary for Step 4

1. **Intentional Step 3 Scope Boundary:** `Inventory`, `Equipment`, `CSPRNG Loot`, `Kora Currency Ledger / Enhancement`, `Party / Guild`, and `Multi-Zone Transfer` are intentionally deferred to Step 4+ per the approved roadmap (their SQL tables in `000001_init_core_schema.up.sql` are already migrated and verified).
2. **Optional Local Dependencies (`IU_ALLOW_OPTIONAL_DEPS=true`):** On developer workstations where Docker Desktop is stopped, `gateway.exe` runs seamlessly against native PostgreSQL `18` (`:5433`) using thread-safe coordinator lease tables backed by PostgreSQL `WHERE fencing_epoch = $2` hard fencing; enabling Docker `Redis 7 (:6379)` + `NATS (:4222)` activates distributed Redis Lua lease scripts automatically via `IU_REDIS_ADDR`.
