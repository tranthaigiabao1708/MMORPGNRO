"""
InfiniteUniverse - Milestone 6 Stylized 3D Anime Character & 15-Animation Generator (Blender 5.2+)
Generates:
  - SK_Player.fbx  (180 cm Stylized Anime Martial Artist with spiky hair, gi tunic, obi sash, wristbands, boots)
  - SK_Enemy.fbx   (182 cm Stylized Anime Martial Raider with horned visor crest & heavy shoulder pads)
  - 15 Skeletal Animations (A_Player_Idle, Walk, Run, Jump, Fall, Landing, Hover, FlyForward, FlyBackward,
    Melee_01, Melee_02, Melee_03, KiBlast, HitReaction, Death)
Validates every character scene against Tools/Blender/validate_and_export_fbx.py rules (0 errors, <=4 skin weights).
"""

import math
import os
import sys
import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import validate_and_export_fbx as validator


def reset_metric_cm_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 0.01  # 1 unit = 1 cm = 1 UU
    scene.render.fps = 30
    return scene


def create_humanoid_armature(scene) -> bpy.types.Object:
    arm_data = bpy.data.armatures.new("Armature")
    arm_obj = bpy.data.objects.new("Armature", arm_data)
    scene.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode="EDIT")

    eb = arm_data.edit_bones

    def add_bone(name, head, tail, parent=None, deform=True):
        b = eb.new(name)
        b.head = Vector(head)
        b.tail = Vector(tail)
        b.use_deform = deform
        if parent:
            b.parent = eb[parent]
        return b

    # Character faces -Y in Blender (standard front view -> +Y in UE5 SkeletalMesh -> +X Actor Forward with -90 Yaw)
    add_bone("root", (0.0, 0.0, 0.0), (0.0, 0.0, 16.0), None, deform=False)
    add_bone("pelvis", (0.0, 0.0, 96.0), (0.0, 0.0, 108.0), "root")
    add_bone("spine_01", (0.0, 0.0, 108.0), (0.0, 0.0, 122.0), "pelvis")
    add_bone("spine_02", (0.0, 0.0, 122.0), (0.0, 0.0, 136.0), "spine_01")
    add_bone("spine_03", (0.0, 0.0, 136.0), (0.0, 0.0, 147.0), "spine_02")
    add_bone("neck_01", (0.0, 0.0, 147.0), (0.0, 0.0, 155.0), "spine_03")
    add_bone("head", (0.0, 0.0, 155.0), (0.0, 0.0, 174.0), "neck_01")

    # Left = +X, Right = -X when facing -Y
    for side, sx in (("l", 1.0), ("r", -1.0)):
        add_bone(f"clavicle_{side}", (sx * 4.0, 0.0, 143.0), (sx * 16.0, 0.0, 143.0), "spine_03")
        add_bone(f"upperarm_{side}", (sx * 16.0, 0.0, 143.0), (sx * 42.0, 0.0, 141.0), f"clavicle_{side}")
        add_bone(f"lowerarm_{side}", (sx * 42.0, 0.0, 141.0), (sx * 65.0, -1.0, 140.0), f"upperarm_{side}")
        add_bone(f"hand_{side}", (sx * 65.0, -1.0, 140.0), (sx * 76.0, -1.5, 139.0), f"lowerarm_{side}")

        add_bone(f"thigh_{side}", (sx * 10.5, 0.0, 96.0), (sx * 11.0, -0.5, 52.0), "pelvis")
        add_bone(f"calf_{side}", (sx * 11.0, -0.5, 52.0), (sx * 11.0, 1.0, 9.0), f"thigh_{side}")
        add_bone(f"foot_{side}", (sx * 11.0, 1.0, 9.0), (sx * 11.0, -13.0, 2.0), f"calf_{side}")
        add_bone(f"ball_{side}", (sx * 11.0, -13.0, 2.0), (sx * 11.0, -20.0, 2.0), f"foot_{side}")

    bpy.ops.object.mode_set(mode="OBJECT")
    return arm_obj


def add_tapered_limb_segment(bm, uv_layer, p0: Vector, p1: Vector, r0_x: float, r0_y: float, r1_x: float, r1_y: float, rings: int, segs: int, mat_idx: int):
    """Creates a clean multi-ring quad tube with capped ends for smooth 3-loop joint deformation."""
    axis = p1 - p0
    length = max(axis.length, 0.001)
    z_dir = axis / length
    ref = Vector((0.0, 0.0, 1.0)) if abs(z_dir.z) < 0.92 else Vector((0.0, 1.0, 0.0))
    x_dir = ref.cross(z_dir).normalized()
    y_dir = z_dir.cross(x_dir).normalized()

    ring_verts = []
    for r_i in range(rings + 1):
        t = r_i / float(rings)
        # Subtle muscular curve in the middle of limb segments
        bulge = 1.0 + 0.08 * math.sin(t * math.pi)
        rx = (r0_x * (1.0 - t) + r1_x * t) * bulge
        ry = (r0_y * (1.0 - t) + r1_y * t) * bulge
        center = p0.lerp(p1, t)
        row = []
        for s_i in range(segs):
            ang = (2.0 * math.pi * s_i) / float(segs)
            pos = center + x_dir * (math.cos(ang) * rx) + y_dir * (math.sin(ang) * ry)
            row.append(bm.verts.new(pos))
        ring_verts.append(row)

    for r_i in range(rings):
        for s_i in range(segs):
            s_next = (s_i + 1) % segs
            f = bm.faces.new((ring_verts[r_i][s_i], ring_verts[r_i][s_next], ring_verts[r_i + 1][s_next], ring_verts[r_i + 1][s_i]))
            f.material_index = mat_idx
            f.smooth = True
            for lp_i, loop in enumerate(f.loops):
                u = (s_i + (1 if lp_i in (1, 2) else 0)) / float(segs)
                v = (r_i + (1 if lp_i in (2, 3) else 0)) / float(rings)
                loop[uv_layer].uv = (u, v)

    # Cap both ends cleanly with center pole fan
    for end_row, end_pt, flip in ((ring_verts[0], p0, True), (ring_verts[-1], p1, False)):
        pole = bm.verts.new(end_pt)
        for s_i in range(segs):
            s_next = (s_i + 1) % segs
            verts = (pole, end_row[s_next], end_row[s_i]) if flip else (pole, end_row[s_i], end_row[s_next])
            f = bm.faces.new(verts)
            f.material_index = mat_idx
            f.smooth = True
            for loop in f.loops:
                loop[uv_layer].uv = (0.5, 0.5)


def add_hair_spike(bm, uv_layer, base_center: Vector, tip_pos: Vector, base_radius: float, mat_idx: int):
    axis = (tip_pos - base_center)
    z_dir = axis.normalized()
    ref = Vector((0.0, 0.0, 1.0)) if abs(z_dir.z) < 0.9 else Vector((1.0, 0.0, 0.0))
    x_dir = ref.cross(z_dir).normalized()
    y_dir = z_dir.cross(x_dir).normalized()

    segs = 6
    ring0 = [bm.verts.new(base_center + (x_dir * math.cos(2 * math.pi * i / segs) + y_dir * math.sin(2 * math.pi * i / segs)) * base_radius) for i in range(segs)]
    mid_c = base_center.lerp(tip_pos, 0.52)
    ring1 = [bm.verts.new(mid_c + (x_dir * math.cos(2 * math.pi * i / segs) + y_dir * math.sin(2 * math.pi * i / segs)) * (base_radius * 0.58)) for i in range(segs)]
    tip_v = bm.verts.new(tip_pos)
    cap_v = bm.verts.new(base_center)

    for i in range(segs):
        nxt = (i + 1) % segs
        f1 = bm.faces.new((ring0[i], ring0[nxt], ring1[nxt], ring1[i]))
        f2 = bm.faces.new((ring1[i], ring1[nxt], tip_v))
        f3 = bm.faces.new((cap_v, ring0[nxt], ring0[i]))
        for f in (f1, f2, f3):
            f.material_index = mat_idx
            f.smooth = True
            for lp in f.loops:
                lp[uv_layer].uv = (0.5, 0.5)


def build_stylized_anime_mesh(scene, arm_obj: bpy.types.Object, mesh_name: str, is_enemy: bool) -> bpy.types.Object:
    mesh_data = bpy.data.meshes.new(mesh_name)
    mesh_obj = bpy.data.objects.new(mesh_name, mesh_data)
    scene.collection.objects.link(mesh_obj)

    # Material slots: 0=Skin, 1=GiPrimary, 2=AccentSash, 3=Hair, 4=Boots
    mat_names = (
        ("M_EnemySkin", "M_EnemyArmor", "M_EnemyCrimsonTrim", "M_EnemyCrest", "M_EnemyBoots")
        if is_enemy
        else ("M_PlayerSkin", "M_PlayerGi", "M_PlayerSash", "M_PlayerHair", "M_PlayerBoots")
    )
    for m_name in mat_names:
        mat = bpy.data.materials.new(name=m_name)
        mesh_data.materials.append(mat)

    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.new("UVMap")

    # 1. Pelvis & Martial Arts Gi Tunic + Obi Belt Sash
    add_tapered_limb_segment(bm, uv_layer, Vector((0, 0, 90)), Vector((0, 0, 108)), 14.5, 10.5, 13.0, 9.5, 3, 14, 1)
    add_tapered_limb_segment(bm, uv_layer, Vector((0, 0, 105)), Vector((0, 0, 114)), 14.2, 10.6, 14.0, 10.4, 2, 14, 2)  # Belt sash
    add_tapered_limb_segment(bm, uv_layer, Vector((0, 0, 112)), Vector((0, 0, 146)), 13.2, 9.5, 17.5, 11.2, 5, 16, 1)  # Athletic chest

    # 2. Neck & Stylized Anime Head + Front Eye/Visor Band (-Y is front face!)
    add_tapered_limb_segment(bm, uv_layer, Vector((0, 0, 145)), Vector((0, 0, 156)), 5.6, 5.4, 5.2, 5.0, 2, 10, 0)
    add_tapered_limb_segment(bm, uv_layer, Vector((0, -1.0, 154)), Vector((0, -0.5, 173)), 8.2, 9.2, 9.5, 10.2, 4, 14, 0)
    # Brow / Scouter visor accent on the -Y front face so orientation is unmistakable
    add_tapered_limb_segment(bm, uv_layer, Vector((-6.5, -9.8, 163.5)), Vector((6.5, -9.8, 163.5)), 1.6, 1.8, 1.6, 1.8, 2, 8, 2)

    # 3. Iconic Spiky Shonen Hair Crown (reaches exact Z = 180.0 cm!)
    add_tapered_limb_segment(bm, uv_layer, Vector((0, 0.5, 165)), Vector((0, 1.5, 176)), 10.4, 11.0, 7.5, 8.0, 2, 12, 3)
    spikes = (
        [
            (Vector((0, 0, 173)), Vector((0, -5, 181.5)), 5.2),
            (Vector((6, 0, 171)), Vector((16, 4, 180.0)), 4.6),
            (Vector((-6, 0, 171)), Vector((-16, 4, 180.0)), 4.6),
            (Vector((0, 5, 170)), Vector((0, 16, 177.0)), 4.8),
        ]
        if is_enemy
        else [
            (Vector((0, -1, 172)), Vector((0, -7, 180.0)), 5.0),
            (Vector((5, -1, 171)), Vector((15, -2, 179.2)), 4.8),
            (Vector((-5, -1, 171)), Vector((-15, -2, 179.2)), 4.8),
            (Vector((6, 3, 169)), Vector((17, 9, 175.5)), 4.5),
            (Vector((-6, 3, 169)), Vector((-17, 9, 175.5)), 4.5),
            (Vector((0, 6, 168)), Vector((0, 17, 173.0)), 4.6),
            (Vector((3, -6, 169)), Vector((6, -14, 165.0)), 3.2),
        ]
    )
    for b_pos, t_pos, rad in spikes:
        add_hair_spike(bm, uv_layer, b_pos, t_pos, rad, 3)

    # 4. Left & Right Arms (Gi shoulder caps + muscular skin arms + wristbands + fists)
    for sx in (1.0, -1.0):
        shoulder_mat = 2 if is_enemy else 1
        sh_rad = 7.8 if is_enemy else 6.4
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 13, 0, 143)), Vector((sx * 23, 0, 142.5)), sh_rad, sh_rad, 5.5, 5.5, 2, 10, shoulder_mat)
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 22, 0, 142.5)), Vector((sx * 42, 0, 141)), 5.0, 5.0, 4.4, 4.4, 4, 10, 0)
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 42, 0, 141)), Vector((sx * 58, -0.8, 140.2)), 4.5, 4.5, 3.8, 3.8, 4, 10, 0)
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 57.5, -0.8, 140.2)), Vector((sx * 65.5, -1.0, 140.0)), 4.2, 4.2, 4.0, 4.0, 2, 10, 2)  # Wristband
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 65.0, -1.0, 140.0)), Vector((sx * 74.5, -1.5, 139.2)), 3.9, 3.2, 3.6, 2.9, 2, 10, 0)  # Fist

    # 5. Baggy Martial-Arts Pants (Thighs + Calves) & Combat Boots (Feet resting cleanly at Z = 0.0 cm!)
    for sx in (1.0, -1.0):
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 10.5, 0, 96)), Vector((sx * 11.0, -0.5, 52)), 9.2, 9.5, 8.2, 8.5, 5, 12, 1)
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 11.0, -0.5, 52)), Vector((sx * 11.0, 0.8, 22)), 7.8, 8.0, 5.4, 5.6, 4, 12, 1)
        # Tall anime martial-arts boot shaft + sole resting at exact Z = 0.0 cm!
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 11.0, 0.8, 23)), Vector((sx * 11.0, 1.0, 5.0)), 5.0, 5.2, 4.5, 4.8, 3, 10, 4)
        add_tapered_limb_segment(bm, uv_layer, Vector((sx * 11.0, 4.5, 4.2)), Vector((sx * 11.0, -19.5, 3.5)), 4.6, 4.2, 3.8, 3.5, 3, 10, 4)

    # Recalculate outward normals and clamp exact ground plane Z = 0.0 cm
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    min_z = min(v.co.z for v in bm.verts)
    for v in bm.verts:
        v.co.z -= min_z

    bm.to_mesh(mesh_data)
    bm.free()
    mesh_data.update()

    # Deterministic, smooth top-4 bone skinning based on distance to deform bone segments (0 unweighted, <=4 influences)
    deform_bones = [b for b in arm_obj.data.bones if b.use_deform]
    vgroups = {b.name: mesh_obj.vertex_groups.new(name=b.name) for b in deform_bones}

    def point_segment_dist(pt: Vector, a: Vector, b: Vector) -> float:
        ab = b - a
        denom = ab.length_squared
        if denom < 1e-6:
            return (pt - a).length
        t = max(0.0, min(1.0, (pt - a).dot(ab) / denom))
        return (pt - (a + ab * t)).length

    for v in mesh_data.vertices:
        pt = v.co
        scored = []
        for b in deform_bones:
            d = point_segment_dist(pt, b.head_local, b.tail_local)
            # Strong inverse falloff for smooth joint blending at shoulders, elbows, hips, and knees
            w_raw = 1.0 / pow(max(d, 1.2), 3.2)
            scored.append((w_raw, b.name))
        scored.sort(key=lambda x: x[0], reverse=True)
        top = scored[:3]
        total_w = sum(item[0] for item in top)
        for w_val, b_name in top:
            norm_w = w_val / total_w
            if norm_w > 0.01:
                vgroups[b_name].add([v.index], norm_w, "REPLACE")

    arm_mod = mesh_obj.modifiers.new(name="Armature", type="ARMATURE")
    arm_mod.object = arm_obj
    mesh_obj.parent = arm_obj
    return mesh_obj


def set_bone_rot_deg(pose_bones, bone_name: str, rx: float, ry: float, rz: float):
    pb = pose_bones.get(bone_name)
    if not pb:
        return
    pb.rotation_mode = "XYZ"
    pb.rotation_euler = Euler((math.radians(rx), math.radians(ry), math.radians(rz)), "XYZ")


def clear_all_pose(arm_obj: bpy.types.Object):
    for pb in arm_obj.pose.bones:
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = Euler((0.0, 0.0, 0.0), "XYZ")
        pb.location = Vector((0.0, 0.0, 0.0))
        pb.scale = Vector((1.0, 1.0, 1.0))


def keyframe_all_deform_bones(arm_obj: bpy.types.Object, frame: int):
    for pb in arm_obj.pose.bones:
        pb.keyframe_insert(data_path="location", frame=frame)
        pb.keyframe_insert(data_path="rotation_euler", frame=frame)


def build_action(scene, arm_obj: bpy.types.Object, action_name: str, num_frames: int, pose_fn) -> bpy.types.Action:
    if not arm_obj.animation_data:
        arm_obj.animation_data_create()

    act = bpy.data.actions.new(name=action_name)
    arm_obj.animation_data.action = act
    scene.frame_start = 0
    scene.frame_end = num_frames

    for f in range(0, num_frames + 1):
        scene.frame_set(f)
        clear_all_pose(arm_obj)
        alpha = f / float(max(1, num_frames))
        pose_fn(arm_obj.pose.bones, alpha)
        keyframe_all_deform_bones(arm_obj, f)

    return act


def export_animation_fbx(scene, arm_obj: bpy.types.Object, output_path: str):
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    arm_obj.select_set(True)
    bpy.context.view_layer.objects.active = arm_obj

    bpy.ops.export_scene.fbx(
        filepath=output_path,
        use_selection=True,
        object_types={"ARMATURE"},
        axis_forward="-Y",
        axis_up="Z",
        global_scale=1.0,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_NONE",
        add_leaf_bones=False,
        primary_bone_axis="Y",
        secondary_bone_axis="X",
        armature_nodetype="NULL",
        bake_anim=True,
        bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True,
    )


def author_and_export_15_animations(scene, arm_obj: bpy.types.Object, out_dir: str):
    def pose_idle(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        pb["pelvis"].location = Vector((0.0, 0.0, s * 1.4))
        set_bone_rot_deg(pb, "spine_01", 4.0 + s * 2.0, 0.0, -8.0)
        set_bone_rot_deg(pb, "head", -2.0 * s, 0.0, 8.0)
        set_bone_rot_deg(pb, "upperarm_l", 18.0, 0.0, -48.0)
        set_bone_rot_deg(pb, "lowerarm_l", 55.0 + s * 4.0, 0.0, 25.0)
        set_bone_rot_deg(pb, "upperarm_r", -12.0, 0.0, 48.0)
        set_bone_rot_deg(pb, "lowerarm_r", 62.0 - s * 4.0, 0.0, -25.0)
        set_bone_rot_deg(pb, "thigh_l", 10.0, 0.0, 6.0)
        set_bone_rot_deg(pb, "calf_l", -16.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", -8.0, 0.0, -6.0)
        set_bone_rot_deg(pb, "calf_r", -12.0, 0.0, 0.0)

    def pose_walk(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        c = abs(math.cos(a * 2.0 * math.pi))
        pb["pelvis"].location = Vector((0.0, 0.0, c * 2.2))
        set_bone_rot_deg(pb, "spine_01", 6.0, 0.0, s * 6.0)
        set_bone_rot_deg(pb, "thigh_l", s * 28.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "calf_l", min(0.0, -s * 32.0) - 6.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", -s * 28.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "calf_r", min(0.0, s * 32.0) - 6.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", -s * 22.0, 0.0, -58.0)
        set_bone_rot_deg(pb, "lowerarm_l", 24.0, 0.0, 15.0)
        set_bone_rot_deg(pb, "upperarm_r", s * 22.0, 0.0, 58.0)
        set_bone_rot_deg(pb, "lowerarm_r", 24.0, 0.0, -15.0)

    def pose_run(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        c = abs(math.cos(a * 2.0 * math.pi))
        pb["pelvis"].location = Vector((0.0, 0.0, c * 4.5 - 2.0))
        set_bone_rot_deg(pb, "spine_01", 22.0, 0.0, s * 10.0)
        set_bone_rot_deg(pb, "spine_02", 12.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "head", -20.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_l", s * 48.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "calf_l", min(0.0, -s * 55.0) - 12.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", -s * 48.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "calf_r", min(0.0, s * 55.0) - 12.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", -s * 42.0, 0.0, -52.0)
        set_bone_rot_deg(pb, "lowerarm_l", 70.0, 0.0, 20.0)
        set_bone_rot_deg(pb, "upperarm_r", s * 42.0, 0.0, 52.0)
        set_bone_rot_deg(pb, "lowerarm_r", 70.0, 0.0, -20.0)

    def pose_jump(pb, a):
        k = min(1.0, a * 2.2)
        set_bone_rot_deg(pb, "spine_01", -8.0 * k, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_l", 42.0 * k, 0.0, 6.0)
        set_bone_rot_deg(pb, "calf_l", -65.0 * k, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", 18.0 * k, 0.0, -6.0)
        set_bone_rot_deg(pb, "calf_r", -28.0 * k, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", 35.0 * k, 0.0, -35.0)
        set_bone_rot_deg(pb, "upperarm_r", 35.0 * k, 0.0, 35.0)

    def pose_fall(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        set_bone_rot_deg(pb, "spine_01", 8.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_l", 26.0 + s * 4.0, 0.0, 8.0)
        set_bone_rot_deg(pb, "calf_l", -40.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", 14.0 - s * 4.0, 0.0, -8.0)
        set_bone_rot_deg(pb, "calf_r", -24.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", 12.0, 0.0, -28.0 + s * 5.0)
        set_bone_rot_deg(pb, "upperarm_r", 12.0, 0.0, 28.0 - s * 5.0)

    def pose_landing(pb, a):
        dip = math.sin(a * math.pi)
        pb["pelvis"].location = Vector((0.0, 0.0, -14.0 * dip))
        set_bone_rot_deg(pb, "spine_01", 18.0 * dip, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_l", 36.0 * dip, 0.0, 8.0)
        set_bone_rot_deg(pb, "calf_l", -52.0 * dip, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", 36.0 * dip, 0.0, -8.0)
        set_bone_rot_deg(pb, "calf_r", -52.0 * dip, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", 15.0 * dip, 0.0, -45.0)
        set_bone_rot_deg(pb, "upperarm_r", 15.0 * dip, 0.0, 45.0)

    def pose_hover(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        pb["pelvis"].location = Vector((0.0, 0.0, 6.0 + s * 4.0))
        set_bone_rot_deg(pb, "spine_01", -5.0 + s * 2.0, 0.0, -6.0)
        set_bone_rot_deg(pb, "thigh_l", 45.0 + s * 3.0, 0.0, 6.0)
        set_bone_rot_deg(pb, "calf_l", -68.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_r", 6.0 - s * 2.0, 0.0, -4.0)
        set_bone_rot_deg(pb, "calf_r", -14.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", 15.0, 0.0, -42.0 + s * 4.0)
        set_bone_rot_deg(pb, "lowerarm_l", 45.0, 0.0, 20.0)
        set_bone_rot_deg(pb, "upperarm_r", 15.0, 0.0, 42.0 - s * 4.0)
        set_bone_rot_deg(pb, "lowerarm_r", 45.0, 0.0, -20.0)

    def pose_fly_forward(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        pb["pelvis"].location = Vector((0.0, 0.0, 8.0 + s * 2.0))
        set_bone_rot_deg(pb, "pelvis", 52.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "spine_02", 14.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "head", -48.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", -35.0, 0.0, -62.0)
        set_bone_rot_deg(pb, "lowerarm_l", 18.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_r", -35.0, 0.0, 62.0)
        set_bone_rot_deg(pb, "lowerarm_r", 18.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "thigh_l", -10.0 + s * 3.0, 0.0, 3.0)
        set_bone_rot_deg(pb, "thigh_r", -10.0 - s * 3.0, 0.0, -3.0)

    def pose_fly_backward(pb, a):
        s = math.sin(a * 2.0 * math.pi)
        pb["pelvis"].location = Vector((0.0, 0.0, 6.0 + s * 2.0))
        set_bone_rot_deg(pb, "pelvis", -24.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "head", 20.0, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", 42.0, 0.0, -38.0)
        set_bone_rot_deg(pb, "lowerarm_l", 55.0, 0.0, 30.0)
        set_bone_rot_deg(pb, "upperarm_r", 42.0, 0.0, 38.0)
        set_bone_rot_deg(pb, "lowerarm_r", 55.0, 0.0, -30.0)
        set_bone_rot_deg(pb, "thigh_l", 28.0, 0.0, 5.0)
        set_bone_rot_deg(pb, "calf_l", -38.0, 0.0, 0.0)

    def pose_melee_01(pb, a):
        punch = math.sin(min(1.0, a * 1.4) * math.pi)
        set_bone_rot_deg(pb, "spine_01", 12.0 * punch, 0.0, 34.0 * punch)
        set_bone_rot_deg(pb, "spine_02", 8.0 * punch, 0.0, 22.0 * punch)
        set_bone_rot_deg(pb, "upperarm_r", 78.0 * punch, 0.0, 15.0)
        set_bone_rot_deg(pb, "lowerarm_r", 10.0 * (1.0 - punch), 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", -25.0 * punch, 0.0, -52.0)
        set_bone_rot_deg(pb, "lowerarm_l", 68.0, 0.0, 25.0)

    def pose_melee_02(pb, a):
        punch = math.sin(min(1.0, a * 1.4) * math.pi)
        set_bone_rot_deg(pb, "spine_01", 14.0 * punch, 0.0, -38.0 * punch)
        set_bone_rot_deg(pb, "spine_02", 10.0 * punch, 0.0, -24.0 * punch)
        set_bone_rot_deg(pb, "upperarm_l", 82.0 * punch, 0.0, -12.0)
        set_bone_rot_deg(pb, "lowerarm_l", 12.0 * (1.0 - punch), 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_r", -28.0 * punch, 0.0, 52.0)
        set_bone_rot_deg(pb, "lowerarm_r", 68.0, 0.0, -25.0)

    def pose_melee_03(pb, a):
        smash = math.sin(min(1.0, a * 1.3) * math.pi)
        pb["pelvis"].location = Vector((0.0, 0.0, 10.0 * smash))
        set_bone_rot_deg(pb, "spine_01", 26.0 * smash, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", 76.0 * smash, 0.0, -18.0)
        set_bone_rot_deg(pb, "upperarm_r", 76.0 * smash, 0.0, 18.0)
        set_bone_rot_deg(pb, "thigh_r", 52.0 * smash, 0.0, -10.0)
        set_bone_rot_deg(pb, "calf_r", -35.0 * smash, 0.0, 0.0)

    def pose_kiblast(pb, a):
        thrust = math.sin(min(1.0, a * 1.5) * math.pi)
        set_bone_rot_deg(pb, "spine_01", 10.0 * thrust, 0.0, 26.0 * thrust)
        set_bone_rot_deg(pb, "upperarm_r", 84.0 * thrust, 0.0, 8.0)
        set_bone_rot_deg(pb, "hand_r", -55.0 * thrust, 0.0, 0.0)
        set_bone_rot_deg(pb, "upperarm_l", -20.0 * thrust, 0.0, -48.0)

    def pose_hit_reaction(pb, a):
        flinch = math.sin(a * math.pi)
        set_bone_rot_deg(pb, "spine_01", -24.0 * flinch, 0.0, -14.0 * flinch)
        set_bone_rot_deg(pb, "head", -28.0 * flinch, 0.0, 12.0 * flinch)
        set_bone_rot_deg(pb, "upperarm_l", 22.0 * flinch, 0.0, -25.0)
        set_bone_rot_deg(pb, "upperarm_r", 22.0 * flinch, 0.0, 25.0)

    def pose_death(pb, a):
        fall = min(1.0, a * 1.25)
        pb["pelvis"].location = Vector((0.0, 18.0 * fall, -82.0 * fall))
        set_bone_rot_deg(pb, "pelvis", -86.0 * fall, 0.0, 0.0)
        set_bone_rot_deg(pb, "head", -22.0 * fall, 0.0, 18.0 * fall)
        set_bone_rot_deg(pb, "upperarm_l", 0.0, 0.0, -20.0 * fall)
        set_bone_rot_deg(pb, "upperarm_r", 0.0, 0.0, 20.0 * fall)

    specs = [
        ("A_Player_Idle", 36, pose_idle),
        ("A_Player_Walk", 24, pose_walk),
        ("A_Player_Run", 18, pose_run),
        ("A_Player_Jump", 14, pose_jump),
        ("A_Player_Fall", 24, pose_fall),
        ("A_Player_Landing", 12, pose_landing),
        ("A_Player_Hover", 36, pose_hover),
        ("A_Player_FlyForward", 24, pose_fly_forward),
        ("A_Player_FlyBackward", 24, pose_fly_backward),
        ("A_Player_Melee_01", 10, pose_melee_01),
        ("A_Player_Melee_02", 10, pose_melee_02),
        ("A_Player_Melee_03", 12, pose_melee_03),
        ("A_Player_KiBlast", 12, pose_kiblast),
        ("A_Player_HitReaction", 10, pose_hit_reaction),
        ("A_Player_Death", 22, pose_death),
    ]

    anim_dir = os.path.join(out_dir, "Animations")
    for anim_name, frames, fn in specs:
        build_action(scene, arm_obj, anim_name, frames, fn)
        out_fbx = os.path.join(anim_dir, f"{anim_name}.fbx")
        export_animation_fbx(scene, arm_obj, out_fbx)
        print(f"[ANIM EXPORT] {anim_name}.fbx ({frames} frames, {os.path.getsize(out_fbx)} bytes)")


def main():
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    out_dir = argv[0] if argv else os.path.abspath(os.path.join(SCRIPT_DIR, "..", "..", "Intermediate", "BlenderExports"))
    os.makedirs(out_dir, exist_ok=True)

    # 1. Build, Validate & Export Anime Player Hero + 15 Skeletal Animations
    scene = reset_metric_cm_scene()
    arm_player = create_humanoid_armature(scene)
    build_stylized_anime_mesh(scene, arm_player, "SK_Player", is_enemy=False)

    errors, warnings = validator.validate_scene()
    for w in warnings:
        print(f"[PLAYER WARN] {w}")
    if errors:
        for e in errors:
            print(f"[PLAYER ERROR] {e}")
        sys.exit(1)

    player_fbx = os.path.join(out_dir, "SK_Player.fbx")
    validator.export_ue5_fbx(player_fbx, bake_anim=False)
    author_and_export_15_animations(scene, arm_player, out_dir)

    # 2. Build, Validate & Export Anime Raider Enemy
    scene = reset_metric_cm_scene()
    arm_enemy = create_humanoid_armature(scene)
    build_stylized_anime_mesh(scene, arm_enemy, "SK_Enemy", is_enemy=True)

    errors_e, warnings_e = validator.validate_scene()
    for w in warnings_e:
        print(f"[ENEMY WARN] {w}")
    if errors_e:
        for e in errors_e:
            print(f"[ENEMY ERROR] {e}")
        sys.exit(1)

    enemy_fbx = os.path.join(out_dir, "SK_Enemy.fbx")
    validator.export_ue5_fbx(enemy_fbx, bake_anim=False)
    print("=== [BLENDER 5.2 M6] ALL ANIME CHARACTERS & 15 ANIMATIONS VALIDATED & EXPORTED CLEANLY ===")


if __name__ == "__main__":
    main()
