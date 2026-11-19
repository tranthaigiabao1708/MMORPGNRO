# Milestone Ledger & 26-Point Acceptance Checklist (`Step 1`)

**Rule:** No task or acceptance item may be marked `PASSED` without build + runtime test evidence.

---

## 1. Milestone 0 Task Status Ledger

| Task ID | Task Name | Status | Evidence / Blocker Note |
| :---: | :--- | :---: | :--- |
| `P0-01` | Inspect UE5, compiler, SDK & build environment | `PASSED` | Audited hardware/toolchains in `EnvironmentReport.md` + `Tools/CheckEnvironment.ps1`. |
| `P0-02` | Create C++ UE5 project (`InfiniteUniverse`) | `BLOCKED` | `.uproject`, `Target.cs`, `Build.cs`, `InfiniteUniverse.h/.cpp` scaffolded; **awaiting UE5 + MSVC v143 + Win11 SDK install** to compile via UBT. |
| `P0-03` | Configure Enhanced Input subsystem | `IN_PROGRESS` | `DefaultInput.ini` & `InfiniteUniverse.uproject` configured; C++ binding awaits `P0-02` compile & `M1`. |
| `P0-04` | Initialize Git & Git LFS repository | `PASSED` | Local Git repo + Git LFS rules (`.gitattributes`, `.gitignore`) initialized and verified. |
| `P0-05` | Establish coding & asset standards | `PASSED` | `CodingStandards.md`, `Architecture.md`, `AssetPipeline.md` + Blender `5.2.3` validator tested live. |
| `P0-06` | Create default prototype startup map | `BLOCKED` | Awaits Unreal Editor installation (`P0-02` unblock) to save binary `Content/Maps/Prototype/M_Prototype.umap`. |

---

## 2. Master 26-Point Core Prototype Acceptance Checklist (`0 / 26` — Updated Only on Runtime Proof)

### Project Foundation (3)
- [ ] **01.** UE5 C++ project (`InfiniteUniverse.uproject`) opens cleanly in Unreal Editor without errors.
- [ ] **02.** `Development Editor` and `Development` game builds succeed cleanly via UBT.
- [ ] **03.** Git + Git LFS repository and complete `Documentation/` suite are active and up to date.

### Player Movement (3)
- [ ] **04.** Walk, Run (`Shift`), and Jump (`Space` tap) + Landing operate smoothly without jitter.
- [ ] **05.** Third-person `SpringArm` camera rotates 360° smoothly with mouse wheel zoom.
- [ ] **06.** Camera collision prevents clipping through walls, pillars, and ground surfaces.

### Free 3D Flight (4)
- [ ] **07.** Smooth state transitions: `Grounded → Flying → Landing → Grounded` (including from mid-jump/fall).
- [ ] **08.** Full 6-DOF flight movement (Forward, Backward, Left, Right relative to camera + Up `Space` / Down `Ctrl`).
- [ ] **09.** Stationary mid-air `Hover` (no key held) and `Flight Dash` acceleration (`Shift`) work reliably.
- [ ] **10.** Zero sudden character teleportation, zero camera snapping, and zero terrain penetration while flying.

### Action Combat Prototype (5)
- [ ] **11.** Target Lock-On acquires living enemies in range, cycles targets, assists camera framing, and auto-releases on target death/out-of-range.
- [ ] **12.** Basic melee attack and sequential `1 → 2 → 3` combo deal validated damage (works on ground and in air).
- [ ] **13.** `Ki Blast` projectile spawns from hand/socket, consumes `Ki`, travels, collides accurately, and deals damage.
- [ ] **14.** `Dodge` executes cleanly and restores valid combat state; targets play `Hit Reaction` on taking damage.
- [ ] **15.** `HP` and `Ki` attributes and event-driven HUD bars update accurately; dead targets cannot receive extra damage.

### Enemy AI (4)
- [ ] **16.** Enemy idles at spawn, detects player within configurable range, and chases player.
- [ ] **17.** Enemy attacks within attack range, dealing damage to Player, then enters `Recover` state.
- [ ] **18.** Enemy receives damage, plays hit reaction, transitions to `Dead` at `0 HP`, and stops all attacks immediately.
- [ ] **19.** Enemy respawns / resets cleanly via `AIU_EnemySpawner` for repeated combat testing.

### Anime Asset Integration (4)
- [ ] **20.** Blender `5.2.3` anime character mesh imports into UE5 at exact scale (`175–185 cm`) and orientation with zero critical warnings.
- [ ] **21.** Humanoid rig and skeleton are recognized cleanly by UE5.
- [ ] **22.** All 13 minimum locomotion, flight, melee, Ki blast, hit reaction, and death animations play properly.
- [ ] **23.** Shoulder, elbow, hip, and knee joints show no severe deformation or mesh self-intersection across test poses.

### Final Prototype Build (3)
- [ ] **24.** Greybox map (`Lvl_Greybox_Prototype`) provides ground obstacles, camera-collision corridors, vertical flight structures, and enemy arena.
- [ ] **25.** No critical gameplay-blocking defects or crashes remain; frame rate holds steady (`>= 60 FPS` target on RTX 3050 profile).
- [ ] **26.** Final test report and packaged `Win64 Development` build verified.
