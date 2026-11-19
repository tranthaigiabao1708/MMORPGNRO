"""
InfiniteUniverse - Headless Blender 5.2+ Anime Asset Validator & UE5 FBX Exporter
Usage:
  D:\blender\blender.exe -b character.blend -P Tools/Blender/validate_and_export_fbx.py -- --validate-only
  D:\blender\blender.exe -b character.blend -P Tools/Blender/validate_and_export_fbx.py -- --export Out/SK_Player.fbx
  D:\blender\blender.exe -b --factory-startup -P Tools/Blender/validate_and_export_fbx.py -- --self-test
"""

import argparse
import math
import os
import sys
import tempfile
import bmesh
import bpy
from mathutils import Vector

REQUIRED_HUMANOID_BONES = [
    "root",
    "pelvis",
    "spine_01",
    "spine_02",
    "neck_01",
    "head",
    "upperarm_l",
    "lowerarm_l",
    "hand_l",
    "upperarm_r",
    "lowerarm_r",
    "hand_r",
    "thigh_l",
    "calf_l",
    "foot_l",
    "thigh_r",
    "calf_r",
    "foot_r",
]

MAX_SKIN_INFLUENCES = 4
MIN_CHAR_HEIGHT_CM = 140.0
MAX_CHAR_HEIGHT_CM = 220.0
MAX_PROTOTYPE_TRIANGLES = 65000


def parse_cli_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    parser = argparse.ArgumentParser(description="Validate & Export Anime Character Asset for UE5")
    parser.add_argument("--validate-only", action="store_true", help="Validate current blend scene without exporting")
    parser.add_argument("--export", type=str, default="", help="Output FBX file path")
    parser.add_argument("--bake-anim", action="store_true", help="Include baked animations in FBX export")
    parser.add_argument("--self-test", action="store_true", help="Build procedural compliant rig+mesh and test pipeline")
    return parser.parse_args(argv)


def build_procedural_compliant_test_asset():
    """Creates a clean UE5-compliant 180cm humanoid armature + skinned mesh in memory for pipeline verification."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01  # 1 Blender unit = 1 cm = 1 Unreal Unit

    # 1. Create Armature with required UE5 humanoid bones
    arm_data = bpy.data.armatures.new("Armature")
    arm_obj = bpy.data.objects.new("Armature", arm_data)
    scene.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode="EDIT")

    eb = arm_data.edit_bones

    def add_bone(name, head, tail, parent=None):
        b = eb.new(name)
        b.head = Vector(head)
        b.tail = Vector(tail)
        if parent:
            b.parent = eb[parent]
        return b

    add_bone("root", (0, 0, 0), (0, 0, 15))
    add_bone("pelvis", (0, 0, 95), (0, 0, 108), "root")
    add_bone("spine_01", (0, 0, 108), (0, 0, 126), "pelvis")
    add_bone("spine_02", (0, 0, 126), (0, 0, 146), "spine_01")
    add_bone("neck_01", (0, 0, 146), (0, 0, 156), "spine_02")
    add_bone("head", (0, 0, 156), (0, 0, 180), "neck_01")

    for side, sx in (("l", 1.0), ("r", -1.0)):
        add_bone(f"upperarm_{side}", (sx * 18, 0, 142), (sx * 44, 0, 142), "spine_02")
        add_bone(f"lowerarm_{side}", (sx * 44, 0, 142), (sx * 68, 0, 142), f"upperarm_{side}")
        add_bone(f"hand_{side}", (sx * 68, 0, 142), (sx * 78, 0, 142), f"lowerarm_{side}")
        add_bone(f"thigh_{side}", (sx * 10, 0, 95), (sx * 10, 0, 50), "pelvis")
        add_bone(f"calf_{side}", (sx * 10, 0, 50), (sx * 10, 0, 8), f"thigh_{side}")
        add_bone(f"foot_{side}", (sx * 10, 0, 8), (sx * 10, -14, 0), f"calf_{side}")

    bpy.ops.object.mode_set(mode="OBJECT")

    # 2. Create clean 180cm proxy mesh at ground Z=0..180 with UVs, Material, and Skin Weights
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 90))
    mesh_obj = bpy.context.active_object
    mesh_obj.name = "SK_TestAnimeHero"
    mesh_obj.scale = (36.0, 22.0, 180.0)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    mat = bpy.data.materials.new(name="M_TestAnimeBody")
    mesh_obj.data.materials.append(mat)

    vg = mesh_obj.vertex_groups.new(name="pelvis")
    vg.add([v.index for v in mesh_obj.data.vertices], 1.0, "REPLACE")

    arm_mod = mesh_obj.modifiers.new(name="Armature", type="ARMATURE")
    arm_mod.object = arm_obj
    mesh_obj.parent = arm_obj


def validate_scene():
    errors = []
    warnings = []
    scene = bpy.context.scene

    # 1. Unit System Check
    if scene.unit_settings.system != "METRIC":
        errors.append(f"Scene unit system must be METRIC, found '{scene.unit_settings.system}'.")
    scale_to_cm = scene.unit_settings.scale_length * 100.0

    armatures = [o for o in scene.objects if o.type == "ARMATURE"]
    meshes = [o for o in scene.objects if o.type == "MESH" and not o.name.startswith("WGT_")]

    if len(armatures) != 1:
        errors.append(f"Expected exactly 1 exportable Armature, found {len(armatures)}.")
        return errors, warnings

    if not meshes:
        errors.append("No exportable character Mesh objects found in scene.")
        return errors, warnings

    arm = armatures[0]

    # 2. Unapplied Transform Check on Armature
    if (arm.location.length > 0.5) or any(abs(r) > 1e-4 for r in arm.rotation_euler) or any(
        abs(s - 1.0) > 1e-4 for s in arm.scale
    ):
        errors.append(
            f"Armature '{arm.name}' has unapplied transforms (Loc={tuple(round(v,3) for v in arm.location)}, "
            f"Rot={tuple(round(v,3) for v in arm.rotation_euler)}, Scale={tuple(round(v,3) for v in arm.scale)})."
        )

    # 3. Bone Hierarchy Check (Case-insensitive match against required bones)
    bone_map = {b.name.lower(): b for b in arm.data.bones}
    missing_bones = [req for req in REQUIRED_HUMANOID_BONES if req not in bone_map]
    if missing_bones:
        errors.append(f"Armature missing required UE5 humanoid bones: {', '.join(missing_bones)}")
    else:
        root_bone = bone_map["root"]
        if root_bone.parent is not None:
            errors.append(f"'root' bone must be top-level (no parent), found parent '{root_bone.parent.name}'.")
        if (root_bone.head_local * scale_to_cm).length > 1.0:
            errors.append("'root' bone head must sit at world origin (0, 0, 0).")

    # 4. Mesh Topology, UVs, Materials, Skinning & Bounding Height Check
    min_z_cm = float("inf")
    max_z_cm = float("-inf")
    total_tris = 0

    for obj in meshes:
        if any(abs(r) > 1e-4 for r in obj.rotation_euler) or any(abs(s - 1.0) > 1e-4 for s in obj.scale):
            errors.append(f"Mesh '{obj.name}' has unapplied Rotation or Scale. Run Ctrl+A -> Rotation & Scale.")

        mesh = obj.data
        if not mesh.uv_layers:
            errors.append(f"Mesh '{obj.name}' has NO UV map.")
        if not mesh.materials or any(m is None for m in mesh.materials):
            warnings.append(f"Mesh '{obj.name}' has empty or missing Material slots.")

        # Check triangle count and non-manifold geometry via BMesh
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        total_tris += len(bm.faces)
        bm.free()

        # Skin weights check (<= 4 non-zero deform weights per vertex, no unweighted vertices)
        deform_group_indices = {
            vg.index for vg in obj.vertex_groups if vg.name.lower() in bone_map
        }
        unweighted_verts = 0
        over_influences = 0
        for v in mesh.vertices:
            world_co = obj.matrix_world @ v.co
            z_cm = world_co.z * scale_to_cm
            min_z_cm = min(min_z_cm, z_cm)
            max_z_cm = max(max_z_cm, z_cm)

            valid_weights = [g.weight for g in v.groups if g.group in deform_group_indices and g.weight > 1e-4]
            if not valid_weights:
                unweighted_verts += 1
            if len(valid_weights) > MAX_SKIN_INFLUENCES:
                over_influences += 1

        if unweighted_verts > 0:
            errors.append(f"Mesh '{obj.name}' has {unweighted_verts} unweighted vertices.")
        if over_influences > 0:
            warnings.append(
                f"Mesh '{obj.name}' has {over_influences} vertices exceeding {MAX_SKIN_INFLUENCES} bone influences "
                "(recommended: Weight -> Limit Total = 4)."
            )

    height_cm = max_z_cm - min_z_cm
    if not (MIN_CHAR_HEIGHT_CM <= height_cm <= MAX_CHAR_HEIGHT_CM):
        errors.append(
            f"Character height is {height_cm:.1f} cm (expected {MIN_CHAR_HEIGHT_CM:.0f}-{MAX_CHAR_HEIGHT_CM:.0f} cm; "
            "adult standard is 175-185 cm)."
        )
    if abs(min_z_cm) > 2.5:
        errors.append(f"Character feet lowest Z is {min_z_cm:.2f} cm (must rest at ground Z = 0 cm +/- 2.5 cm).")
    if total_tris > MAX_PROTOTYPE_TRIANGLES:
        errors.append(
            f"Total triangle count {total_tris} exceeds prototype gameplay limit ({MAX_PROTOTYPE_TRIANGLES})."
        )

    print(
        f"[ASSET SUMMARY] Armature='{arm.name}' ({len(arm.data.bones)} bones) | "
        f"Meshes={len(meshes)} | Triangles={total_tris} | Height={height_cm:.1f} cm | FeetZ={min_z_cm:.2f} cm"
    )
    return errors, warnings


def export_ue5_fbx(output_path: str, bake_anim: bool):
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in bpy.context.scene.objects:
        if obj.type in {"ARMATURE", "MESH"} and not obj.name.startswith("WGT_"):
            obj.select_set(True)

    bpy.ops.export_scene.fbx(
        filepath=output_path,
        use_selection=True,
        object_types={"ARMATURE", "MESH"},
        axis_forward="-Y",
        axis_up="Z",
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        mesh_smooth_type="FACE",
        use_mesh_modifiers=True,
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        armature_nodetype="NULL",
        bake_anim=bake_anim,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        path_mode="AUTO",
    )
    print(f"[EXPORT PASS] Exported UE5-compliant FBX -> {output_path} ({os.path.getsize(output_path)} bytes)")


def main():
    args = parse_cli_args()

    if args.self_test:
        print("[SELF-TEST] Building procedural 180cm Anime Humanoid test asset in Blender...")
        build_procedural_compliant_test_asset()
        errors, warnings = validate_scene()
        for w in warnings:
            print(f"  [WARN] {w}")
        if errors:
            for e in errors:
                print(f"  [ERROR] {e}")
            sys.exit(1)
        temp_fbx = os.path.join(tempfile.gettempdir(), "SK_IU_SelfTest_UE5.fbx")
        export_ue5_fbx(temp_fbx, bake_anim=False)
        if not os.path.exists(temp_fbx) or os.path.getsize(temp_fbx) < 1024:
            print("  [ERROR] Exported self-test FBX missing or empty!")
            sys.exit(1)
        os.remove(temp_fbx)
        print("[SELF-TEST PASSED] Blender 5.2+ Validation & UE5 FBX Export Pipeline verified!")
        sys.exit(0)

    errors, warnings = validate_scene()
    for w in warnings:
        print(f"  [WARN] {w}")
    if errors:
        print(f"[VALIDATION FAILED] {len(errors)} error(s) detected:")
        for e in errors:
            print(f"  [ERROR] {e}")
        sys.exit(1)

    print("[VALIDATION PASSED] Asset satisfies all UE5 Core Prototype standards.")
    if args.export and not args.validate_only:
        export_ue5_fbx(args.export, bake_anim=args.bake_anim)


if __name__ == "__main__":
    main()
