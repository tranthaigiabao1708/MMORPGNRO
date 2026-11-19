# Build & Editor Execution Guide (`InfiniteUniverse`)

---

## 1. Prerequisites

Before compiling C++, verify all checks pass in PowerShell:
```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\CheckEnvironment.ps1 -AutoBindUProject
```

Required components:
- **Unreal Engine `5.4` or `5.5`** installed on `D:\Epic Games\` (Core Components + Engine Source).
- **Visual Studio 2022 or 2026** with:
  - Workload: **Game development with C++**
  - Individual Components: **`MSVC v143 - VS 2022 C++ x64/x86 build tools`**, **`Windows 11 SDK`**, **`.NET 8.0 Runtime`**.
- **Git + Git LFS** (`git lfs install`).

---

## 2. Building from Command Line (Recommended for AI Agent & CI)

From `d:\Openword_Dragonboy\InfiniteUniverse`:
```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\BuildEditor.ps1 -Configuration Development
```

### Generating Visual Studio Solution Files (Optional for IDE Debugging)
Once UE5 is installed, generate `InfiniteUniverse.sln` via UnrealBuildTool:
```powershell
& "D:\Epic Games\UE_5.5\Engine\Build\BatchFiles\Build.bat" -projectfiles -project="d:\Openword_Dragonboy\InfiniteUniverse\InfiniteUniverse.uproject" -game -rocket -progress
```

---

## 3. Opening the Project in Unreal Editor

```powershell
$env:UE-LocalDataCachePath = "D:\UE_DerivedDataCache"
Start-Process "D:\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"d:\Openword_Dragonboy\InfiniteUniverse\InfiniteUniverse.uproject"'
```

---

## 4. Hardware Optimization Notes (RTX 3050 4GB VRAM / 16GB RAM)

1. **VRAM Protection:** `Config/DefaultEngine.ini` disables Virtual Shadow Maps (`r.Shadow.Virtual.Enable=0`) and Lumen (`r.DynamicGlobalIlluminationMethod=0`) by default. Keep Editor Viewport Scalability at **High** (not *Cinematic*) to stay well under `2.8 GB VRAM`.
2. **RAM Protection During C++ Compile:** `Config/BuildConfiguration.xml` caps parallel compiler processes at `6` (`MaxProcessorCount=6`). Avoid running Blender high-subdivision renders simultaneously while compiling full rebuilds.
3. **Protect Drive `C:`:** Always keep `UE-LocalDataCachePath=D:\UE_DerivedDataCache` active (can also be added permanently to Windows User Environment Variables).
