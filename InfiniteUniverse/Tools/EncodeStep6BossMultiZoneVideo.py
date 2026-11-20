"""
Encodes live captured Dual-Client Step 6 Multi-Server Zone Transfer (:7777 Khu 1 -> :7778 Khu 2),
3-Phase World Boss (Highland Titan), 3D Aerial Quake Evasion (>4.5m), Poise Stagger, Quest Turn-In & Pity Shop frames
into a synchronized 1920x588 Side-by-Side Demo Video:
Documentation/Media/Step6_WorldBoss_MultiZone_Demo.mp4
"""

import glob
import os
import sys
import cv2
import numpy as np


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    proj_root = os.path.dirname(script_dir)
    saved_root = os.path.join(proj_root, "Saved", "PackagedBuilds", "Windows", "InfiniteUniverse", "Saved")

    c1_frames = sorted(glob.glob(os.path.join(saved_root, "Step6Frames_Client1", "frame_*.png")))
    c2_frames = sorted(glob.glob(os.path.join(saved_root, "Step6Frames_Client2", "frame_*.png")))

    if not c1_frames:
        print(f"[ERROR] Missing captured frames in {saved_root}/Step6Frames_Client1")
        sys.exit(1)

    panel_w, panel_h, header_h = 960, 540, 48
    out_w, out_h = panel_w * 2, panel_h + header_h

    out_dir = os.path.join(proj_root, "Documentation", "Media")
    os.makedirs(out_dir, exist_ok=True)
    out_mp4 = os.path.join(out_dir, "Step6_WorldBoss_MultiZone_Demo.mp4")

    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(out_mp4, fourcc, 15.0, (out_w, out_h))
    if not writer.isOpened():
        print(f"[ERROR] Failed to open VideoWriter for {out_mp4}")
        sys.exit(1)

    count = max(len(c1_frames), len(c2_frames)) if c2_frames else len(c1_frames)
    written = 0
    mid_split = max(1, int(count * 0.43))

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
        canvas[:header_h, :] = (24, 15, 10)

        stage_label = (
            "STAGE A (:7777 KHU 1): Quest + Doi Khu Transfer"
            if i < mid_split
            else "STAGE B (:7778 KHU 2): 3-Phase Boss + 3D Dodge + Pity Shop"
        )
        cv2.putText(
            canvas,
            "CLIENT 1: Party Leader / Quest & Pity Shop UI",
            (16, 31),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.53,
            (110, 225, 255),
            2,
            cv2.LINE_AA,
        )
        cv2.putText(
            canvas,
            stage_label,
            (615, 31),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.53,
            (125, 255, 155),
            2,
            cv2.LINE_AA,
        )
        cv2.putText(
            canvas,
            "CLIENT 2: Co-op Raider / Aerial Quake Dodge Ally",
            (panel_w + 225, 31),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.53,
            (255, 215, 115),
            2,
            cv2.LINE_AA,
        )

        canvas[header_h:, :panel_w] = img1
        canvas[header_h:, panel_w:] = img2
        cv2.line(canvas, (panel_w, 0), (panel_w, out_h), (90, 180, 230), 2)
        cv2.line(canvas, (0, header_h), (out_w, header_h), (70, 130, 180), 1)

        writer.write(canvas)
        written += 1

    writer.release()
    size_mb = os.path.getsize(out_mp4) / (1024 * 1024)
    print(f"[SUCCESS] Encoded {written} frames ({written / 15.0:.1f}s @ 15fps) -> {out_mp4} ({size_mb:.2f} MB)")


if __name__ == "__main__":
    main()
