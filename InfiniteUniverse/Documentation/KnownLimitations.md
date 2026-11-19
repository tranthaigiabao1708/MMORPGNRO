# Step 1 Known Limitations & Locked Scope Boundaries (`InfiniteUniverse`)

Per the Step 1 Gatekeeper Policy, the following capabilities are **intentionally excluded or simplified** in `Step1_Stable` so that core C++ architecture, 3D flight, combat loop, and single-player stability could be verified cleanly first:

---

## 1. Intentionally Excluded Systems (Scheduled for Step 2+)
1. **Multiplayer & Dedicated Server (`Step 2`):** All Step 1 gameplay runs in `NM_Standalone`. Replication (`GetLifetimeReplicatedProps`, RPCs, server-authoritative hit validation, and `FSavedMove_Character` prediction for `CMOVE_FreeFlight`) begins in **Step 2**.
2. **Backend, Account/Auth & Persistent Database:** No login, character save/load, PostgreSQL/Redis, or Go backend calls exist in `InfiniteUniverse/`. Attributes reset cleanly per session/respawn.
3. **Gameplay Ability System (`GAS`):** Step 1 uses lightweight, deterministic C++ components (`UIU_AttributeComponent`, `UIU_CombatComponent`, `UIU_DamageComponent`) to validate game-feel first before any future ability-tree expansion.
4. **RPG Meta Systems:** Inventory, Equipment, Loot/Drop tables, Quests/Missions, Economy/Trading, Guild/Party, Multi-Planet travel, Character Progression, World Bosses, and Full Transformation Trees are strictly excluded from Step 1.

---

## 2. Current Technical & Content Limitations in `Step1_Stable`
1. **Endurance Soak Duration Honesty:**
   - Automated headless endurance testing (`EnduranceStabilityMemoryAndAttributeDamage`) executes `3,600+` simulated frames (`60+ seconds` multi-cycle combat/respawn) verifying `0` leaked projectiles/afterimages, and live D3D12 hardware profiling (`-AutoDemoShowcase`) executes real rendered loops on `Lvl_Greybox_Prototype`.
   - A full **30-minute (`1800s`) wall-clock unattended soak** has not been blocked on synchronously during interactive development, but is directly supported out-of-the-box via:
     ```powershell
     & "c:\MMORPGNRO\InfiniteUniverse\Saved\PackagedBuilds\Windows\InfiniteUniverse.exe" -AutoDemoShowcase -DemoDuration=1800 -WinX=1280 -WinY=720 -Windowed
     ```
     which writes second-by-second FPS & Physical RAM telemetry to `Saved/Logs/Step1_LiveD3D12Profile.csv`.
2. **Enemy AI Scope:**
   - `AIU_EnemyAIController` implements a deterministic single-target state machine (`Idle → Detect → Chase → Attack → Recover → HitReaction → Dead`) with optional 3D aerial chase (`bCanFlyChase`), without complex group flanking, ranged AI projectile volleys, or EQS cover queries.
3. **Animation Blending:**
   - `UIU_SkeletalAnimDirectorComponent` switches directly between the 15 `UAnimSequence` clips (`ABP_PlayerAnime` `UAnimBlueprint` base class `UIU_CharacterAnimInstance` is also provided for future state-machine crossfade graphs).
4. **Audio:**
   - Step 1 focuses strictly on visual/HUD/camera/animation feedback; SFX and BGM assets (`MetaSounds`) are not yet authored.
