# Step 2 Known Limitations & Locked Scope Boundaries (`InfiniteUniverse` — `Step2_Stable`)

Per the Step 1 & Step 2 Gatekeeper Policy, Multiplayer, Dedicated Server (`NM_DedicatedServer`), Client-Predicted 6-DOF Flight (`FSavedMove_IUCharacter`), Server-Authoritative Co-op/Arena PvP Combat, and Multi-Player Enemy AI are now **completed and verified (`64/64 PASS`)** in `Step2_Stable`, while the following Step 3+ systems remain **intentionally excluded**:

---

## 1. Intentionally Deferred Systems (Locked for Step 3+)
1. **Backend, Account/Auth & Persistent Database (`Step 3`):** No login token verification, character save/load, PostgreSQL/Redis, or Go microservice gRPC/HTTP calls are wired into `InfiniteUniverse/` yet. Session stats (`EnemyKills`, `PvPKills`, `Deaths`, `HP`, `Ki`) live cleanly in authoritative `AIU_PlayerState` / `UIU_AttributeComponent` memory per Dedicated Server match.
2. **Gameplay Ability System (`GAS`) & Transformation Trees:** Combat intentionally uses deterministic, low-bandwidth C++ replication (`~7.8 KB/s` per client) so core 3D flight + Mirage Perfect Evade network feel is rock-solid before expanding into multi-skill trees or Transformations.
3. **MMORPG Meta Systems:** Inventory, Equipment, Loot/Drop tables, Quests/Missions, Economy/Trading, Guild/Party matchmaking, World Bosses, and Multi-Planet server meshing/zoneless travel remain deferred to Step 3+.

---

## 2. Current Technical Notes in `Step2_Stable`
1. **Dedicated Server Execution on Launcher Installed Engine vs Source Engine:**
   - Epic Games Launcher Installed Builds (`Engine/Build/InstalledBuild.txt`) do not ship prebuilt `UnrealServer` binaries for `TargetType.Server`.
   - On Windows workstations using the Launcher `UE 5.8.4` installation, headless `NM_DedicatedServer` (`NetMode=1`) runs natively via `UnrealEditor.exe "InfiniteUniverse.uproject" /Game/Maps/Prototype/Lvl_Greybox_Prototype -server -NullRHI -port=7777` (`Tools/LaunchMultiplayerTest.ps1`), accepting live connections from packaged `Win64 Development` (`InfiniteUniverse.exe`) clients.
   - `Source/InfiniteUniverseServer.Target.cs` is already authored and included in `Source/` for standalone Linux/Windows Dedicated Server binary builds on a GitHub Source Engine toolchain.
2. **Endurance Soak Duration Honesty:**
   - Automated multi-process LAN and WAN Lag (`~110–130 ms RTT, ±15 ms jitter, 2% packet loss`) runs verify `0` crashes, `0` stuck states, and flat server/client memory (`~1.51 GB` Server, `~1.11 GB` Client).
   - Long unattended multi-process burn-ins (e.g., 30 minutes = `1800s`) can be run at any time via:
     ```powershell
     powershell -ExecutionPolicy Bypass -File "c:\MMORPGNRO\InfiniteUniverse\Tools\LaunchMultiplayerTest.ps1" -AutomatedDemoCapture -DurationSeconds 1800
     ```
3. **Animation & Audio Polish:**
   - `UIU_SkeletalAnimDirectorComponent` uses deterministic clip switching across the 15 anime `UAnimSequence` assets; full inertial blend-spaces, IK, and `MetaSounds` audio assets remain scheduled for content polish milestones.
