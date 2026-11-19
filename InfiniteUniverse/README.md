# InfiniteUniverse — Phase 1: Core Gameplay Prototype

**Engine:** Unreal Engine 5 (`C++ Core` + `Blueprint Presentation`)  
**Input:** Enhanced Input Subsystem  
**Asset Pipeline:** Blender `5.2.3` → FBX → UE5  
**Scope:** Single-Player Offline Core Gameplay Prototype (3rd-Person Movement, Free 3D Flight, 3-Hit Melee + Ki Blast + Lock-On + Dodge, 1 Basic Enemy AI, 1 Greybox Arena, 1 Anime Character Integration)

---

## 1. Quick Start & Build Commands

### Step A — Verify Development Environment
Run the automated environment checker from PowerShell:
```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\CheckEnvironment.ps1 -AutoBindUProject
```
*(If any item reports `[BLOCKED]`, follow the exact installation instructions in [Documentation/EnvironmentReport.md](./Documentation/EnvironmentReport.md)).*

### Step B — Build `InfiniteUniverseEditor` (Win64 Development)
```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\BuildEditor.ps1 -Configuration Development
```
This script automatically:
1. Binds `InfiniteUniverse.uproject` to your installed `UE 5.4 / 5.5` version.
2. Redirects `UE-LocalDataCachePath` to `D:\UE_DerivedDataCache` so `C:\` free space is never exhausted.
3. Compiles the `InfiniteUniverse` C++ module with safe parallel memory caps (`MaxProcessorCount=6` for 16 GB RAM).

### Step C — Validate & Export Blender 5.2 Anime Assets
```powershell
& "D:\blender\blender.exe" -b path\to\Player_Character.blend -P .\Tools\Blender\validate_and_export_fbx.py -- --export .\Content\Characters\Player\Meshes\SK_Player.fbx
```

---

## 2. Project Documentation Index

- [EnvironmentReport.md](./Documentation/EnvironmentReport.md) — Hardware audit (`RTX 3050 4GB VRAM`, `16GB RAM`, disk layout) and toolchain blocker resolution guide.
- [Architecture.md](./Documentation/Architecture.md) — Modular C++ component architecture (`Core`, `Characters`, `Movement`, `Combat`, `Attributes`, `AI`, `UI`, `Data`).
- [CodingStandards.md](./Documentation/CodingStandards.md) — C++ UHT class prefixes, UE5 asset naming conventions, Zero-Tick rules, and strict scope guardrails.
- [BuildGuide.md](./Documentation/BuildGuide.md) — Detailed CLI & Visual Studio build workflow, 4GB VRAM Editor optimization, and troubleshooting.
- [InputMapping.md](./Documentation/InputMapping.md) — Remappable Enhanced Input Actions (`IA_*`) and Context (`IMC_Player`) for Ground, 3D Flight, and Combat.
- [AssetPipeline.md](./Documentation/AssetPipeline.md) — Blender `5.2.3` scale, topology, humanoid bone hierarchy, and UE5 FBX export checklist.
- [AcceptanceTests.md](./Documentation/AcceptanceTests.md) — Milestone `M0`–`M7` task ledger and the 26-Point Core Prototype Acceptance Checklist.
