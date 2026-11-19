# Step 2 Roadmap & Acceptance Gatekeeper Specification (`InfiniteUniverse` — UE `5.8`)

**Base Tag:** `Step1_Stable` (`08ed581` — `62/62 PASS`, `23/23` Automation Tests, `215.3 FPS @ 1080p`)  
**Target Tag:** `Step2_Stable`  
**Locked Decisions:**
1. **Dedicated Server Mode:** Headless `InfiniteUniverse.exe -server -log -port=7777` (`NM_DedicatedServer`) on UE `5.8` Installed Build + `Source/InfiniteUniverseServer.Target.cs` ready for Linux/Cloud CI source builds.
2. **Co-op PvE & Sparring Arena PvP:** Friendly-fire disabled in open greybox sectors for Co-op AI combat; Server-Authoritative PvP Sparring enabled inside the West Martial Combat Arena (`36m × 36m`) or via `AIU_GameState::bAllowGlobalPvPSparring` to test live 2-Player **Wukong Mirage Perfect Evade** & afterimage replication.

---

## 1. Non-Negotiable Regression Rule ("Extend, Never Rewrite")
1. No working physics formula, flight curve, camera offset, or combat timing from `Step1_Stable` may be arbitrarily rewritten during Step 2.
2. Every Step 2 milestone must continue passing all `23/23` standalone regression tests (`InfiniteUniverse.Milestone1..Milestone7`) in addition to new multi-instance network tests.
3. Deferred out-of-scope systems (Backend/DB, Login, Inventory, Equipment, Loot, Quest, Economy, Guild, Multi-Planet, Full Transformations) remain strictly locked until Step 3+.

---

## 2. Step 2 Milestones (`S2-M0` → `S2-M5`)
- **`S2-M0` — Dedicated Server Bootstrap & Multi-Client Harness:** `AIU_GameState`, `AIU_PlayerState`, `InfiniteUniverseServer.Target.cs`, `F3` Net Telemetry (`NetMode/Role/Ping/Loss/Zone`), and `Tools/LaunchMultiplayerTest.ps1` (`1 Headless Server -server :7777 + 2 Windowed Clients`).
- **`S2-M1` — Networked Ground & Free 3D Flight Prediction:** `FSavedMove_IUCharacter` + `FNetworkPredictionData_Client_IUCharacter` for zero-latency client 6-DOF flight prediction, server speed/collision validation, and smooth `SimulatedProxy` flight lean & animation sync.
- **`S2-M2` — Server-Authoritative Attributes, Co-op/Arena PvP Rules & Respawn:** `DOREPLIFETIME` + `OnRep_CurrentHP/Ki/IsDead` on `UIU_AttributeComponent`, server-only `ApplyDamageSpec` with Co-op vs Martial Arena PvP rule check, and synchronized respawn.
- **`S2-M3` — Networked Action Combat & PvP Mirage Perfect Evade:** `Server_*` RPC intent validation, replicated `AIU_KiProjectile`, server-validated Perfect Evade (`<= 0.30s` / `< 20 cm` with bounded ping compensation <= 120ms), and `NetMulticast` `UPoseableMeshComponent` afterimage ghosts across all clients.
- **`S2-M4` — Server-Authoritative Enemy AI & Multi-Player Aggro:** Server-only `AIU_EnemyAIController` & `AIU_EnemySpawner`, multi-player target selection/switching, and replicated windup/state cues.
- **`S2-M5` — WAN Lag/Loss Simulation (`80ms / 2% Loss`), 8-Group Gatekeeper Audit (`64 pts`) & `Step2_Stable` Freeze:** Full multi-client endurance, bandwidth & FPS report, side-by-side 2-Client Demo Video (Co-op AI + Arena 1v1 PvP), and annotated `Step2_Stable` Git tag.
