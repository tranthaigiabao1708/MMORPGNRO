"""
Milestone 5 Greybox Proving Ground & Genshin Camera Blueprint Sync Script
- Creates /Game/Maps/Prototype/Lvl_Greybox_Prototype (240m x 240m x 68m Proving Ground, safe even if M_Prototype is open in GUI Editor!)
- Also syncs BP_PlayerCharacter, BP_PlayerController, and M_Prototype on disk whenever not file-locked by an active GUI Editor
- Builds all 6 Greybox Test Sectors (P5-01..P5-07):
    P5-01 & P5-02: Wide walkable Ground Floor + 4 Perimeter Walls + 3 Slope Ramps (15, 25, 35 deg)
    P5-03: Parkour Jump & Step Platforms (North Sector +Y)
    P5-04: Tight Camera Collision Colonnade, Low Lintels & Alley (East Sector +X)
    P5-05 & P5-06: Walled Martial Combat Arena + Cover Pillars + 4 Enemy AI Spawners (West Sector -X)
    P5-07: Stepped Cliff Terraces + 67m Sky Spire + 5 Luminous 3D Flight Archway Gates (South & Sky Sector -Y/+Z)
"""
import os
import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary
mat_lib = unreal.MaterialEditingLibrary


def is_package_file_unlocked(disk_path: str) -> bool:
    if not os.path.exists(disk_path):
        return True
    try:
        with open(disk_path, "r+b"):
            return True
    except OSError:
        return False


def create_or_update_solid_material(folder: str, name: str, base_rgb: tuple, roughness_val: float, emissive_rgb=(0.0, 0.0, 0.0)):
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        mat = editor_asset_lib.load_asset(full_path)
        mat_lib.delete_all_material_expressions(mat)
    else:
        mat = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())

    col = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -320, -100)
    col.set_editor_property("parameter_name", "BaseTint")
    col.set_editor_property("default_value", unreal.LinearColor(base_rgb[0], base_rgb[1], base_rgb[2], 1.0))
    mat_lib.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = mat_lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -260, 120)
    rough.set_editor_property("r", roughness_val)
    mat_lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    if any(v > 0.0 for v in emissive_rgb):
        emis = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -320, 220)
        emis.set_editor_property("parameter_name", "EmissiveGlow")
        emis.set_editor_property("default_value", unreal.LinearColor(emissive_rgb[0], emissive_rgb[1], emissive_rgb[2], 1.0))
        mat_lib.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mat_lib.recompile_material(mat)
    editor_asset_lib.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def try_sync_genshin_camera_blueprints():
    content_dir = unreal.Paths.project_content_dir()
    bp_char_disk = os.path.join(content_dir, "Characters", "Player", "BP_PlayerCharacter.uasset")
    bp_pc_disk = os.path.join(content_dir, "Core", "BP_PlayerController.uasset")

    if is_package_file_unlocked(bp_char_disk):
        bp_char = editor_asset_lib.load_asset("/Game/Characters/Player/BP_PlayerCharacter")
        char_cdo = unreal.get_default_object(bp_char.generated_class())
        char_cdo.set_editor_property("DefaultCameraArmLength", 420.0)
        char_cdo.set_editor_property("MinCameraArmLength", 200.0)
        char_cdo.set_editor_property("MaxCameraArmLength", 700.0)
        char_cdo.set_editor_property("CameraTargetOffset", unreal.Vector(0.0, 0.0, 68.0))
        char_cdo.set_editor_property("CameraSocketOffset", unreal.Vector(0.0, 0.0, 52.0))
        char_cdo.set_editor_property("DefaultCameraFOV", 86.0)
        char_cdo.set_editor_property("FlightDashCameraFOV", 100.0)
        boom = char_cdo.get_camera_boom()
        if boom:
            boom.set_editor_property("target_arm_length", 420.0)
            boom.set_editor_property("target_offset", unreal.Vector(0.0, 0.0, 68.0))
            boom.set_editor_property("socket_offset", unreal.Vector(0.0, 0.0, 52.0))
        cam = char_cdo.get_follow_camera()
        if cam:
            cam.set_editor_property("field_of_view", 86.0)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp_char)
        editor_asset_lib.save_loaded_asset(bp_char, only_if_is_dirty=False)

    if is_package_file_unlocked(bp_pc_disk):
        bp_pc = editor_asset_lib.load_asset("/Game/Core/BP_PlayerController")
        pc_cdo = unreal.get_default_object(bp_pc.generated_class())
        pc_cdo.set_editor_property("DefaultStartingCameraPitch", -8.5)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp_pc)
        editor_asset_lib.save_loaded_asset(bp_pc, only_if_is_dirty=False)


def spawn_box(actor_subsys, mesh, mat, label: str, loc: unreal.Vector, scale: unreal.Vector, rot=unreal.Rotator(0.0, 0.0, 0.0)):
    act = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    act.set_actor_label(label)
    smc = act.get_editor_property("static_mesh_component")
    smc.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    smc.set_static_mesh(mesh)
    smc.set_collision_profile_name("BlockAll")
    if mat:
        smc.set_material(0, mat)
    act.set_actor_scale3d(scale)
    return act


def build_flight_archway_gate(actor_subsys, cube_mesh, gate_mat, prefix: str, center_loc: unreal.Vector, yaw_deg: float):
    rot = unreal.Rotator(0.0, yaw_deg, 0.0)
    right_vec = unreal.MathLibrary.get_right_vector(rot)
    half_span = 360.0

    spawn_box(actor_subsys, cube_mesh, gate_mat, f"{prefix}_LeftPillar", center_loc - (right_vec * half_span), unreal.Vector(0.8, 0.8, 6.0), rot)
    spawn_box(actor_subsys, cube_mesh, gate_mat, f"{prefix}_RightPillar", center_loc + (right_vec * half_span), unreal.Vector(0.8, 0.8, 6.0), rot)
    spawn_box(actor_subsys, cube_mesh, gate_mat, f"{prefix}_TopBar", center_loc + unreal.Vector(0.0, 0.0, 320.0), unreal.Vector(0.8, 8.0, 0.7), rot)
    spawn_box(actor_subsys, cube_mesh, gate_mat, f"{prefix}_BottomBar", center_loc - unreal.Vector(0.0, 0.0, 320.0), unreal.Vector(0.8, 8.0, 0.7), rot)


def populate_greybox_sectors_on_current_level(actor_subsys, mats):
    m_ground, m_parkour, m_occlusion, m_arena, m_gate = mats

    managed_prefixes = ("IU_Greybox_", "IU_Sky", "IU_PracticeDummy", "IU_EnemySpawner")
    for act in actor_subsys.get_all_level_actors():
        lbl = act.get_actor_label()
        if any(lbl.startswith(p) for p in managed_prefixes) or "MatPreview" in lbl or isinstance(act, unreal.TextRenderActor):
            actor_subsys.destroy_actor(act)

    starts = [a for a in actor_subsys.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    if starts:
        starts[0].set_actor_location_and_rotation(unreal.Vector(0.0, 0.0, 110.0), unreal.Rotator(0.0, 0.0, 0.0), False, True)
    else:
        ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 110.0), unreal.Rotator(0.0, 0.0, 0.0))
        ps.set_actor_label("PlayerStart_Greybox")

    cube = editor_asset_lib.load_asset("/Engine/BasicShapes/Cube")
    cyl = editor_asset_lib.load_asset("/Engine/BasicShapes/Cylinder")

    # P5-01 & P5-02: 240m x 240m Ground Slab & Perimeter Walls + 3 Slope Ramps
    spawn_box(actor_subsys, cube, m_ground, "IU_Greybox_MainFloor", unreal.Vector(0.0, 0.0, -55.0), unreal.Vector(240.0, 240.0, 1.0))
    spawn_box(actor_subsys, cube, m_ground, "IU_Greybox_Wall_North", unreal.Vector(0.0, 12000.0, 300.0), unreal.Vector(242.0, 1.5, 7.0))
    spawn_box(actor_subsys, cube, m_ground, "IU_Greybox_Wall_South", unreal.Vector(0.0, -12000.0, 300.0), unreal.Vector(242.0, 1.5, 7.0))
    spawn_box(actor_subsys, cube, m_ground, "IU_Greybox_Wall_East", unreal.Vector(12000.0, 0.0, 300.0), unreal.Vector(1.5, 242.0, 7.0))
    spawn_box(actor_subsys, cube, m_ground, "IU_Greybox_Wall_West", unreal.Vector(-12000.0, 0.0, 300.0), unreal.Vector(1.5, 242.0, 7.0))

    for idx, angle in enumerate((15.0, 25.0, 35.0)):
        spawn_box(
            actor_subsys, cube, m_parkour, f"IU_Greybox_SlopeRamp_{int(angle)}deg",
            unreal.Vector(450.0 + idx * 380.0, 1450.0, 70.0 + idx * 25.0),
            unreal.Vector(2.6, 7.0, 0.35),
            unreal.Rotator(0.0, 90.0, -angle)
        )

    # P5-03: Jump, Vault & Parkour Step Course (North Sector +Y)
    steps = [
        ("IU_Greybox_JumpStep_01", unreal.Vector(-350.0, 1300.0, 30.0), unreal.Vector(3.2, 3.2, 0.6)),
        ("IU_Greybox_JumpStep_02", unreal.Vector(-350.0, 1720.0, 62.5), unreal.Vector(3.2, 3.2, 1.25)),
        ("IU_Greybox_JumpStep_03", unreal.Vector(-350.0, 2180.0, 97.5), unreal.Vector(3.4, 3.4, 1.95)),
        ("IU_Greybox_JumpStep_04", unreal.Vector(-350.0, 2680.0, 135.0), unreal.Vector(3.6, 3.6, 2.70)),
        ("IU_Greybox_JumpStep_05", unreal.Vector(-350.0, 3220.0, 175.0), unreal.Vector(3.8, 3.8, 3.50)),
        ("IU_Greybox_JumpTerrace_Top", unreal.Vector(-350.0, 3950.0, 215.0), unreal.Vector(8.0, 7.0, 4.30)),
    ]
    for lbl, loc, sc in steps:
        spawn_box(actor_subsys, cube, m_parkour, lbl, loc, sc)

    # P5-04: Camera Collision Colonnade, Low Lintels & Tight Alley (East Sector +X)
    spawn_box(actor_subsys, cube, m_occlusion, "IU_Greybox_AlleyWall_Left", unreal.Vector(2800.0, -240.0, 210.0), unreal.Vector(16.0, 0.5, 4.2))
    spawn_box(actor_subsys, cube, m_occlusion, "IU_Greybox_AlleyWall_Right", unreal.Vector(2800.0, 240.0, 210.0), unreal.Vector(16.0, 0.5, 4.2))
    spawn_box(actor_subsys, cube, m_occlusion, "IU_Greybox_AlleyRoof_Low", unreal.Vector(3100.0, 0.0, 330.0), unreal.Vector(7.0, 5.2, 0.4))

    for idx in range(6):
        px = 2100.0 + idx * 340.0
        spawn_box(actor_subsys, cyl, m_occlusion, f"IU_Greybox_Colonnade_L_{idx}", unreal.Vector(px, -580.0, 220.0), unreal.Vector(0.95, 0.95, 4.4))
        spawn_box(actor_subsys, cyl, m_occlusion, f"IU_Greybox_Colonnade_R_{idx}", unreal.Vector(px, -920.0, 220.0), unreal.Vector(0.95, 0.95, 4.4))
    spawn_box(actor_subsys, cube, m_occlusion, "IU_Greybox_Colonnade_Lintel", unreal.Vector(2950.0, -750.0, 450.0), unreal.Vector(19.0, 4.5, 0.45))

    # Forward Aiming Courtyard Practice Dummies (Right in front of PlayerStart +X)
    dummy_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_PracticeDummy")
    dummies = [
        ("IU_PracticeDummy_Ground_Center", unreal.Vector(720.0, 0.0, 95.0), False),
        ("IU_PracticeDummy_Ground_Right_Striker", unreal.Vector(820.0, 280.0, 95.0), True),
        ("IU_PracticeDummy_Elevated_Pedestal", unreal.Vector(1150.0, -280.0, 415.0), False),
    ]
    spawn_box(actor_subsys, cyl, m_parkour, "IU_Greybox_DummyPedestal", unreal.Vector(1150.0, -280.0, 160.0), unreal.Vector(2.2, 2.2, 3.2))
    for lbl, loc, striker in dummies:
        d = actor_subsys.spawn_actor_from_class(dummy_cls, loc, unreal.Rotator(0.0, 180.0, 0.0))
        d.set_actor_label(lbl)
        d.set_editor_property("bEnablePeriodicTestStrike", striker)

    # P5-05 & P5-06: Walled Martial Combat Arena & 4 Enemy AI Spawners (West Sector -X)
    arena_center = unreal.Vector(-2600.0, 0.0, 0.0)
    spawn_box(actor_subsys, cube, m_arena, "IU_Greybox_ArenaRim_North", arena_center + unreal.Vector(0.0, 1800.0, 55.0), unreal.Vector(36.0, 0.6, 1.1))
    spawn_box(actor_subsys, cube, m_arena, "IU_Greybox_ArenaRim_South", arena_center + unreal.Vector(0.0, -1800.0, 55.0), unreal.Vector(36.0, 0.6, 1.1))
    spawn_box(actor_subsys, cube, m_arena, "IU_Greybox_ArenaRim_Back", arena_center + unreal.Vector(-1800.0, 0.0, 90.0), unreal.Vector(0.6, 36.6, 1.8))

    for idx, offset in enumerate([(-650.0, -650.0), (-650.0, 650.0), (650.0, -650.0), (650.0, 650.0)]):
        spawn_box(
            actor_subsys, cyl, m_arena, f"IU_Greybox_ArenaCoverPillar_{idx}",
            arena_center + unreal.Vector(offset[0], offset[1], 190.0), unreal.Vector(1.3, 1.3, 3.8)
        )

    bp_spawner = editor_asset_lib.load_asset("/Game/AI/BP_EnemySpawner")
    spawner_gen_cls = bp_spawner.generated_class()
    spawners = [
        ("IU_EnemySpawner_Arena_Duelist", unreal.Vector(-1850.0, 0.0, 98.0), 0.0, False),
        ("IU_EnemySpawner_Arena_Pack_A", unreal.Vector(-3550.0, -420.0, 98.0), 0.0, False),
        ("IU_EnemySpawner_Arena_Pack_B", unreal.Vector(-3550.0, 420.0, 98.0), 0.0, False),
        ("IU_EnemySpawner_Sky_Guardian", unreal.Vector(-2200.0, -2400.0, 1720.0), 45.0, True),
    ]
    for lbl, loc, yaw, is_flyer in spawners:
        sp = actor_subsys.spawn_actor_from_class(spawner_gen_cls, loc, unreal.Rotator(0.0, yaw, 0.0))
        sp.set_actor_label(lbl)
        sp.set_editor_property("bSpawnAerialFlyer", is_flyer)
        sp.set_editor_property("RespawnDelaySeconds", 4.0)

    # P5-07: Vertical Cliff Terraces, 67m Sky Spire & 5-Gate 3D Flight Course
    terraces = [
        ("IU_Greybox_CliffTerrace_06m", unreal.Vector(-1200.0, -2400.0, 300.0), unreal.Vector(12.0, 12.0, 6.0)),
        ("IU_Greybox_CliffTerrace_16m", unreal.Vector(-2200.0, -3400.0, 800.0), unreal.Vector(14.0, 14.0, 16.0)),
        ("IU_Greybox_CliffTerrace_30m", unreal.Vector(-800.0, -4700.0, 1500.0), unreal.Vector(15.0, 15.0, 30.0)),
        ("IU_Greybox_CliffTerrace_46m", unreal.Vector(900.0, -5400.0, 2300.0), unreal.Vector(16.0, 16.0, 46.0)),
    ]
    for lbl, loc, sc in terraces:
        spawn_box(actor_subsys, cube, m_ground, lbl, loc, sc)

    spawn_box(actor_subsys, cyl, m_occlusion, "IU_Greybox_SkySpire_Core", unreal.Vector(2200.0, -3800.0, 3300.0), unreal.Vector(5.5, 5.5, 66.0))
    spawn_box(actor_subsys, cyl, m_parkour, "IU_Greybox_SkySanctuary_Top67m", unreal.Vector(2200.0, -3800.0, 6650.0), unreal.Vector(16.0, 16.0, 1.0))

    flight_gates = [
        ("IU_Greybox_FlightGate_01_8m", unreal.Vector(300.0, -1500.0, 800.0), -65.0),
        ("IU_Greybox_FlightGate_02_18m", unreal.Vector(-900.0, -2700.0, 1850.0), -50.0),
        ("IU_Greybox_FlightGate_03_32m", unreal.Vector(-300.0, -4100.0, 3250.0), -15.0),
        ("IU_Greybox_FlightGate_04_48m", unreal.Vector(1100.0, -4900.0, 4850.0), 35.0),
        ("IU_Greybox_FlightGate_05_60m", unreal.Vector(1900.0, -4200.0, 6000.0), 75.0),
    ]
    for prefix, loc, yaw in flight_gates:
        build_flight_archway_gate(actor_subsys, cube, m_gate, prefix, loc, yaw)


def build_greybox_maps():
    mat_dir = "/Game/Developer/TestAssets"
    mats = (
        create_or_update_solid_material(mat_dir, "M_Greybox_Ground", (0.17, 0.19, 0.22), 0.82),
        create_or_update_solid_material(mat_dir, "M_Greybox_Parkour", (0.55, 0.42, 0.20), 0.68),
        create_or_update_solid_material(mat_dir, "M_Greybox_Occlusion", (0.22, 0.30, 0.38), 0.62),
        create_or_update_solid_material(mat_dir, "M_Greybox_Arena", (0.28, 0.14, 0.16), 0.70),
        create_or_update_solid_material(mat_dir, "M_Greybox_FlightGate", (0.08, 0.68, 0.88), 0.35, (0.12, 0.95, 1.35)),
    )

    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    content_dir = unreal.Paths.project_content_dir()

    # 1. Always build/update dedicated Lvl_Greybox_Prototype (never locked by M_Prototype)
    greybox_pkg = "/Game/Maps/Prototype/Lvl_Greybox_Prototype"
    greybox_disk = os.path.join(content_dir, "Maps", "Prototype", "Lvl_Greybox_Prototype.umap")
    if is_package_file_unlocked(greybox_disk):
        if editor_asset_lib.does_asset_exist(greybox_pkg):
            level_subsys.load_level(greybox_pkg)
        else:
            level_subsys.new_level_from_template(greybox_pkg, "/Engine/Maps/Templates/TimeOfDay_Default")
        populate_greybox_sectors_on_current_level(actor_subsys, mats)
        level_subsys.save_current_level()
        unreal.log("=== [IU M5] Saved /Game/Maps/Prototype/Lvl_Greybox_Prototype ===")

    # 2. Also update M_Prototype on disk if not currently locked by open GUI Editor
    m_proto_disk = os.path.join(content_dir, "Maps", "Prototype", "M_Prototype.umap")
    if is_package_file_unlocked(m_proto_disk):
        if level_subsys.load_level("/Game/Maps/Prototype/M_Prototype"):
            populate_greybox_sectors_on_current_level(actor_subsys, mats)
            level_subsys.save_current_level()
            unreal.log("=== [IU M5] Also updated /Game/Maps/Prototype/M_Prototype ===")
    else:
        unreal.log("=== [IU M5] M_Prototype.umap is open in GUI Editor; open Lvl_Greybox_Prototype in Editor to play M5 Greybox! ===")


try_sync_genshin_camera_blueprints()
build_greybox_maps()
