"""
Encodes live captured Dual-Client Step 3 Persistence frames (Phase 1 -> Server Restart -> Phase 2)
into a synchronized 1920x588 Side-by-Side Demo Video:
Documentation/Media/Step3_Persistence_Demo.mp4
"""

import glob
import os
import sys
import cv2
import numpy as np


def render_split_frames(writer, c1_frames, c2_frames, top_left, top_mid, top_right, panel_w=960, panel_h=540, header_h=48):
    out_w = panel_w * 2
    out_h = panel_h + header_h
    count = max(len(c1_frames), len(c2_frames)) if c2_frames else len(c1_frames)
    written = 0

    for i in range(count):
        f1 = c1_frames[min(i, len(c1_frames) - 1)]
        img1 = cv2.imread(f1)
        if img1 is None:
            continue
        if img1.shape[:2] != (panel_h, panel_w):
            img1 = cv2.resize(img1, (panel_w, panel_h))

        if c2_frames:
            f2 = c2_frames[min(i, len(c2_frames) - 1)]
            img2 = cv2.imread(f2)
            if img2 is None:
                img2 = np.zeros((panel_h, panel_w, 3), dtype=np.uint8)
            elif img2.shape[:2] != (panel_h, panel_w):
                img2 = cv2.resize(img2, (panel_w, panel_h))
        else:
            img2 = np.zeros((panel_h, panel_w, 3), dtype=np.uint8)

        canvas = np.zeros((out_h, out_w, 3), dtype=np.uint8)
        canvas[:header_h, :] = (26, 16, 10)

        cv2.putText(canvas, top_left, (18, 31), cv2.FONT_HERSHEY_SIMPLEX, 0.62, (110, 225, 255), 2, cv2.LINE_AA)
        cv2.putText(canvas, top_mid, (715, 31), cv2.FONT_HERSHEY_SIMPLEX, 0.58, (125, 255, 155), 2, cv2.LINE_AA)
        cv2.putText(canvas, top_right, (panel_w + 335, 31), cv2.FONT_HERSHEY_SIMPLEX, 0.62, (255, 215, 115), 2, cv2.LINE_AA)

        canvas[header_h:, :panel_w] = img1
        canvas[header_h:, panel_w:] = img2
        cv2.line(canvas, (panel_w, 0), (panel_w, out_h), (90, 180, 230), 2)
        cv2.line(canvas, (0, header_h), (out_w, header_h), (70, 130, 180), 1)

        writer.write(canvas)
        written += 1
    return written


def render_restart_interstitial(writer, frames_n=24, out_w=1920, out_h=588):
    for i in range(frames_n):
        card = np.zeros((out_h, out_w, 3), dtype=np.uint8)
        card[:, :] = (22, 14, 10)
        cv2.rectangle(card, (210, 130), (1710, 455), (48, 34, 22), -1)
        cv2.rectangle(card, (210, 130), (1710, 455), (90, 200, 255), 2)

        cv2.putText(
            card,
            "[ DEDICATED SERVER #1 TERMINATED — ZERO RAM STATE REMAINING ]",
            (320, 225),
            cv2.FONT_HERSHEY_SIMPLEX,
            1.02,
            (95, 110, 255),
            2,
            cv2.LINE_AA,
        )
        cv2.putText(
            card,
            "Booting Brand-New Dedicated Server #2 (:7777) + Go Gateway (:8080) + PostgreSQL 18 (:5433)",
            (280, 305),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.78,
            (240, 240, 245),
            2,
            cv2.LINE_AA,
        )
        cv2.putText(
            card,
            "Re-Login -> Epoch #1->#2 -> Restoring Lv.2 (EXP Only) + Potential Stats (300 HP, 300 Ki, 110 Atk)!",
            (235, 380),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.70,
            (120, 255, 165),
            2,
            cv2.LINE_AA,
        )
        writer.write(card)
    return frames_n


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    proj_root = os.path.dirname(script_dir)
    saved_root = os.path.join(proj_root, "Saved", "PackagedBuilds", "Windows", "InfiniteUniverse", "Saved")

    p1_c1 = sorted(glob.glob(os.path.join(saved_root, "Step3Frames_Phase1_Client1", "frame_*.png")))
    p1_c2 = sorted(glob.glob(os.path.join(saved_root, "Step3Frames_Phase1_Client2", "frame_*.png")))
    p2_c1 = sorted(glob.glob(os.path.join(saved_root, "Step3Frames_Phase2_Client1", "frame_*.png")))
    p2_c2 = sorted(glob.glob(os.path.join(saved_root, "Step3Frames_Phase2_Client2", "frame_*.png")))

    if not p1_c1 or not p2_c1:
        print(f"[ERROR] Missing captured frames: Phase1_C1={len(p1_c1)}, Phase2_C1={len(p2_c1)}")
        sys.exit(1)

    out_dir = os.path.join(proj_root, "Documentation", "Media")
    os.makedirs(out_dir, exist_ok=True)
    out_mp4 = os.path.join(out_dir, "Step3_Persistence_Demo.mp4")

    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(out_mp4, fourcc, 15.0, (1920, 588))
    if not writer.isOpened():
        print(f"[ERROR] Failed to open VideoWriter for {out_mp4}")
        sys.exit(1)

    total = 0
    total += render_split_frames(
        writer,
        p1_c1,
        p1_c2,
        "CLIENT 1: Raid (+LV EXP & +TN) -> +HP/+Ki/+Atk -> Logout",
        "DEDICATED SERVER #1 + POSTGRESQL 18 (Epoch #1)",
        "CLIENT 2: Co-op Raid & Potential Upgrade (Epoch #1)",
    )
    total += render_restart_interstitial(writer, 24)
    total += render_split_frames(
        writer,
        p2_c1,
        p2_c2,
        "CLIENT 1: Restored Lv.2 + 300 HP / 300 Ki / 110 Atk!",
        "NEW DEDICATED SERVER #2 + POSTGRESQL 18 (Epoch #2)",
        "CLIENT 2: Restored Lv.2 + Potential Stats (Epoch #2)",
    )

    writer.release()
    size_mb = os.path.getsize(out_mp4) / (1024.0 * 1024.0)
    print(f"[VIDEO PASS] Encoded {total} frames (1920x588 @ 15fps) -> {out_mp4} ({size_mb:.2f} MB)")


if __name__ == "__main__":
    main()
