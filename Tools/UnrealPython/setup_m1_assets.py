"""
Milestone 1 Asset & Map Setup Script
- Uses C++ AIU_PlayerController.ConfigureStandardPlayerMappings to guarantee exact W/A/S/D & Mouse2D modifiers
- Configures BP_PlayerCharacter, BP_PlayerController, BP_GameMode
- Cleans clutter on M_Prototype and widens floor to 60m x 60m for comfortable camera & movement testing
"""
import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary


def get_or_create_input_action(name: str, folder: str, value_type: unreal.InputActionValueType) -> unreal.InputAction:
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        ia = editor_asset_lib.load_asset(full_path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.InputAction)
        ia = asset_tools.create_asset(name, folder, unreal.InputAction, factory)
    ia.set_editor_property("value_type", value_type)
    editor_asset_lib.save_loaded_asset(ia)
    return ia


def setup_enhanced_input_assets():
    actions_dir = "/Game/Input/Actions"
    ia_move = get_or_create_input_action("IA_Move", actions_dir, unreal.InputActionValueType.AXIS2D)
    ia_look = get_or_create_input_action("IA_Look", actions_dir, unreal.InputActionValueType.AXIS2D)
    ia_jump = get_or_create_input_action("IA_Jump", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_sprint = get_or_create_input_action("IA_Sprint", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_zoom = get_or_create_input_action("IA_CameraZoom", actions_dir, unreal.InputActionValueType.AXIS1D)
    ia_rot_mode = get_or_create_input_action("IA_ToggleRotationMode", actions_dir, unreal.InputActionValueType.BOOLEAN)

    imc_path = "/Game/Input/IMC_Player"
    if editor_asset_lib.does_asset_exist(imc_path):
        imc = editor_asset_lib.load_asset(imc_path)
    else:
        imc_factory = unreal.DataAssetFactory()
        imc_factory.set_editor_property("data_asset_class", unreal.InputMappingContext)
        imc = asset_tools.create_asset("IMC_Player", "/Game/Input", unreal.InputMappingContext, imc_factory)

    # Populate mappings & modifiers directly in C++ (avoids Python UStruct copy loss!)
    unreal.IU_PlayerController.configure_standard_player_mappings(
        imc, ia_move, ia_look, ia_jump, ia_sprint, ia_zoom, ia_rot_mode
    )
    imc.modify()
    editor_asset_lib.save_loaded_asset(imc, only_if_is_dirty=False)
    unreal.log("=== [IU M1] Saved IMC_Player with C++ verified axis modifiers ===")
    return imc, ia_move, ia_look, ia_jump, ia_sprint, ia_zoom, ia_rot_mode


def get_or_create_blueprint(name: str, folder: str, parent_class: unreal.Class) -> unreal.Blueprint:
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        bp = editor_asset_lib.load_asset(full_path)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        editor_asset_lib.save_loaded_asset(bp, only_if_is_dirty=False)
        return bp
    bp_factory = unreal.BlueprintFactory()
    bp_factory.set_editor_property("parent_class", parent_class)
    bp = asset_tools.create_asset(name, folder, unreal.Blueprint, bp_factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    editor_asset_lib.save_loaded_asset(bp, only_if_is_dirty=False)
    return bp


def setup_core_blueprints(imc, ia_move, ia_look, ia_jump, ia_sprint, ia_zoom, ia_rot_mode):
    char_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_PlayerCharacter")
    pc_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_PlayerController")
    gm_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_GameMode")

    bp_char = get_or_create_blueprint("BP_PlayerCharacter", "/Game/Characters/Player", char_cls)
    bp_pc = get_or_create_blueprint("BP_PlayerController", "/Game/Core", pc_cls)
    bp_gm = get_or_create_blueprint("BP_GameMode", "/Game/Core", gm_cls)

    # Sync BP_PlayerCharacter CDO camera pivot defaults (character centered Y=0)
    char_cdo = unreal.get_default_object(bp_char.generated_class())
    char_cdo.set_editor_property("CameraTargetOffset", unreal.Vector(0.0, 0.0, 55.0))
    char_cdo.set_editor_property("CameraSocketOffset", unreal.Vector(0.0, 0.0, 20.0))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_char)
    editor_asset_lib.save_loaded_asset(bp_char, only_if_is_dirty=False)

    pc_cdo = unreal.get_default_object(bp_pc.generated_class())
    pc_cdo.set_editor_property("DefaultMappingContext", imc)
    pc_cdo.set_editor_property("MoveAction", ia_move)
    pc_cdo.set_editor_property("LookAction", ia_look)
    pc_cdo.set_editor_property("JumpAction", ia_jump)
    pc_cdo.set_editor_property("SprintAction", ia_sprint)
    pc_cdo.set_editor_property("CameraZoomAction", ia_zoom)
    pc_cdo.set_editor_property("ToggleRotationModeAction", ia_rot_mode)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_pc)
    editor_asset_lib.save_loaded_asset(bp_pc, only_if_is_dirty=False)

    gm_cdo = unreal.get_default_object(bp_gm.generated_class())
    gm_cdo.set_editor_property("default_pawn_class", bp_char.generated_class())
    gm_cdo.set_editor_property("player_controller_class", bp_pc.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_gm)
    editor_asset_lib.save_loaded_asset(bp_gm, only_if_is_dirty=False)


def tidy_prototype_map():
    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if level_subsys.load_level("/Game/Maps/Prototype/M_Prototype"):
        for actor in actor_subsys.get_all_level_actors():
            label = actor.get_actor_label()
            if "MatPreview" in label or isinstance(actor, unreal.TextRenderActor):
                actor_subsys.destroy_actor(actor)
            elif "Floor" in label and isinstance(actor, unreal.StaticMeshActor):
                actor.set_actor_scale3d(unreal.Vector(6.0, 6.0, 1.0))
        level_subsys.save_current_level()
        unreal.log("=== [IU M1] Tidied M_Prototype floor (60m x 60m) ===")


inputs = setup_enhanced_input_assets()
setup_core_blueprints(*inputs)
tidy_prototype_map()
