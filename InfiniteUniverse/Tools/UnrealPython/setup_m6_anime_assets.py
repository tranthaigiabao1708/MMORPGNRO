"""
Milestone 6 Anime 3D Character, Stylized Rim-Lit Materials & 15-Animation Import Script (UE 5.8)
Imports from Intermediate/BlenderExports/:
  - /Game/Characters/Player/Meshes/SK_Player (+ SK_Player_Skeleton + Socket_KiMuzzle_R on hand_r + 5 Anime Materials)
  - /Game/Characters/Enemy/Meshes/SK_Enemy (sharing SK_Player_Skeleton + 5 Raider Anime Materials)
  - /Game/Characters/Player/Animations/A_Player_* (all 15 locomotion, flight & combat UAnimSequences)
  - /Game/Characters/Player/ABP_PlayerAnime (UAnimBlueprint parented to UIU_CharacterAnimInstance)
  - Syncs BP_PlayerCharacter & BP_EnemyCharacter SkeletalMesh defaults when unlocked
"""

import os
import sys
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


def create_anime_stylized_material(
    folder: str,
    name: str,
    base_rgb: tuple,
    roughness_val: float,
    rim_rgb: tuple,
    core_emissive_rgb: tuple = (0.0, 0.0, 0.0),
):
    """Creates a 2-Band Anime Cel-Shaded material with crisp shadow step, Fresnel rim-light, and optional core glow."""
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        mat = editor_asset_lib.load_asset(full_path)
        mat_lib.delete_all_material_expressions(mat)
    else:
        mat = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())

    # 2-Band Anime Cel-Toon Shading: SmoothStep(Dot(VertexNormalWS, KeyLightDir)) -> Lerp(ShadowTint,LitTint)
    normal_ws = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, -920, -240)
    light_dir = mat_lib.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -920, -100)
    light_dir.set_editor_property("constant", unreal.LinearColor(0.42, -0.35, 0.84, 1.0))

    ndotl = mat_lib.create_material_expression(mat, unreal.MaterialExpressionDotProduct, -700, -180)
    mat_lib.connect_material_expressions(normal_ws, "", ndotl, "A")
    mat_lib.connect_material_expressions(light_dir, "", ndotl, "B")

    toon_band = mat_lib.create_material_expression(mat, unreal.MaterialExpressionSmoothStep, -520, -180)
    mat_lib.connect_material_expressions(ndotl, "", toon_band, "Value")

    shadow_col = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -680, -380)
    shadow_col.set_editor_property("parameter_name", "ShadowTint")
    shadow_col.set_editor_property(
        "default_value",
        unreal.LinearColor(base_rgb[0] * 0.56, base_rgb[1] * 0.52, min(1.0, base_rgb[2] * 0.64 + 0.02), 1.0),
    )

    base_col = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -680, -40)
    base_col.set_editor_property("parameter_name", "BaseTint")
    base_col.set_editor_property("default_value", unreal.LinearColor(base_rgb[0], base_rgb[1], base_rgb[2], 1.0))

    cel_lerp = mat_lib.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -280, -160)
    mat_lib.connect_material_expressions(shadow_col, "", cel_lerp, "A")
    mat_lib.connect_material_expressions(base_col, "", cel_lerp, "B")
    mat_lib.connect_material_expressions(toon_band, "", cel_lerp, "Alpha")
    mat_lib.connect_material_property(cel_lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)

    rough = mat_lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -320, 60)
    rough.set_editor_property("r", roughness_val)
    mat_lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    spec = mat_lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -320, 140)
    spec.set_editor_property("r", 0.22)
    mat_lib.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    # Stylized Anime Fresnel Rim Highlight + Optional Core Glow (for Eyes / Scouter / Ki Orb)
    fresnel = mat_lib.create_material_expression(mat, unreal.MaterialExpressionFresnel, -620, 260)

    rim_col = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -620, 420)
    rim_col.set_editor_property("parameter_name", "AnimeRimGlow")
    rim_col.set_editor_property("default_value", unreal.LinearColor(rim_rgb[0], rim_rgb[1], rim_rgb[2], 1.0))

    rim_mul = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, 310)
    mat_lib.connect_material_expressions(fresnel, "", rim_mul, "A")
    mat_lib.connect_material_expressions(rim_col, "", rim_mul, "B")

    if any(v > 0.0 for v in core_emissive_rgb):
        core_glow = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -620, 580)
        core_glow.set_editor_property("parameter_name", "CoreEmissive")
        core_glow.set_editor_property(
            "default_value",
            unreal.LinearColor(core_emissive_rgb[0], core_emissive_rgb[1], core_emissive_rgb[2], 1.0),
        )
        emis_add = mat_lib.create_material_expression(mat, unreal.MaterialExpressionAdd, -200, 360)
        mat_lib.connect_material_expressions(rim_mul, "", emis_add, "A")
        mat_lib.connect_material_expressions(core_glow, "", emis_add, "B")
        mat_lib.connect_material_property(emis_add, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        mat_lib.connect_material_property(rim_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mat_lib.recompile_material(mat)
    editor_asset_lib.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def import_skeletal_mesh_fbx(fbx_path: str, dest_folder: str, dest_name: str, shared_skeleton=None):
    ui = unreal.FbxImportUI()
    ui.automated_import_should_detect_type = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.original_import_type = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.import_animations = False
    ui.import_materials = False
    ui.import_textures = False
    ui.create_physics_asset = shared_skeleton is None
    if shared_skeleton is not None:
        ui.skeleton = shared_skeleton

    skeletal_data = ui.skeletal_mesh_import_data
    skeletal_data.set_editor_property("import_uniform_scale", 1.0)
    skeletal_data.set_editor_property("convert_scene", True)
    skeletal_data.set_editor_property("force_front_x_axis", False)

    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = dest_folder
    task.destination_name = dest_name
    task.replace_existing = True
    task.replace_existing_settings = True
    task.automated = True
    task.save = True
    task.factory = unreal.FbxFactory()
    task.options = ui

    asset_tools.import_asset_tasks([task])
    asset_path = f"{dest_folder}/{dest_name}"
    mesh = editor_asset_lib.load_asset(asset_path)
    if not mesh or not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"Failed to import SkeletalMesh at {asset_path} from {fbx_path}")
    return mesh


def import_animation_fbx(fbx_path: str, dest_folder: str, dest_name: str, target_skeleton):
    ui = unreal.FbxImportUI()
    ui.automated_import_should_detect_type = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.original_import_type = unreal.FBXImportType.FBXIT_ANIMATION
    ui.import_mesh = False
    ui.import_as_skeletal = False
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.skeleton = target_skeleton

    anim_data = ui.anim_sequence_import_data
    anim_data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    anim_data.set_editor_property("import_meshes_in_bone_hierarchy", False)
    anim_data.set_editor_property("use_default_sample_rate", True)
    anim_data.set_editor_property("import_bone_tracks", True)
    anim_data.set_editor_property("convert_scene", True)
    anim_data.set_editor_property("force_front_x_axis", False)

    task = unreal.AssetImportTask()
    task.filename = fbx_path
    task.destination_path = dest_folder
    task.destination_name = dest_name
    task.replace_existing = True
    task.replace_existing_settings = True
    task.automated = True
    task.save = True
    task.factory = unreal.FbxFactory()
    task.options = ui

    asset_tools.import_asset_tasks([task])
    asset_path = f"{dest_folder}/{dest_name}"
    anim_seq = editor_asset_lib.load_asset(asset_path)
    if not anim_seq or not isinstance(anim_seq, unreal.AnimSequence):
        raise RuntimeError(f"Failed to import AnimSequence at {asset_path} from {fbx_path}")
    editor_asset_lib.save_loaded_asset(anim_seq, only_if_is_dirty=False)
    return anim_seq


def assign_materials_to_skeletal_mesh(sk_mesh: unreal.SkeletalMesh, ordered_mats: list):
    current_slots = list(sk_mesh.get_editor_property("materials"))
    new_slots = []
    for idx, mat in enumerate(ordered_mats):
        if idx < len(current_slots):
            slot = current_slots[idx]
            slot.set_editor_property("material_interface", mat)
            new_slots.append(slot)
        else:
            slot = unreal.SkeletalMaterial()
            slot.set_editor_property("material_interface", mat)
            slot.set_editor_property("material_slot_name", mat.get_name())
            new_slots.append(slot)
    sk_mesh.set_editor_property("materials", new_slots)


def ensure_ki_muzzle_socket(sk_mesh: unreal.SkeletalMesh):
    ok = unreal.IU_SkeletalAnimDirectorComponent.ensure_skeletal_mesh_socket(
        sk_mesh,
        "Socket_KiMuzzle_R",
        "hand_r",
        unreal.Vector(0.0, 10.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
    )
    if not ok:
        raise RuntimeError("Failed to create Socket_KiMuzzle_R on SK_Player")
    return ok


def create_or_update_player_anim_blueprint(target_skeleton):
    abp_folder = "/Game/Characters/Player"
    abp_name = "ABP_PlayerAnime"
    abp_path = f"{abp_folder}/{abp_name}"
    if editor_asset_lib.does_asset_exist(abp_path):
        abp = editor_asset_lib.load_asset(abp_path)
    else:
        factory = unreal.AnimBlueprintFactory()
        factory.set_editor_property("parent_class", unreal.IU_CharacterAnimInstance)
        factory.set_editor_property("target_skeleton", target_skeleton)
        abp = asset_tools.create_asset(abp_name, abp_folder, unreal.AnimBlueprint, factory)
    if abp:
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
        editor_asset_lib.save_loaded_asset(abp, only_if_is_dirty=False)
    return abp


def sync_character_blueprints(sk_player, sk_enemy):
    content_dir = unreal.Paths.project_content_dir()

    bp_player_disk = os.path.join(content_dir, "Characters", "Player", "BP_PlayerCharacter.uasset")
    if is_package_file_unlocked(bp_player_disk) and editor_asset_lib.does_asset_exist("/Game/Characters/Player/BP_PlayerCharacter"):
        bp_player = editor_asset_lib.load_asset("/Game/Characters/Player/BP_PlayerCharacter")
        cdo = unreal.get_default_object(bp_player.generated_class())
        mesh_comp = cdo.get_editor_property("mesh")
        if mesh_comp:
            mesh_comp.set_skeletal_mesh_asset(sk_player)
            mesh_comp.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
            mesh_comp.set_editor_property("relative_rotation", unreal.Rotator(0.0, -90.0, 0.0))
        ph_body = cdo.get_placeholder_body_mesh()
        if ph_body:
            ph_body.set_visibility(False, True)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp_player)
        editor_asset_lib.save_loaded_asset(bp_player, only_if_is_dirty=False)
        unreal.log("[M6] Synced SK_Player onto BP_PlayerCharacter CDO.")

    bp_enemy_disk = os.path.join(content_dir, "Characters", "Enemy", "BP_EnemyCharacter.uasset")
    if is_package_file_unlocked(bp_enemy_disk) and editor_asset_lib.does_asset_exist("/Game/Characters/Enemy/BP_EnemyCharacter"):
        bp_enemy = editor_asset_lib.load_asset("/Game/Characters/Enemy/BP_EnemyCharacter")
        cdo = unreal.get_default_object(bp_enemy.generated_class())
        mesh_comp = cdo.get_editor_property("mesh")
        if mesh_comp:
            mesh_comp.set_skeletal_mesh_asset(sk_enemy)
            mesh_comp.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -92.0))
            mesh_comp.set_editor_property("relative_rotation", unreal.Rotator(0.0, -90.0, 0.0))
        ph_body = cdo.get_placeholder_body_mesh()
        if ph_body:
            ph_body.set_visibility(False, True)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp_enemy)
        editor_asset_lib.save_loaded_asset(bp_enemy, only_if_is_dirty=False)
        unreal.log("[M6] Synced SK_Enemy onto BP_EnemyCharacter CDO.")


def main():
    # Use deterministic UFbxFactory + UFbxImportUI pipeline in UE 5.8
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")

    proj_dir = unreal.Paths.project_dir()
    export_dir = os.path.abspath(os.path.join(proj_dir, "Intermediate", "BlenderExports"))

    player_fbx = os.path.join(export_dir, "SK_Player.fbx")
    enemy_fbx = os.path.join(export_dir, "SK_Enemy.fbx")
    if not os.path.exists(player_fbx) or not os.path.exists(enemy_fbx):
        raise RuntimeError(f"Missing Blender FBX exports in {export_dir}")

    # 1. Create 6 Stylized Cel-Toon Player Materials, 6 Raider Enemy Materials & Luminous Ki Orb Material
    p_mat_folder = "/Game/Characters/Player/Materials"
    player_mats = [
        create_anime_stylized_material(p_mat_folder, "M_PlayerSkin", (0.96, 0.72, 0.56), 0.65, (0.14, 0.08, 0.06)),
        create_anime_stylized_material(p_mat_folder, "M_PlayerGi", (0.98, 0.32, 0.04), 0.72, (0.26, 0.10, 0.02)),
        create_anime_stylized_material(p_mat_folder, "M_PlayerSash", (0.05, 0.24, 0.86), 0.54, (0.06, 0.18, 0.42)),
        create_anime_stylized_material(p_mat_folder, "M_PlayerHair", (0.03, 0.04, 0.09), 0.38, (0.10, 0.16, 0.36)),
        create_anime_stylized_material(p_mat_folder, "M_PlayerBoots", (0.05, 0.08, 0.18), 0.50, (0.05, 0.08, 0.18)),
        create_anime_stylized_material(p_mat_folder, "M_PlayerEyes", (0.12, 0.82, 1.0), 0.20, (0.30, 0.90, 1.0), (0.35, 1.45, 2.20)),
    ]

    e_mat_folder = "/Game/Characters/Enemy/Materials"
    enemy_mats = [
        create_anime_stylized_material(e_mat_folder, "M_EnemySkin", (0.62, 0.54, 0.72), 0.58, (0.12, 0.07, 0.16)),
        create_anime_stylized_material(e_mat_folder, "M_EnemyArmor", (0.06, 0.05, 0.08), 0.35, (0.18, 0.03, 0.04)),
        create_anime_stylized_material(e_mat_folder, "M_EnemyCrimsonTrim", (0.82, 0.06, 0.09), 0.42, (0.38, 0.04, 0.05)),
        create_anime_stylized_material(e_mat_folder, "M_EnemyCrest", (0.96, 0.12, 0.06), 0.30, (0.55, 0.08, 0.02)),
        create_anime_stylized_material(e_mat_folder, "M_EnemyBoots", (0.04, 0.04, 0.06), 0.38, (0.12, 0.02, 0.02)),
        create_anime_stylized_material(e_mat_folder, "M_EnemyVisor", (1.0, 0.08, 0.12), 0.15, (1.0, 0.20, 0.10), (2.60, 0.18, 0.22)),
    ]

    create_anime_stylized_material(
        "/Game/Combat/VFX",
        "M_KiEnergyOrb",
        (0.25, 0.90, 1.0),
        0.10,
        (1.0, 0.88, 0.35),
        (1.40, 3.80, 5.20),
    )

    # 2. Import SK_Player + Skeleton + Socket_KiMuzzle_R
    sk_player = import_skeletal_mesh_fbx(player_fbx, "/Game/Characters/Player/Meshes", "SK_Player", shared_skeleton=None)
    assign_materials_to_skeletal_mesh(sk_player, player_mats)
    ensure_ki_muzzle_socket(sk_player)

    shared_skel = sk_player.skeleton
    editor_asset_lib.save_loaded_asset(shared_skel, only_if_is_dirty=False)
    editor_asset_lib.save_loaded_asset(sk_player, only_if_is_dirty=False)

    bounds_p = sk_player.get_imported_bounds()
    unreal.log(f"[M6] Imported SK_Player (Height={bounds_p.box_extent.z * 2.0:.2f} cm, Skeleton={shared_skel.get_path_name()})")

    # 3. Import SK_Enemy sharing SK_Player_Skeleton
    sk_enemy = import_skeletal_mesh_fbx(enemy_fbx, "/Game/Characters/Enemy/Meshes", "SK_Enemy", shared_skeleton=shared_skel)
    assign_materials_to_skeletal_mesh(sk_enemy, enemy_mats)
    editor_asset_lib.save_loaded_asset(sk_enemy, only_if_is_dirty=False)

    bounds_e = sk_enemy.get_imported_bounds()
    unreal.log(f"[M6] Imported SK_Enemy (Height={bounds_e.box_extent.z * 2.0:.2f} cm, Skeleton={sk_enemy.skeleton.get_path_name()})")

    # 4. Import all 15 Skeletal Animations onto SK_Player_Skeleton
    anim_names = [
        "A_Player_Idle",
        "A_Player_Walk",
        "A_Player_Run",
        "A_Player_Jump",
        "A_Player_Fall",
        "A_Player_Landing",
        "A_Player_Hover",
        "A_Player_FlyForward",
        "A_Player_FlyBackward",
        "A_Player_Melee_01",
        "A_Player_Melee_02",
        "A_Player_Melee_03",
        "A_Player_KiBlast",
        "A_Player_HitReaction",
        "A_Player_Death",
    ]

    anim_src_dir = os.path.join(export_dir, "Animations")
    for a_name in anim_names:
        a_fbx = os.path.join(anim_src_dir, f"{a_name}.fbx")
        if not os.path.exists(a_fbx):
            raise RuntimeError(f"Missing animation FBX: {a_fbx}")
        seq = import_animation_fbx(a_fbx, "/Game/Characters/Player/Animations", a_name, shared_skel)
        unreal.log(f"[M6] Imported {a_name} ({seq.get_play_length():.2f}s)")

    # 5. Create ABP_PlayerAnime & sync BP_PlayerCharacter + BP_EnemyCharacter
    create_or_update_player_anim_blueprint(shared_skel)
    sync_character_blueprints(sk_player, sk_enemy)

    editor_asset_lib.save_directory("/Game/Characters", only_if_is_dirty=False, recursive=True)
    unreal.log("=== [M6 ANIME ASSETS] ALL SKELETAL MESHES, 10 MATERIALS & 15 ANIMATIONS IMPORTED CLEANLY ===")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        unreal.log_error(f"[M6 SETUP FAILED] {exc}")
        sys.exit(1)
