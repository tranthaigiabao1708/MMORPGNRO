# Step 1 Bug Report & Defect Resolution Log (`InfiniteUniverse`)

**Baseline Engine:** Unreal Engine `5.8.4` (`Win64 Development`)  
**Last Verified:** `2026-11-19`

---

## 1. Summary by Severity

| Severity | Discovered | Resolved & Regression-Tested | Open / Blocking |
| :--- | :---: | :---: | :---: |
| **Critical / Gameplay-Blocking (`S1`)** | 4 | 4 | **0** |
| **Major (`S2`)** | 5 | 5 | **0** |
| **Minor / Tooling (`S3`)** | 2 | 2 | **0** |

---

## 2. Resolved Defects Ledger

| Bug ID | Severity | Subsystem | Root Cause | Resolution Implemented | Regression Test Proof |
| :---: | :---: | :--- | :--- | :--- | :--- |
| `BUG-01` | `S1` | `IU_PlayerCharacter` | At `0 HP`, `ApplyMovementInput`, `StartJumpInput`, `ToggleFlightInput`, and `StartSprintInput` remained active and player had no auto-respawn recovery (soft-lock). | Added `AttributeComponent->IsDead()` guards across all movement/flight/sprint inputs, auto-exited `Flight` & `Lock-On` on defeat, and added `ResetPlayerAtSpawn()` (`3.0s` timer). | `AuditedBugFixesDeadLockoutAndAutoRespawn` (`PASS`) |
| `BUG-02` | `S2` | `IU_PlayerCharacter` / `IU_DamageComponent` | Pressing `Space` or `F` during active `HitReaction` bypassed hit-stun into Jump / Flight. | Added `DamageComponent->IsInHitReaction()` guard on `StartJumpInput()` & `ToggleFlightInput()` + `ResetDamageState()`. | `AuditedBugFixesDeadLockoutAndAutoRespawn` (`PASS`) |
| `BUG-03` | `S1` | `IU_PlayerController` (UE `5.8` Migration) | UE `5.8` `APlayerController::IsLocalController()` checks `Cast<ULocalPlayer>(Player) != nullptr` when `NetDriver == nullptr`, breaking synthetic headless controller checks. | Exposed `MarkAsLocalTestController()` calling protected `SetAsLocalPlayerController()` for headless test worlds. | `SixDOFHoverAndDash` & `FlightTransitionsNoTeleport` (`PASS`) |
| `BUG-04` | `S2` | `IU_PlayerCharacter` (Camera Lag vs Crosshair Aim) | Enabling `bEnableCameraRotationLag = true` introduced angular lag between `ControlRotation` crosshair ray and `FollowCamera` transform during fast mouse pans and initial `-8.5°` pitch setup. | Kept `bEnableCameraLag = true` (smooth positional follow) with `bEnableCameraRotationLag = false` (1:1 zero-latency aim alignment). | `GenshinElevatedCameraAndTrueAimRay` (`PASS`) |
| `BUG-05` | `S2` | `IU_CombatComponent` / `IU_SkeletalAnimDirectorComponent` | `RequestKiBlast()` spawned `AIU_KiProjectile` without broadcasting an event to trigger `A_Player_KiBlast`, and `AIU_EnemyCharacter` kept cylinder placeholder visible after `SK_Enemy` assignment. | Added `OnKiBlastFired` delegate + `HandleKiBlastFired()`, and added `AIU_EnemyCharacter::UpdatePlaceholderVisibility()`. | `All15AnimeAnimationsAndPoseableAfterimage` (`PASS`) |
| `BUG-06` | `S1` | `IU_Milestone4EnemyAITests` | Transient `UWorld` without `SetBegunPlay(true)` skipped `BeginPlay()` on dynamically spawned actors, and manually ticking both `TimerManager` and `World->Tick()` double-advanced timers. | Bound component delegates in `PostInitializeComponents()`, set `World->SetBegunPlay(true)`, and unified world stepping via `World->Tick(LEVELTICK_All, Dt)`. | `Milestone4.*` (`4/4 PASS`) |
| `BUG-07` | `S3` | `Tools/PackageStandalone.ps1` | Passing `-archivedirectory=Saved\StagedBuilds` collided with UAT's internal `Saved\StagedBuilds` staging directory during copy. | Separated archive output to `Saved\PackagedBuilds` and passed UAT CLI flags via explicit PowerShell array splatting `@UATArgs`. | `RunUAT BuildCookRun` (`ExitCode 0`) |
| `BUG-08` | `S3` | `setup_m6_anime_assets.py` | `USkeletalMeshSocket::SocketName` is read-only in UE `5.8` Python reflection. | Added `UIU_SkeletalAnimDirectorComponent::EnsureSkeletalMeshSocket` (`#if WITH_EDITOR` authoring + runtime-safe `FindSocket`). | `AnimeMeshesSkeletonAndSocketValidation` (`PASS`) |
| `BUG-09` | `S1` | `IU_HUDManager` | Calling `DrawHUD()` in headless/NullRHI contexts where `Canvas == nullptr` crashed inside `Super::DrawHUD()` (`AHUD::DrawHUD`) before reaching the `if (!Canvas)` check. | Moved `if (!Canvas) return;` guard ahead of `Super::DrawHUD()` and triggered initial delegate bindings in `BeginPlay()`. | `EnduranceStabilityMemoryAndAttributeDamage` (`PASS`) |
| `BUG-10` | `S2` | Cooked Build Materials (`setup_m6_anime_assets.py`) | Visually inspecting live D3D12 frames from `PackagedBuilds/Windows/InfiniteUniverse.exe` revealed `SK_Player` & `SK_Enemy` falling back to grey `WorldGridMaterial` in cooked builds because `MATUSAGE_SKELETAL_MESH` was not enabled on procedurally generated `UMaterial` assets. | Added `MaterialEditingLibrary.set_base_material_usage(mat, MATUSAGE_SKELETAL_MESH, True)` across all character & afterimage materials and re-cooked. | Live D3D12 Packaged Build Frame Inspection (`PASS`) |
| `BUG-11` | `S2` | Cooked Build Packaging (`Config/DefaultGame.ini`) | Visually inspecting live D3D12 cooked frames revealed T-pose in the packaged `.exe` because the 15 `A_Player_*` `UAnimSequence` clips loaded dynamically via `LoadObject` in `UIU_SkeletalAnimDirectorComponent` were omitted when cooking map-only dependencies. | Added `+DirectoriesToAlwaysCook` (`/Game/Characters`, `/Game/Combat`, `/Game/AI`, `/Game/Input`, `/Game/Core`, `/Game/Developer`) in `Config/DefaultGame.ini`. | Live D3D12 Packaged Build Demo Video (`PASS`) |

---

## 3. Open Defects

- **Critical (`S1`) / Major (`S2`):** **None (`0`)**.
- **Minor Cosmetic Notes (`S4` — Non-Blocking for Step 1):**
  1. Procedural prototype anime animations (`15` baked `UAnimSequence` clips) use direct clip switching via `UIU_SkeletalAnimDirectorComponent` rather than full inertial blend-spaces + IK foot placement (scheduled for visual animation polish alongside GAS/Montage replication in Step 2+).
  2. `AIU_KiProjectile` uses an emissive mesh + `UPointLightComponent` fallback when `ImpactVFX` (`UNiagaraSystem`) is left unassigned in Blueprint.
