# Blender `5.2.3` → UE5 Anime Asset Pipeline (`Phase 6 Standard`)

**Detected Local Tool:** `D:\blender\blender.exe` (`Blender 5.2.3`)  
**Automated Gate Script:** `Tools/Blender/validate_and_export_fbx.py`

---

## 1. Core Rule: Gameplay First, Validated Asset Second

1. Milestones `M1` through `M5` are built and verified using a clean UE5 Mannequin / Capsule placeholder first.
2. An AI-generated or sculpted high-poly statue mesh MUST NOT be imported directly into `Content/Characters/`. Every character asset must pass retopology, UV unwrapping, humanoid skinning, and automated validation via `validate_and_export_fbx.py` before replacing the placeholder.

---

## 2. Mandatory Technical Specifications in Blender `5.2.3`

| Parameter | Required Value | Reason |
| :--- | :--- | :--- |
| **Scene Units** | `Metric`, `Unit Scale = 0.01` | Exact `1:1` match with Unreal Units (`1 UU = 1 cm`). |
| **Character Height** | `175 – 185 cm` (Adult baseline; allowed validator envelope `140 – 220 cm`) | Matches capsule collision and camera framing defaults. |
| **World Origin** | `(0, 0, 0)` centered between both feet, soles resting at `Z = 0 cm` | Prevents floating or sunken feet in UE5 capsule. |
| **Object Transforms** | `Location = (0,0,0)`, `Rotation = (0,0,0)`, `Scale = (1,1,1)` (`Ctrl+A -> All Transforms`) | Eliminates 100x scale bugs and 90° flipped animations on FBX import. |
| **Triangle Budget** | `15,000 – 35,000 tris` (Hard prototype ceiling: `65,000 tris`) | Smooth skinning performance on `RTX 3050 4GB VRAM`. |
| **Skin Influences** | Max `4` non-zero bone weights per vertex (`Weight -> Limit Total = 4`), normalized | Matches UE5 standard GPU skin cache without weight truncation artifacts. |
| **UV & Normals** | Valid non-overlapping `UVMap`; outward-facing normals (`Shift+N`) | Prevents inside-out cel-shading outlines and broken textures. |

---

## 3. Required Humanoid Bone Hierarchy

The armature must maintain stable bone names across all exports:

```text
root (Head at 0,0,0 - top-level, no parent)
└── pelvis
    ├── spine_01
    │   └── spine_02
    │       ├── neck_01
    │       │   └── head
    │       ├── upperarm_l -> lowerarm_l -> hand_l
    │       └── upperarm_r -> lowerarm_r -> hand_r   (Attach Socket_KiMuzzle_R in UE5)
    ├── thigh_l -> calf_l -> foot_l
    └── thigh_r -> calf_r -> foot_r
```
*(Optional `clavicle_l/r`, `spine_03`, `ball_l/r`, and hair/cloth physics bones are supported as children of the corresponding parent bones).*

---

## 4. Minimum 13 Prototype Animations (`Phase 6`)

`Idle`, `Walk`, `Run`, `Jump`, `Fall`, `Landing`, `Hover`, `Fly_Forward`, `Fly_Backward`, `Melee_Attack` (3-hit montage or sequence), `Ki_Blast`, `Hit_Reaction`, `Death`.

*(Animations may be authored directly or retargeted in UE5 via `IK Rig / IK Retargeter` from standard humanoid animations, provided shoulder/elbow/knee deformations pass visual inspection).*

---

## 5. Automated Validation & Export Commands

### Validate a `.blend` File Without Exporting
```powershell
& "D:\blender\blender.exe" -b path\to\Player_Character.blend -P .\Tools\Blender\validate_and_export_fbx.py -- --validate-only
```

### Validate and Export UE5-Ready Skeletal Mesh FBX
```powershell
& "D:\blender\blender.exe" -b path\to\Player_Character.blend -P .\Tools\Blender\validate_and_export_fbx.py -- --export .\Content\Characters\Player\Meshes\SK_Player.fbx
```
*(Enforces `Smoothing = FACE`, `Add Leaf Bones = False`, `-Y Forward / Z Up`, and excludes all Cameras, Lights, and Helper Widgets).*
