"""
Milestone 1 Asset Setup Script (Enhanced Input Actions, IMC_Player, and Core Blueprints)
Run via UnrealEditor-Cmd.exe -run=pythonscript -script=...
"""
import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary


def make_key(key_name: str) -> unreal.Key:
    k = unreal.Key()
    k.import_text(key_name)
    return k


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
        imc.unmap_all()
    else:
        imc_factory = unreal.DataAssetFactory()
        imc_factory.set_editor_property("data_asset_class", unreal.InputMappingContext)
        imc = asset_tools.create_asset("IMC_Player", "/Game/Input", unreal.InputMappingContext, imc_factory)

    # W (+Y Forward): Swizzle YXZ
    m_w = imc.map_key(ia_move, make_key("W"))
    swiz_w = unreal.InputModifierSwizzleAxis(imc)
    swiz_w.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
    m_w.set_editor_property("modifiers", [swiz_w])

    # S (-Y Backward): Swizzle YXZ + Negate
    m_s = imc.map_key(ia_move, make_key("S"))
    swiz_s = unreal.InputModifierSwizzleAxis(imc)
    swiz_s.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
    neg_s = unreal.InputModifierNegate(imc)
    m_s.set_editor_property("modifiers", [swiz_s, neg_s])

    # D (+X Right)
    imc.map_key(ia_move, make_key("D"))

    # A (-X Left): Negate
    m_a = imc.map_key(ia_move, make_key("A"))
    neg_a = unreal.InputModifierNegate(imc)
    m_a.set_editor_property("modifiers", [neg_a])

    # MouseX (+X Yaw) & MouseY (-Y Pitch via Swizzle YXZ + Negate)
    imc.map_key(ia_look, make_key("MouseX"))
    m_look_y = imc.map_key(ia_look, make_key("MouseY"))
    swiz_look_y = unreal.InputModifierSwizzleAxis(imc)
    swiz_look_y.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
    neg_look_y = unreal.InputModifierNegate(imc)
    m_look_y.set_editor_property("modifiers", [swiz_look_y, neg_look_y])

    # SpaceBar -> Jump, LeftShift -> Sprint, MouseWheelAxis -> Zoom, Tab -> Toggle Rotation Mode
    imc.map_key(ia_jump, make_key("SpaceBar"))
    imc.map_key(ia_sprint, make_key("LeftShift"))
    imc.map_key(ia_zoom, make_key("MouseWheelAxis"))
    imc.map_key(ia_rot_mode, make_key("Tab"))

    editor_asset_lib.save_loaded_asset(imc)
    unreal.log("=== [IU M1] Saved IMC_Player and 6 InputActions ===")
    return imc, ia_move, ia_look, ia_jump, ia_sprint, ia_zoom, ia_rot_mode


def get_or_create_blueprint(name: str, folder: str, parent_class: unreal.Class) -> unreal.Blueprint:
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        return editor_asset_lib.load_asset(full_path)
    bp_factory = unreal.BlueprintFactory()
    bp_factory.set_editor_property("parent_class", parent_class)
    bp = asset_tools.create_asset(name, folder, unreal.Blueprint, bp_factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    editor_asset_lib.save_loaded_asset(bp)
    return bp


def setup_core_blueprints(imc, ia_move, ia_look, ia_jump, ia_sprint, ia_zoom, ia_rot_mode):
    char_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_PlayerCharacter")
    pc_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_PlayerController")
    gm_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_GameMode")

    bp_char = get_or_create_blueprint("BP_PlayerCharacter", "/Game/Characters/Player", char_cls)
    bp_pc = get_or_create_blueprint("BP_PlayerController", "/Game/Core", pc_cls)
    bp_gm = get_or_create_blueprint("BP_GameMode", "/Game/Core", gm_cls)

    # Configure BP_PlayerController default properties with Enhanced Input assets
    pc_cdo = unreal.get_default_object(bp_pc.generated_class())
    pc_cdo.set_editor_property("DefaultMappingContext", imc)
    pc_cdo.set_editor_property("MoveAction", ia_move)
    pc_cdo.set_editor_property("LookAction", ia_look)
    pc_cdo.set_editor_property("JumpAction", ia_jump)
    pc_cdo.set_editor_property("SprintAction", ia_sprint)
    pc_cdo.set_editor_property("CameraZoomAction", ia_zoom)
    pc_cdo.set_editor_property("ToggleRotationModeAction", ia_rot_mode)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_pc)
    editor_asset_lib.save_loaded_asset(bp_pc)

    # Configure BP_GameMode to spawn BP_PlayerCharacter and BP_PlayerController
    gm_cdo = unreal.get_default_object(bp_gm.generated_class())
    gm_cdo.set_editor_property("default_pawn_class", bp_char.generated_class())
    gm_cdo.set_editor_property("player_controller_class", bp_pc.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_gm)
    editor_asset_lib.save_loaded_asset(bp_gm)

    unreal.log("=== [IU M1] Saved BP_PlayerCharacter, BP_PlayerController, and BP_GameMode ===")


inputs = setup_enhanced_input_assets()
setup_core_blueprints(*inputs)
