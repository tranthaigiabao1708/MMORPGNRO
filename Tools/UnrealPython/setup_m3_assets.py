"""
Milestone 3 Asset, HUD & Combat Target Setup Script
- Creates IA_MeleeAttack (LMB), IA_KiBlast (Q), IA_Dodge (RMB - Contextual Dash / Short Blink / Combat Evade), IA_LockOn (MiddleMouse / C), IA_SwitchTarget (E)
- Updates IMC_Player, BP_PlayerController, BP_PlayerCharacter, BP_GameMode, and creates WBP_PlayerHUD + BP_KiProjectile
- Places 2 Ground Practice Dummies and 1 Sky Platform Practice Dummy in M_Prototype for immediate combat testing
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


def setup_m3_assets():
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
            ("IU_PracticeDummy_Ground_A", unreal.Vector(550.0, -160.0, 95.0)),
            ("IU_PracticeDummy_Ground_B", unreal.Vector(620.0, 220.0, 95.0)),
            ("IU_PracticeDummy_Sky_5m", unreal.Vector(900.0, 0.0, 615.0)),
        ]
        for label, loc in dummies:
            d = actor_subsys.spawn_actor_from_class(dummy_cls, loc)
            d.set_actor_label(label)

        level_subsys.save_current_level()
        unreal.log("=== [IU M3] Placed 3 Combat Practice Dummies in M_Prototype ===")


setup_m3_assets()
