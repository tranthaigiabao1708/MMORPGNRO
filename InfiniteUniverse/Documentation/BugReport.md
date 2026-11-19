# Bug Report & Defect Resolution Log (`InfiniteUniverse` — Step 1 & Step 2)

**Baseline Engine:** Unreal Engine `5.8.4` (`Win64 Development` + `NM_DedicatedServer`)  
**Last Verified:** `2026-11-19`

---

## 1. Summary by Severity

| Severity | Discovered | Resolved & Regression-Tested | Open / Blocking |
| :--- | :---: | :---: | :---: |
| **Critical / Gameplay or Net-Blocking (`S1`)** | 7 | 7 | **0** |
| **Major (`S2`)** | 7 | 7 | **0** |
| **Minor / UI / Tooling (`S3`)** | 3 | 3 | **0** |

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

## 3. Step 2 Multiplayer Resolved Defects Ledger (`BUG-12` → `BUG-15`)

| Bug ID | Severity | Subsystem | Root Cause | Resolution Implemented | Verification Proof |
| :---: | :---: | :--- | :--- | :--- | :--- |
| `BUG-12` | `S1` | `IU_MovementComponent` (Anti-Speedhack) | Server flight speed clamp initially ran *after* `SafeMoveUpdatedComponent`, allowing 1 frame of spoofed displacement when a cheated `99,999 cm/s` velocity was injected. | Moved Server-Authoritative 3D flight speed envelope clamp *before* `Adjusted = Velocity * DeltaTime` in `PhysCustom_FreeFlight`. | `SavedMoveFlightFlagsAndAntiSpeedhack` (`PASS`) |
| `BUG-13` | `S1` | `Tools/LaunchMultiplayerTest.ps1` (UE `5.8` Installed Build) | Cooked `TargetType.Game` binary (`InfiniteUniverse.exe`) ignores `-server` (`#if WITH_EDITOR || UE_SERVER` in `LaunchEngineLoop`), booting in `NM_Standalone` (`NetMode=0`) instead of binding UDP `:7777`. | Configured `LaunchMultiplayerTest.ps1` to boot `UnrealEditor.exe ... -server -NullRHI -port=7777` (`NetMode=1 / NM_DedicatedServer`) and poll `IpNetDriver listening on port 7777` before connecting cooked `InfiniteUniverse.exe` D3D12 Clients (`NetMode=3`). | `Step2_DedicatedServer.log` (`Join succeeded x2`, `OnlinePlayers=2`) |
| `BUG-14` | `S2` | `IU_SkeletalAnimDirectorComponent` & `IU_AttributeComponent` | Under UDP packet reordering, if `Multicast_OnEnemyReset` / `Multicast_OnPlayerRespawned` arrived 1 packet ahead of `OnRep_IsDead (false)`, `AnimDirector` re-latched `DeathAnim` and left respawned actors visually prone on Clients. | Added `bWasDeadLastFrame` transition recovery in `UIU_SkeletalAnimDirectorComponent::TickComponent` and capsule collision restore inside `UIU_AttributeComponent::OnRep_IsDead`. | Live 2-Client Video Frame Inspection (`PASS`) |
| `BUG-15` | `S3` | `IU_HUDManager` (`960×540` Split-Window Layout) | At `960×540` side-by-side window resolution, the centered top stage banner overlapped the top-right `F3` Debug Telemetry panel, and `StrikeFistMesh` (`1.04m` scale) over-filled close-up camera views. | Made banner/toast coordinates responsive for `< 1200px` viewports and scaled `StrikeFistMesh` visual orb to `0.36` while keeping physics `AttackHitboxRadius = 52 cm`. | Live 2-Client Video Frame Inspection (`PASS`) |
| `SEC-01` | `S1` | `IU_PlayerController` (Security Audit) | Unprivileged remote `NM_Client` callers could invoke `Server_ToggleGlobalPvPSparring` (`F4`) on `NM_DedicatedServer` and enable global PvP griefing in Co-op PvE sectors. | Restricted `Server_ToggleGlobalPvPSparring_Implementation` on `NM_DedicatedServer` unless `-AllowClientPvPToggle` is explicitly set on Server CLI; logs `[Security]` warning. | Step 2 Security Audit (`PASS`) |
| `SEC-02` | `S2` | `IU_DamageComponent` & `IU_CombatComponent` (Security Audit) | Malformed `NaN` / `Infinity` floats/vectors or out-of-range enum payloads sent to `Server_*` RPCs / `ApplyDamageSpec` could corrupt transforms or HP state. | Added `FMath::IsFinite`, `!ContainsNaN()`, and enum-range guards across all `Server_*` RPCs and `ApplyDamageSpec`, plus explicit `ExitingPawn->Destroy()` verification in `AIU_GameMode::Logout`. | `InvalidDamageAndClientExploitRejection` (`PASS`) |

---

## 4. Open Defects

- **Critical (`S1`) / Major (`S2`):** **None (`0`)**.
- **Minor Deferred Polish Notes (`S4` — Non-Blocking for Step 2):**
  1. Direct `UAnimSequence` state playback via `UIU_SkeletalAnimDirectorComponent` will transition to full `UAnimMontage` root-motion/inertial blend-spaces when GAS ability montages are introduced in future steps.
  2. Linux/Container standalone `InfiniteUniverseServer` binary compilation requires a Source-built UE `5.8` toolchain (`Source/InfiniteUniverseServer.Target.cs` is already authored and included in `Source/`).
