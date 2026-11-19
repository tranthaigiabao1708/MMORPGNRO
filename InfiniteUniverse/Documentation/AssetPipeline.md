# Blender `5.2` → UE `5.8` Anime Asset Pipeline (`Milestone 6 Standard`)

**Detected Local Tool:** `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe` (`Blender 5.2.2 LTS`)  
**Automated Gate Scripts:**
- `Tools/Blender/validate_and_export_fbx.py` (Scene validator & deterministic UE5 FBX exporter)
- `Tools/Blender/generate_anime_prototype_assets.py` (Stylized 3D Anime Fighter, Raider Enemy & 15-Animation generator)
- `Tools/UnrealPython/setup_m6_anime_assets.py` (UE `5.8` SkeletalMesh, Fresnel Rim-Lit Material, Socket & Animation importer)

---

## 1. Core Rule: Gameplay First, Validated Asset Second

1. Milestones `M1` through `M5` are built and verified first; placeholder cylinder/cube silhouettes auto-hide (`UpdatePlaceholderVisibility()`) the moment a valid `USkeletalMesh` (`SK_Player` / `SK_Enemy`) is equipped.
2. No unvalidated mesh may be imported directly into `Content/Characters/`. Every character asset must pass `validate_and_export_fbx.py` (`0 errors`, `<= 4` bone weights per vertex, `Z = 0.00 cm` soles, `175–185 cm` adult height) before import.

---

## 2. Mandatory Technical Specifications in Blender `5.2`

| Parameter | Required Value | Verified Prototype Output |
| :--- | :--- | :--- |
| **Scene Units** | `Metric`, `Unit Scale = 0.01` | `1 UU = 1 cm` (`1:1` Unreal scale) |
| **Character Height** | `175 – 185 cm` | `SK_Player = 180.1 cm`, `SK_Enemy = 181.6 cm` |
| **World Origin** | `(0, 0, 0)` between feet, soles at `Z = 0 cm` | `FeetZ = 0.00 cm` on both meshes |
| **Object Transforms** | `Loc=(0,0,0)`, `Rot=(0,0,0)`, `Scale=(1,1,1)` | Applied cleanly (`0` transform warnings) |
| **Triangle Budget** | `<= 35,000 tris` (`65,000` ceiling) | `SK_Player = 2,484 tris`, `SK_Enemy = 2,412 tris` |
| **Skin Influences** | Max `4` normalized weights per vertex | Verified `<= 4` weights, `0` unweighted vertices |
| **UV & Normals** | Valid `UVMap` + outward smooth normals | Verified (`FACE` smoothing, `Add Leaf Bones = False`) |

---

## 3. Verified 23-Bone Humanoid Hierarchy & Socket

Both `SK_Player` and `SK_Enemy` share `/Game/Characters/Player/Meshes/SK_Player_Skeleton`:

```text
root (0,0,0)
└── pelvis
    ├── spine_01 -> spine_02 -> spine_03
    │   ├── neck_01 -> head
    │   ├── clavicle_l -> upperarm_l -> lowerarm_l -> hand_l
    │   └── clavicle_r -> upperarm_r -> lowerarm_r -> hand_r  [+ Socket_KiMuzzle_R]
    ├── thigh_l -> calf_l -> foot_l -> ball_l
    └── thigh_r -> calf_r -> foot_r -> ball_r
```

---

## 4. Imported 15 Anime Skeletal Animations (`/Game/Characters/Player/Animations/`)

Driven automatically at runtime by `UIU_SkeletalAnimDirectorComponent` (or custom Blueprint `ABP_PlayerAnime` inheriting `UIU_CharacterAnimInstance`):
1. `A_Player_Idle`
2. `A_Player_Walk`
3. `A_Player_Run`
4. `A_Player_Jump`
5. `A_Player_Fall`
6. `A_Player_Landing`
7. `A_Player_Hover`
8. `A_Player_FlyForward`
9. `A_Player_FlyBackward`
10. `A_Player_Melee_01`
11. `A_Player_Melee_02`
12. `A_Player_Melee_03`
13. `A_Player_KiBlast`
14. `A_Player_HitReaction`
15. `A_Player_Death`

---

## 5. End-to-End Regeneration & Import Commands

```powershell
# 1. Generate, validate & export SK_Player.fbx, SK_Enemy.fbx & 15 A_Player_*.fbx in Blender 5.2
& "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --factory-startup --python "c:\MMORPGNRO\InfiniteUniverse\Tools\Blender\generate_anime_prototype_assets.py"

# 2. Import into UE 5.8 Content/Characters/ with Stylized Fresnel Rim-Lit Materials & Socket_KiMuzzle_R
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "c:\MMORPGNRO\InfiniteUniverse\InfiniteUniverse.uproject" -run=pythonscript -script="c:\MMORPGNRO\InfiniteUniverse\Tools\UnrealPython\setup_m6_anime_assets.py" -unattended -nopause -nosplash -NullRHI
```
