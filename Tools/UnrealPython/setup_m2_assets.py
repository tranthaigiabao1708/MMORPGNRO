"""
Milestone 2 Asset & Sky Playground Setup Script
- Adds IA_Descend (LeftCtrl) and IA_FlightToggle (F) to IMC_Player and BP_PlayerController
- Recompiles BP_PlayerCharacter with FlightComponent defaults
- Adds 3 elevated floating sky platforms & vertical pillars in M_Prototype for 3D Flight & Landing tests
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


def setup_m2_inputs_and_blueprints():
    actions_dir = "/Game/Input/Actions"
    ia_move = get_or_create_input_action("IA_Move", actions_dir, unreal.InputActionValueType.AXIS2D)
    ia_look = get_or_create_input_action("IA_Look", actions_dir, unreal.InputActionValueType.AXIS2D)
    ia_jump = get_or_create_input_action("IA_Jump", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_descend = get_or_create_input_action("IA_Descend", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_flight_toggle = get_or_create_input_action("IA_FlightToggle", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_sprint = get_or_create_input_action("IA_Sprint", actions_dir, unreal.InputActionValueType.BOOLEAN)
    ia_zoom = get_or_create_input_action("IA_CameraZoom", actions_dir, unreal.InputActionValueType.AXIS1D)
    ia_rot_mode = get_or_create_input_action("IA_ToggleRotationMode", actions_dir, unreal.InputActionValueType.BOOLEAN)

    imc = editor_asset_lib.load_asset("/Game/Input/IMC_Player")
    unreal.IU_PlayerController.configure_standard_player_mappings(
        imc, ia_move, ia_look, ia_jump, ia_sprint, ia_zoom, ia_rot_mode, ia_descend, ia_flight_toggle
    )
    imc.modify()
    editor_asset_lib.save_loaded_asset(imc, only_if_is_dirty=False)

    bp_char = editor_asset_lib.load_asset("/Game/Characters/Player/BP_PlayerCharacter")
    char_cdo = unreal.get_default_object(bp_char.generated_class())
    flight_comp = char_cdo.get_flight_component()
    if flight_comp:
        flight_comp.set_editor_property("FlightSpeed", 1575.0)
        flight_comp.set_editor_property("VerticalFlightSpeed", 1275.0)
        flight_comp.set_editor_property("FlightDashMultiplier", 2.0)
        flight_comp.set_editor_property("InitialSlowAscentDuration", 1.0)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_char)
    editor_asset_lib.save_loaded_asset(bp_char, only_if_is_dirty=False)

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
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_pc)
    editor_asset_lib.save_loaded_asset(bp_pc, only_if_is_dirty=False)
    unreal.log("=== [IU M2] Updated IMC_Player, IA_Descend, IA_FlightToggle, and Blueprints ===")


def setup_flight_test_landmarks():
    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not level_subsys.load_level("/Game/Maps/Prototype/M_Prototype"):
        return

    # Remove any old M2 landmarks first so re-running is idempotent
    for actor in actor_subsys.get_all_level_actors():
        if actor.get_actor_label().startswith("IU_Sky"):
            actor_subsys.destroy_actor(actor)

    cube_mesh = editor_asset_lib.load_asset("/Engine/BasicShapes/Cube")
    cyl_mesh = editor_asset_lib.load_asset("/Engine/BasicShapes/Cylinder")

    platforms = [
        ("IU_SkyPlatform_Low_5m", unreal.Vector(900.0, 0.0, 500.0), unreal.Vector(6.0, 6.0, 0.4)),
        ("IU_SkyPlatform_Mid_14m", unreal.Vector(1800.0, 700.0, 1400.0), unreal.Vector(7.0, 7.0, 0.4)),
        ("IU_SkyPlatform_High_26m", unreal.Vector(600.0, -1200.0, 2600.0), unreal.Vector(8.0, 8.0, 0.5)),
    ]
    for label, loc, scale in platforms:
        plat = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, loc)
        plat.set_actor_label(label)
        smc = plat.get_editor_property("static_mesh_component")
        smc.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        smc.set_static_mesh(cube_mesh)
        plat.set_actor_scale3d(scale)

    pillars = [
        ("IU_SkyPillar_East", unreal.Vector(1400.0, -700.0, 900.0), unreal.Vector(1.8, 1.8, 18.0)),
        ("IU_SkyPillar_West", unreal.Vector(-1200.0, 800.0, 1200.0), unreal.Vector(2.2, 2.2, 24.0)),
    ]
    for label, loc, scale in pillars:
        pil = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, loc)
        pil.set_actor_label(label)
        smc = pil.get_editor_property("static_mesh_component")
        smc.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        smc.set_static_mesh(cyl_mesh)
        pil.set_actor_scale3d(scale)

    level_subsys.save_current_level()
    unreal.log("=== [IU M2] Added 3 Sky Platforms & 2 Vertical Pillars to M_Prototype ===")


setup_m2_inputs_and_blueprints()
setup_flight_test_landmarks()
