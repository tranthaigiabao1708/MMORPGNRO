"""
Milestone 3 Asset, HUD, Afterimage Ghost Shader & Combat Target Setup Script
- Creates IA_MeleeAttack (LMB), IA_KiBlast (Q), IA_Dodge (RMB - Unified A->B Dash / InstantBlink / Wukong Mirage Evade),
  IA_LockOn (MiddleMouse / C), IA_SwitchTarget (E), IA_ToggleMobilityStyle (V)
- Generates /Game/Combat/VFX/M_AfterimageGhost translucent spirit material for Black Myth: Wukong style evade decoy
- Updates IMC_Player, BP_PlayerController, BP_PlayerCharacter, BP_GameMode, BP_KiProjectile, and M_Prototype dummies
"""
import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary
mat_lib = unreal.MaterialEditingLibrary


def get_or_create_input_action(name: str, folder: str, value_type: unreal.InputActionValueType) -> unreal.InputAction:
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        ia = editor_asset_lib.load_asset(full_path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.InputAction)
        ia = asset_tools.create_asset(name, folder, unreal.InputAction, factory)
    ia.set_editor_property("value_type", value_type)
    editor_asset_lib.save_loaded_asset(ia, only_if_is_dirty=False)
    return ia


def get_or_create_blueprint(name: str, folder: str, parent_class: unreal.Class, factory_cls=unreal.BlueprintFactory):
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        bp = editor_asset_lib.load_asset(full_path)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        editor_asset_lib.save_loaded_asset(bp, only_if_is_dirty=False)
        return bp
    factory = factory_cls()
    factory.set_editor_property("parent_class", parent_class)
    bp = asset_tools.create_asset(name, folder, None, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    editor_asset_lib.save_loaded_asset(bp, only_if_is_dirty=False)
    return bp


def create_or_update_afterimage_material():
    mat_folder = "/Game/Combat/VFX"
    mat_name = "M_AfterimageGhost"
    full_path = f"{mat_folder}/{mat_name}"

    if editor_asset_lib.does_asset_exist(full_path):
        mat = editor_asset_lib.load_asset(full_path)
        mat_lib.delete_all_material_expressions(mat)
    else:
        mat = asset_tools.create_asset(mat_name, mat_folder, unreal.Material, unreal.MaterialFactoryNew())

    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("used_with_skeletal_mesh", True)

    color_param = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -380, -100)
    color_param.set_editor_property("parameter_name", "GhostColor")
    color_param.set_editor_property("default_value", unreal.LinearColor(0.22, 0.85, 1.0, 1.0))

    emissive_boost = mat_lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -380, 60)
    emissive_boost.set_editor_property("r", 2.4)

    mul_expr = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -150, -40)
    mat_lib.connect_material_expressions(color_param, "", mul_expr, "A")
    mat_lib.connect_material_expressions(emissive_boost, "", mul_expr, "B")
    mat_lib.connect_material_property(mul_expr, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity_param = mat_lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -220, 160)
    opacity_param.set_editor_property("parameter_name", "GhostOpacity")
    opacity_param.set_editor_property("default_value", 0.62)
    mat_lib.connect_material_property(opacity_param, "", unreal.MaterialProperty.MP_OPACITY)

    mat_lib.recompile_material(mat)
    editor_asset_lib.save_loaded_asset(mat, only_if_is_dirty=False)
    unreal.log("=== [IU M3] Built Translucent Mirage Shader /Game/Combat/VFX/M_AfterimageGhost ===")


def setup_m3_assets():
    create_or_update_afterimage_material()

    actions_dir = "/Game/Input/Actions"
    ia_move = get_or_create_input_action("IA_Move", actions_dir, unreal.InputActionValueType.AXIS2D)
    ia_look = get_or_create_input_action("IA_Look", actions_dir, unreal.InputActionValueType.AXIS2D)
    ia_jump = get_or_create_input_action("IA_Jump", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_descend = get_or_create_input_action("IA_Descend", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_flight_toggle = get_or_create_input_action("IA_FlightToggle", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_sprint = get_or_create_input_action("IA_Sprint", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_zoom = get_or_create_input_action("IA_CameraZoom", actions_dir, unreal.InputActionValueType.AXIS1D)
    ia_rot_mode = get_or_create_input_action("IA_ToggleRotationMode", actions_dir, unreal.InputActionValueType.BOOLEAN)

    ia_melee = get_or_create_input_action("IA_MeleeAttack", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_kiblast = get_or_create_input_action("IA_KiBlast", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_dodge = get_or_create_input_action("IA_Dodge", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_lockon = get_or_create_input_action("IA_LockOn", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_switch = get_or_create_input_action("IA_SwitchTarget", actions_dir, unreal.InputActionValueType.AXIS1D)
    ia_mob_style = get_or_create_input_action("IA_ToggleMobilityStyle", actions_dir, unreal.InputActionValueType.BOOLEAN)

    imc = editor_asset_lib.load_asset("/Game/Input/IMC_Player")
    unreal.IU_PlayerController.configure_standard_player_mappings(
        imc,
        ia_move,
        ia_look,
        ia_jump,
        ia_sprint,
        ia_zoom,
        ia_rot_mode,
        ia_descend,
        ia_flight_toggle,
        ia_melee,
        ia_kiblast,
        ia_dodge,
        ia_lockon,
        ia_switch,
        ia_mob_style,
    )
    imc.modify()
    editor_asset_lib.save_loaded_asset(imc, only_if_is_dirty=False)

    # Create BP_KiProjectile
    proj_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_KiProjectile")
    bp_proj = get_or_create_blueprint("BP_KiProjectile", "/Game/Combat/Projectiles", proj_cls)

    # Update BP_PlayerCharacter
    bp_char = editor_asset_lib.load_asset("/Game/Characters/Player/BP_PlayerCharacter")
    char_cdo = unreal.get_default_object(bp_char.generated_class())
    combat_comp = char_cdo.get_combat_component()
    if combat_comp:
        combat_comp.set_editor_property("KiProjectileClass", bp_proj.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_char)
    editor_asset_lib.save_loaded_asset(bp_char, only_if_is_dirty=False)

    # Update BP_PlayerController
    bp_pc = editor_asset_lib.load_asset("/Game/Core/BP_PlayerController")
    pc_cdo = unreal.get_default_object(bp_pc.generated_class())
    pc_cdo.set_editor_property("DefaultMappingContext", imc)
    pc_cdo.set_editor_property("MoveAction", ia_move)
    pc_cdo.set_editor_property("LookAction", ia_look)
    pc_cdo.set_editor_property("JumpAction", ia_jump)
    pc_cdo.set_editor_property("DescendAction", ia_descend)
    pc_cdo.set_editor_property("FlightToggleAction", ia_flight_toggle)
    pc_cdo.set_editor_property("SprintAction", ia_sprint)
    pc_cdo.set_editor_property("CameraZoomAction", ia_zoom)
    pc_cdo.set_editor_property("ToggleRotationModeAction", ia_rot_mode)
    pc_cdo.set_editor_property("MeleeAttackAction", ia_melee)
    pc_cdo.set_editor_property("KiBlastAction", ia_kiblast)
    pc_cdo.set_editor_property("DodgeAction", ia_dodge)
    pc_cdo.set_editor_property("LockOnAction", ia_lockon)
    pc_cdo.set_editor_property("SwitchTargetAction", ia_switch)
    pc_cdo.set_editor_property("ToggleMobilityStyleAction", ia_mob_style)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_pc)
    editor_asset_lib.save_loaded_asset(bp_pc, only_if_is_dirty=False)

    # Update BP_GameMode HUDClass -> AIU_HUDManager
    hud_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_HUDManager")
    bp_gm = editor_asset_lib.load_asset("/Game/Core/BP_GameMode")
    gm_cdo = unreal.get_default_object(bp_gm.generated_class())
    gm_cdo.set_editor_property("hud_class", hud_cls)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_gm)
    editor_asset_lib.save_loaded_asset(bp_gm, only_if_is_dirty=False)

    # Place Practice Dummies in M_Prototype (2 Ground + 1 Aerial on 5m platform)
    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    dummy_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_PracticeDummy")

    if level_subsys.load_level("/Game/Maps/Prototype/M_Prototype"):
        for actor in actor_subsys.get_all_level_actors():
            if actor.get_actor_label().startswith("IU_PracticeDummy"):
                actor_subsys.destroy_actor(actor)

        dummies = [
            ("IU_PracticeDummy_Ground_A", unreal.Vector(550.0, -160.0, 95.0), False),
            ("IU_PracticeDummy_Ground_B_Striker", unreal.Vector(620.0, 220.0, 95.0), True),
            ("IU_PracticeDummy_Sky_5m", unreal.Vector(900.0, 0.0, 615.0), False),
        ]
        for label, loc, enable_striker in dummies:
            d = actor_subsys.spawn_actor_from_class(dummy_cls, loc)
            d.set_actor_label(label)
            d.set_editor_property("bEnablePeriodicTestStrike", enable_striker)

        level_subsys.save_current_level()
        unreal.log("=== [IU M3] Placed 3 Combat Practice Dummies in M_Prototype ===")


setup_m3_assets()
