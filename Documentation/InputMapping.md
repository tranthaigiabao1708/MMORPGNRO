# Enhanced Input Mapping Specification (`Tasks P0-03, P1-07, P2, P3`)

All player controls use Unreal Engine 5's **Enhanced Input** subsystem (`IMC_Player` + discrete `UInputAction` assets). No raw keys (`EKeys::SpaceBar`, etc.) may be hardcoded inside C++ classes, ensuring 100% runtime remappability.

---

## 1. Input Actions Registry (`Content/Input/Actions/`)

| Input Action Asset | Value Type | Default PC Binding | Context Behavior |
| :--- | :---: | :--- | :--- |
| `IA_Move` | `Axis2D (FVector2D)` | `W / S` (`Y`), `D / A` (`X`) | Ground: Camera-planar walk/run.<br>Flight: Camera-oriented 3D horizontal/forward flight. |
| `IA_Look` | `Axis2D (FVector2D)` | `Mouse XY` | 360° SpringArm orbit & pitch control. |
| `IA_CameraZoom` | `Axis1D (float)` | `Mouse Wheel Up / Down` | Smoothly adjusts `SpringArm->TargetArmLength` within `[220, 600] cm`. |
| `IA_JumpOrFlyUp` | `Digital (bool)` | `Space` *(Tap vs Hold `0.30s`)* | Ground Tap: Jump.<br>Ground/Air Hold (`0.30s`): Smoothly enter `Flying` (`Hover`).<br>While `Flying`: Ascend (`+Z`). |
| `IA_DescendOrCrouch` | `Digital (bool)` | `Left Ctrl` *(Remappable)* | While `Flying`: Descend (`-Z`); touching walkable floor transitions cleanly to `Landing → Grounded`. |
| `IA_FlightToggle` | `Digital (bool)` | `F` *(Optional explicit toggle)* | Toggles `Flying ↔ Landing` cleanly without requiring hold. |
| `IA_SprintOrFlightDash` | `Digital (bool)` | `Left Shift` | Ground: Run (`SprintSpeed`).<br>Flight: High-speed `FlightDashMultiplier` boost. |
| `IA_MeleeAttack` | `Digital (bool)` | `Left Mouse Button` | Executes basic melee & advances `Combo 1 → 2 → 3` inside valid combo window (Ground & Air). |
| `IA_KiBlast` | `Digital (bool)` | `Right Mouse Button` | Fires `AIU_KiProjectile` from hand socket toward locked target or camera aim point (consumes `Ki`). |
| `IA_Dodge` | `Digital (bool)` | `Q` *(or `Left Alt`)* | Directional evade burst on ground or in mid-air; cleanly exits back to `Idle` / `Flying`. |
| `IA_LockOnToggle` | `Digital (bool)` | `Middle Mouse Button` / `Tab` | Acquires nearest valid living enemy in view or releases current lock-on. |
| `IA_SwitchTarget` | `Axis1D (float)` | `E` (`+1`) / `R` (`-1`) | Cycles lock-on left/right among valid living enemies in range. |
