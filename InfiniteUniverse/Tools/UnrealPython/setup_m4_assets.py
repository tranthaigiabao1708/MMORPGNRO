"""
Milestone 4 Enemy AI Asset & Prototype Arena Setup Script
- Creates /Game/Characters/Enemy/Materials/M_EnemyCrimson stylized crimson material for Enemy AI
- Creates /Game/Characters/Enemy/BP_EnemyCharacter (parent: AIU_EnemyCharacter) with crimson material applied
- Creates /Game/AI/BP_EnemySpawner (parent: AIU_EnemySpawner) defaulting to BP_EnemyCharacter
- Places 2 live Enemy AI Spawners in /Game/Maps/Prototype/M_Prototype:
    1) IU_EnemySpawner_Ground_West (-1450, 0, 145) -> Ground Martial Raider
    2) IU_EnemySpawner_Aerial_Sky (-1650, -650, 550) -> 3D Aerial Flyer Raider
"""
import unreal

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_asset_lib = unreal.EditorAssetLibrary
mat_lib = unreal.MaterialEditingLibrary


def create_or_update_enemy_crimson_material():
    mat_folder = "/Game/Characters/Enemy/Materials"
    mat_name = "M_EnemyCrimson"
    full_path = f"{mat_folder}/{mat_name}"

    if editor_asset_lib.does_asset_exist(full_path):
        mat = editor_asset_lib.load_asset(full_path)
        mat_lib.delete_all_material_expressions(mat)
    else:
        mat = asset_tools.create_asset(mat_name, mat_folder, unreal.Material, unreal.MaterialFactoryNew())

    base_col = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -340, -120)
    base_col.set_editor_property("parameter_name", "EnemyBaseColor")
    base_col.set_editor_property("default_value", unreal.LinearColor(0.68, 0.08, 0.12, 1.0))
    mat_lib.connect_material_property(base_col, "", unreal.MaterialProperty.MP_BASE_COLOR)

    emissive_col = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -340, 80)
    emissive_col.set_editor_property("parameter_name", "EnemyRimGlow")
    emissive_col.set_editor_property("default_value", unreal.LinearColor(0.22, 0.02, 0.03, 1.0))
    mat_lib.connect_material_property(emissive_col, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    roughness = mat_lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -220, 240)
    roughness.set_editor_property("r", 0.42)
    mat_lib.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

    mat_lib.recompile_material(mat)
    editor_asset_lib.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def get_or_create_blueprint(name: str, folder: str, parent_class: unreal.Class):
    full_path = f"{folder}/{name}"
    if editor_asset_lib.does_asset_exist(full_path):
        bp = editor_asset_lib.load_asset(full_path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        bp = asset_tools.create_asset(name, folder, None, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    editor_asset_lib.save_loaded_asset(bp, only_if_is_dirty=False)
    return bp


def setup_m4_assets():
    crimson_mat = create_or_update_enemy_crimson_material()

    enemy_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_EnemyCharacter")
    spawner_cls = unreal.load_class(None, "/Script/InfiniteUniverse.IU_EnemySpawner")

    bp_enemy = get_or_create_blueprint("BP_EnemyCharacter", "/Game/Characters/Enemy", enemy_cls)
    enemy_cdo = unreal.get_default_object(bp_enemy.generated_class())
    body_mesh = enemy_cdo.get_editor_property("PlaceholderBodyMesh")
    head_mesh = enemy_cdo.get_editor_property("PlaceholderHeadMesh")
    if body_mesh and crimson_mat:
        body_mesh.set_material(0, crimson_mat)
    if head_mesh and crimson_mat:
        head_mesh.set_material(0, crimson_mat)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_enemy)
    editor_asset_lib.save_loaded_asset(bp_enemy, only_if_is_dirty=False)

    bp_spawner = get_or_create_blueprint("BP_EnemySpawner", "/Game/AI", spawner_cls)
    spawner_cdo = unreal.get_default_object(bp_spawner.generated_class())
    spawner_cdo.set_editor_property("EnemyClass", bp_enemy.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(bp_spawner)
    editor_asset_lib.save_loaded_asset(bp_spawner, only_if_is_dirty=False)

    level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if level_subsys.load_level("/Game/Maps/Prototype/M_Prototype"):
        for actor in actor_subsys.get_all_level_actors():
            if actor.get_actor_label().startswith("IU_EnemySpawner"):
                actor_subsys.destroy_actor(actor)

        spawners = [
            ("IU_EnemySpawner_Ground_West", unreal.Vector(-1450.0, 0.0, 145.0), False),
            ("IU_EnemySpawner_Aerial_Sky", unreal.Vector(-1650.0, -650.0, 550.0), True),
        ]
        for label, loc, is_aerial in spawners:
            sp = actor_subsys.spawn_actor_from_class(bp_spawner.generated_class(), loc)
            sp.set_actor_label(label)
            sp.set_editor_property("bSpawnAerialFlyer", is_aerial)
            sp.set_editor_property("RespawnDelaySeconds", 4.0)

        level_subsys.save_current_level()
        unreal.log("=== [IU M4] Created BP_EnemyCharacter, BP_EnemySpawner, and placed 2 Enemy Spawners in M_Prototype ===")


setup_m4_assets()
