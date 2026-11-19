"""
Creates /Game/Maps/Prototype/M_Prototype from Epic's lightweight TimeOfDay_Default template,
ensuring a PlayerStart exists and saving it cleanly via Unreal's native serializer.
"""
import unreal

TARGET_MAP_PACKAGE = "/Game/Maps/Prototype/M_Prototype"
TEMPLATE_MAP_PATH = "/Engine/Maps/Templates/TimeOfDay_Default"

unreal.log("=== [IU M0] Creating Prototype Startup Map: " + TARGET_MAP_PACKAGE + " ===")

level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

loaded = level_subsys.new_level_from_template(TARGET_MAP_PACKAGE, TEMPLATE_MAP_PATH)
if not loaded:
    raise RuntimeError("Failed to create new level from template: " + TEMPLATE_MAP_PATH)

# Ensure at least one PlayerStart exists above the floor
existing_starts = [a for a in actor_subsys.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
if not existing_starts:
    ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 120.0))
    ps.set_actor_label("PlayerStart_Prototype")
    unreal.log("Spawned PlayerStart_Prototype at (0, 0, 120)")
else:
    unreal.log(f"Found {len(existing_starts)} existing PlayerStart actor(s).")

saved = level_subsys.save_current_level()
if not saved:
    raise RuntimeError("Failed to save level: " + TARGET_MAP_PACKAGE)

unreal.log("=== [IU M0] Successfully saved " + TARGET_MAP_PACKAGE + " ===")
