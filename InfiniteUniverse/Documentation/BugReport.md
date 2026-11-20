# Bug Report & Defect Resolution Log (`InfiniteUniverse` — Step 1, Step 2 & Step 3)

**Baseline Engine & Stack:** Unreal Engine `5.8.4` (`Win64 Development` + `NM_DedicatedServer`) + Go `1.27.3` + PostgreSQL `18`  
**Last Verified:** `2026-11-20`

---

## 1. Summary by Severity

| Severity | Discovered | Resolved & Regression-Tested | Open / Blocking |
| :--- | :---: | :---: | :---: |
| **Critical / Gameplay, Net or Persistence-Blocking (`S1`)** | 10 | 10 | **0** |
| **Major (`S2`)** | 8 | 8 | **0** |
| **Minor / UI / Tooling (`S3`)** | 4 | 4 | **0** |

---

## 2. Step 1 Resolved Defects Ledger (`BUG-01` → `BUG-11`)

| Bug ID | Severity | Subsystem | Root Cause | Resolution Implemented | Regression Test Proof |
| :---: | :---: | :--- | :--- | :--- | :--- |
| `BUG-01` | `S1` | `IU_PlayerCharacter` | At `0 HP`, movement/flight/sprint inputs remained active and player lacked auto-respawn recovery. | Added `IsDead()` guards across all inputs, auto-exited `Flight` & `Lock-On`, and added `ResetPlayerAtSpawn()` (`3.0s` timer). | `AuditedBugFixesDeadLockoutAndAutoRespawn` (`PASS`) |
| `BUG-02` | `S2` | `IU_PlayerCharacter` / `IU_DamageComponent` | Pressing `Space` or `F` during active `HitReaction` bypassed hit-stun into Jump / Flight. | Added `DamageComponent->IsInHitReaction()` guard on `StartJumpInput()` & `ToggleFlightInput()`. | `AuditedBugFixesDeadLockoutAndAutoRespawn` (`PASS`) |
| `BUG-03` | `S1` | `IU_PlayerController` (`UE 5.8`) | UE `5.8` `IsLocalController()` checks `Cast<ULocalPlayer>(Player) != nullptr` when `NetDriver == nullptr`. | Exposed `MarkAsLocalTestController()` calling `SetAsLocalPlayerController()` for headless test worlds. | `SixDOFHoverAndDash` & `SavedMoveFlightFlagsAndAntiSpeedhack` (`PASS`) |
| `BUG-04` | `S2` | `IU_PlayerCharacter` | `bEnableCameraRotationLag = true` introduced angular lag between `ControlRotation` crosshair ray and `FollowCamera`. | Kept `bEnableCameraLag = true` with `bEnableCameraRotationLag = false` (1:1 zero-latency aim). | `GenshinElevatedCameraAndTrueAimRay` (`PASS`) |
| `BUG-05` | `S2` | `IU_CombatComponent` / `IU_SkeletalAnimDirectorComponent` | `RequestKiBlast()` lacked animation broadcast and `AIU_EnemyCharacter` kept placeholder cylinder visible over `SK_Enemy`. | Added `OnKiBlastFired` delegate and `UpdatePlaceholderVisibility()`. | `All15AnimeAnimationsAndPoseableAfterimage` (`PASS`) |
| `BUG-06` | `S1` | `IU_Milestone4EnemyAITests` | Transient `UWorld` without `SetBegunPlay(true)` skipped `BeginPlay()` on dynamically spawned actors. | Bound delegates in `PostInitializeComponents()` and unified world stepping via `World->Tick()`. | `Milestone4.*` (`4/4 PASS`) |
| `BUG-07` | `S3` | `Tools/PackageStandalone.ps1` | `-archivedirectory=Saved\StagedBuilds` collided with UAT's internal staging folder. | Separated archive output to `Saved\PackagedBuilds` and used `@UATArgs` splatting. | `RunUAT BuildCookRun` (`ExitCode 0`) |
| `BUG-08` | `S3` | `setup_m6_anime_assets.py` | `USkeletalMeshSocket::SocketName` is read-only in UE `5.8` Python reflection. | Added C++ `UIU_SkeletalAnimDirectorComponent::EnsureSkeletalMeshSocket`. | `AnimeMeshesSkeletonAndSocketValidation` (`PASS`) |
| `BUG-09` | `S1` | `IU_HUDManager` | Calling `DrawHUD()` when `Canvas == nullptr` crashed inside `Super::DrawHUD()`. | Moved `if (!Canvas) return;` ahead of `Super::DrawHUD()`. | `EnduranceStabilityMemoryAndAttributeDamage` (`PASS`) |
| `BUG-10` | `S2` | Cooked Build Materials | Procedural `UMaterial` assets lacked `MATUSAGE_SKELETAL_MESH`, causing grey `WorldGridMaterial` in cooked `.exe`. | Enabled `MATUSAGE_SKELETAL_MESH = True` on all character & afterimage materials. | Live D3D12 Packaged Frame Audit (`PASS`) |
| `BUG-11` | `S2` | Cooked Build Packaging | Dynamic `LoadObject` `UAnimSequence` clips were omitted from map-only cook, causing T-pose in cooked `.exe`. | Added `+DirectoriesToAlwaysCook` in `Config/DefaultGame.ini`. | Live D3D12 Packaged Demo Video (`PASS`) |

---

## 3. Step 2 Multiplayer Resolved Defects Ledger (`BUG-12` → `BUG-15`, `SEC-01` → `SEC-02`)

| Bug ID | Severity | Subsystem | Root Cause | Resolution Implemented | Verification Proof |
| :---: | :---: | :--- | :--- | :--- | :--- |
| `BUG-12` | `S1` | `IU_MovementComponent` (Anti-Speedhack) | Server flight speed clamp initially ran *after* `SafeMoveUpdatedComponent`, allowing 1 frame of spoofed displacement when a cheated `99,999 cm/s` velocity was injected. | Moved Server-Authoritative 3D flight speed envelope clamp *before* `Adjusted = Velocity * DeltaTime` in `PhysCustom_FreeFlight`. | `SavedMoveFlightFlagsAndAntiSpeedhack` (`PASS`) |
| `BUG-13` | `S1` | `Tools/LaunchMultiplayerTest.ps1` (UE `5.8` Installed Build) | Cooked `TargetType.Game` binary (`InfiniteUniverse.exe`) ignores `-server` (`#if WITH_EDITOR || UE_SERVER` in `LaunchEngineLoop`), booting in `NM_Standalone` (`NetMode=0`) instead of binding UDP `:7777`. | Configured `LaunchMultiplayerTest.ps1` to boot `UnrealEditor.exe ... -server -NullRHI -port=7777` (`NetMode=1 / NM_DedicatedServer`) and poll `IpNetDriver listening on port 7777` before connecting cooked `InfiniteUniverse.exe` D3D12 Clients (`NetMode=3`). | `Step2_DedicatedServer.log` (`Join succeeded x2`, `OnlinePlayers=2`) |
| `BUG-14` | `S2` | `IU_SkeletalAnimDirectorComponent` & `IU_AttributeComponent` | Under UDP packet reordering, if `Multicast_OnEnemyReset` / `Multicast_OnPlayerRespawned` arrived 1 packet ahead of `OnRep_IsDead (false)`, `AnimDirector` re-latched `DeathAnim` and left respawned actors visually prone on Clients. | Added `bWasDeadLastFrame` transition recovery in `UIU_SkeletalAnimDirectorComponent::TickComponent` and capsule collision restore inside `UIU_AttributeComponent::OnRep_IsDead`. | Live 2-Client Video Frame Inspection (`PASS`) |
| `BUG-15` | `S3` | `IU_HUDManager` (`960×540` Split-Window Layout) | At `960×540` side-by-side window resolution, the centered top stage banner overlapped the top-right `F3` Debug Telemetry panel, and `StrikeFistMesh` (`1.04m` scale) over-filled close-up camera views. | Made banner/toast coordinates responsive for `< 1200px` viewports and scaled `StrikeFistMesh` visual orb to `0.36` while keeping physics `AttackHitboxRadius = 52 cm`. | Live 2-Client Video Frame Inspection (`PASS`) |
| `SEC-01` | `S1` | `IU_PlayerController` (Security Audit) | Unprivileged remote `NM_Client` callers could invoke `Server_ToggleGlobalPvPSparring` (`F4`) on `NM_DedicatedServer` and enable global PvP griefing in Co-op PvE sectors. | Restricted `Server_ToggleGlobalPvPSparring_Implementation` on `NM_DedicatedServer` unless `-AllowClientPvPToggle` is explicitly set on Server CLI; logs `[Security]` warning. | Step 2 Security Audit (`PASS`) |
| `SEC-02` | `S2` | `IU_DamageComponent` & `IU_CombatComponent` (Security Audit) | Malformed `NaN` / `Infinity` floats/vectors or out-of-range enum payloads sent to `Server_*` RPCs / `ApplyDamageSpec` could corrupt transforms or HP state. | Added `FMath::IsFinite`, `!ContainsNaN()`, and enum-range guards across all `Server_*` RPCs and `ApplyDamageSpec`, plus explicit `ExitingPawn->Destroy()` verification in `AIU_GameMode::Logout`. | `InvalidDamageAndClientExploitRejection` (`PASS`) |

---

## 4. Step 3 Backend Persistence & Progression Resolved Defects Ledger (`BUG-16` → `BUG-19`)

| Bug ID | Severity | Subsystem | Root Cause | Resolution Implemented | Verification Proof |
| :---: | :---: | :--- | :--- | :--- | :--- |
| `BUG-16` | `S1` | `backend/internal/modules/character` (`GrantEXPFenced`) | Two concurrent goroutines submitting the identical `idempotency_key` in the same microsecond could both pass a simple map check before the first PostgreSQL transaction committed, granting duplicate EXP. | Implemented per-key in-flight channel serialization (`inFlightKill map[string]chan struct{}`) so concurrent duplicate `idempotency_key` submissions block on the first in-flight commit and return the cached idempotent result with zero duplicate DB mutations. | `TestStep3PersistenceAcrossRestartAndConcurrent50Goroutines` (`go test -race PASS`) |
| `BUG-17` | `S1` | `Core/IU_GameMode` (`PreLogin`) | Calling `Super::PreLogin` on a manually spawned test `AIU_GameMode` (where `World->GetAuthGameMode()` was not set) triggered `check(GameMode)` inside `AGameSession::ApproveLogin`. | Executed the zero-allocation `?Ticket=` security gate *ahead* of `Super::PreLogin` and guarded `Super::PreLogin` with `GameSession && GetWorld()->GetAuthGameMode() == this`. | `PreLoginTicketGateAndAuthWidget` (`34/34 UE5 Tests PASS`) |
| `BUG-18` | `S2` | `Core/IU_AutoDemoSubsystem` (Client Logout Sequencing) | Closing `ND->ServerConnection` `0.45s` ahead of `RequestExit` caused UE `5.8` `HandleDisconnect` to tear down the `NM_Client` world and browse to `/Game/Maps/...?closed` before `RequestExit` fired. | Moved `ServerConnection->Close()` + `FlushNet(true)` into the exact `TargetDurationSeconds` exit block immediately preceding `WritePerformanceReportCSV()` and `RequestExit(false)`. | `LaunchStep3FullStackTest.ps1` (`PASS`) |
| `BUG-19` | `S3` | `UI/IU_HUDManager` (`960×540` HUD + EXP Bar) | Adding the new `LV. X | EXP` bar increased `VitalsBox` height, causing `BannerTopY = 148` on `960×540` split windows to touch `[GROUND | EARTH DASH]` and reach the `F3` panel edge; unfocused windows also throttled FPS. | Shifted `< 1200px` `BannerTopY` to `194px` (`BannerW <= 480px`) and disabled `t.IdleWhenNotForeground` / `t.UnfocusedFrameRateLimit` during automated multi-window runs. | Step 3 Live D3D12 Frame Inspection (`PASS`) |
| `BUG-20` | `S1` | `Core/IU_GameMode` & `AIU_PlayerCharacter` (`Logout` Position Save) | During UDP `UNetConnection::Close()`, `APlayerController::Destroyed()` unpossesses `Pawn` *before* invoking `AGameModeBase::Logout(Exiting)`, so `Exiting->GetPawn()` was already `nullptr` and `ReleaseCharacterLease` omitted `final_save` (`LOGOUT` coordinates). | Cached live `(Position, Yaw, HP, Ki, bIsAlive)` onto `AIU_PlayerState::CachePawnWorldSnapshot()` inside `AIU_PlayerCharacter::UnPossessed()` / `EndPlay()`, and made `BuildCharacterSavePayload` read that cached snapshot whenever `Controller->GetPawn()` is `nullptr`. | SQL `character_save_snapshots` (`LOGOUT` row verified) & Phase 2 West Overlook Restoration (`PASS`) |

---

## 5. Open Defects & Deferred Scope Notes

- **Critical (`S1`) / Major (`S2`):** **None (`0`)**.
- **Deferred Architecture Notes (`Step 4+` by Design):**
  1. `Inventory`, `Equipment`, `CSPRNG Personal/Boss Loot`, `Currency Ledger / Enhance`, `Party / Guild`, and `Multi-Zone Transfer` tables exist in `000001_init_core_schema.up.sql` and are intentionally locked for Step 4+ milestones.
  2. When running without Docker/Redis (`IU_ALLOW_OPTIONAL_DEPS=true`), `world.Coordinator` enforces single-instance in-memory mutex lease tables backed by hard PostgreSQL `WHERE fencing_epoch = $2` SQL fencing; horizontal multi-gateway deployments enable Redis 7 Lua lease scripts (`IU_REDIS_ADDR=127.0.0.1:6379`) with zero code changes.
