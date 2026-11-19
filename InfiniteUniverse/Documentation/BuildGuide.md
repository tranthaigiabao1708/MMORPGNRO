# Build & Standalone Execution Guide (`InfiniteUniverse` — UE `5.8`)

---

## 1. Prerequisites

Before compiling C++, verify all workstation checks pass in PowerShell:
```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\CheckEnvironment.ps1 -AutoBindUProject
```

Verified Workstation Toolchain:
- **Unreal Engine `5.8`** at `C:\Program Files\Epic Games\UE_5.8`
- **Visual Studio 2022 Community (`17.14.19`)** with **`MSVC 14.44.35207`** and **`Windows 11 SDK 10.0.26100.0`**
- **Blender `5.2.2 LTS`** at `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`
- **Git + Git LFS** (`git version 2.52.0`)

---

## 2. Building from Command Line

### A. Build `Development Editor` Target
From `c:\MMORPGNRO\InfiniteUniverse`:
```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\BuildEditor.ps1 -Configuration Development
```

### B. Build Standalone `Win64 Development` Game Binary (`InfiniteUniverse.exe`)
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" InfiniteUniverse Win64 Development -Project="c:\MMORPGNRO\InfiniteUniverse\InfiniteUniverse.uproject" -WaitMutex -FromMsBuild
```
*(Outputs `c:\MMORPGNRO\InfiniteUniverse\Binaries\Win64\InfiniteUniverse.exe`).*

### C. Run All Headless Automation Tests (`M1` – `M7`)
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "c:\MMORPGNRO\InfiniteUniverse\InfiniteUniverse.uproject" -ExecCmds="Automation RunTests InfiniteUniverse; Quit" -unattended -nopause -nosplash -NullRHI -TestExit="Automation Test Queue Empty"
```

---

## 3. Opening the Project in Unreal Editor

Double-click `OpenInUnrealEditor.bat` or run:
```powershell
Start-Process "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"c:\MMORPGNRO\InfiniteUniverse\InfiniteUniverse.uproject"'
```
*(Default startup map: `/Game/Maps/Prototype/Lvl_Greybox_Prototype`).*

---

## 4. Workstation & Baseline Optimization Notes

1. **Workstation Tuning (`RTX 5060 Ti 16GB VRAM` / `i5-14600KF` / `32GB RAM`):**
   - `Config/BuildConfiguration.xml` configures parallel compilation up to `MaxProcessorCount=18`.
   - `Config/DefaultEngine.ini` allocates `r.Streaming.PoolSize=4096` (`4 GB` texture streaming pool) while preserving baseline scalability compatibility (`>= 60 FPS` target even on `RTX 3050` class hardware).
2. **Zero-Tick Discipline:** Core gameplay components (`UIU_AttributeComponent`, `UIU_DamageComponent`, `UIU_CombatComponent` when not dashing, `AIU_PlayerCharacter` when camera is settled, `AIU_EnemySpawner`, and `UIU_PlayerHUDWidget`) run event/timer-driven with tick disabled when idle.
