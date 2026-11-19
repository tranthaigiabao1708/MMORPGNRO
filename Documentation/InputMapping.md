# Enhanced Input Mapping Specification (`Tasks P0-03, P1-07, P2, P3`)

All player controls use Unreal Engine 5's **Enhanced Input** subsystem (`IMC_Player` + discrete `UInputAction` assets). No raw keys (`EKeys::SpaceBar`, etc.) may be hardcoded inside C++ classes, ensuring 100% runtime remappability.

---

## 1. Input Actions Registry (`Content/Input/Actions/`)

| Input Action Asset | Value Type | Default PC Binding | Context Behavior |
| :--- | :---: | :--- | :--- |
| `IA_Move` | `Axis2D (FVector2D)` | `W / S` (`Y`), `D / A` (`X`) | Ground: Camera-planar walk/run.<br>Flight: Camera-oriented 3D horizontal/forward flight. |
| `IA_Look` | `Axis2D (FVector2D)` | `Mouse XY` | 360° SpringArm orbit & pitch control. |
| `IA_CameraZoom` | `Axis1D (float)` | `Mouse Wheel Up / Down` | Smoothly adjusts `SpringArm->TargetArmLength` within `[220, 600] cm`. |
| `IA_Jump` | `Digital (bool)` | `Space` | **Ground (1st press):** Jump.<br>**Airborne (2nd press):** Activate Free Flight.<br>**Hold while Flying:** Ascend (`+Z`) with 1.0s slow gather → rapid surge up to 1.5x max flight speed. |
| `IA_Descend` | `Digital (bool)` | `Left Shift` | **Ground:** Crouch & move slower (`250 cm/s`).<br>**While Flying:** Descend (`-Z`); touching walkable floor transitions cleanly to `Landing → Grounded`. |
| `IA_FlightToggle` | `Digital (bool)` | `F` *(Cancel only)* | Cancels active `Flying` mode if pressed in air (entering flight requires airborne `Space`). |
| `IA_Sprint` | `Digital (bool)` | `Left Ctrl` | **Ground:** Fast Run (`880 cm/s`).<br>**While Flying:** **`x2.0`** high-speed Flight Boost (`3150 cm/s` planar / `2550 cm/s` vertical). |
| `IA_MeleeAttack` | `Digital (bool)` | `Left Mouse Button` | Executes basic melee & advances `Combo 1 → 2 → 3` inside valid combo window (Ground & Air). |
| `IA_KiBlast` | `Digital (bool)` | `Right Mouse Button` | Fires `AIU_KiProjectile` from hand socket toward locked target or camera aim point (consumes `Ki`). |
| `IA_Dodge` | `Digital (bool)` | `Q` | Directional evade burst on ground or in mid-air with brief invulnerability (`i-frames`); cleanly exits back to `Idle` / `Flying`. |
| `IA_LockOn` | `Digital (bool)` | `Middle Mouse Button` / `C` | Acquires nearest valid living target in range or releases current lock-on. |
| `IA_SwitchTarget` | `Digital (bool)` | `E` | Cycles lock-on among valid living targets in range. |
| `IA_ToggleRotationMode` | `Digital (bool)` | `Tab` | Toggles ground rotation mode between `OrientToCamera` (default) and `OrientToMovement`. |
