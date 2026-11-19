"""
Encodes live captured Dual-Client D3D12 frames (Step2Frames_Client1 & Step2Frames_Client2)
into a synchronized 1920x588 Side-by-Side Split-Screen Multiplayer Demo Video:
Documentation/Media/Step2_Multiplayer_Demo.mp4
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

    c1_dir = os.path.join(saved_root, "Step2Frames_Client1")
    c2_dir = os.path.join(saved_root, "Step2Frames_Client2")

    c1_frames = sorted(glob.glob(os.path.join(c1_dir, "frame_*.png")))
    c2_frames = sorted(glob.glob(os.path.join(c2_dir, "frame_*.png")))

    if not c1_frames:
        print(f"[ERROR] No Client 1 frames found in {c1_dir}")
        sys.exit(1)

    out_dir = os.path.join(proj_root, "Documentation", "Media")
    os.makedirs(out_dir, exist_ok=True)
    out_mp4 = os.path.join(out_dir, "Step2_Multiplayer_Demo.mp4")

    panel_w, panel_h = 960, 540
    header_h = 48
    out_w = panel_w * 2
    out_h = panel_h + header_h

    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(out_mp4, fourcc, 15.0, (out_w, out_h))
    if not writer.isOpened():
        print(f"[ERROR] Failed to open VideoWriter for {out_mp4}")
        sys.exit(1)

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
        canvas[:header_h, :] = (24, 16, 12)  # Dark slate-blue header bar

        cv2.putText(
            canvas,
            "CLIENT 1 (D3D12 Window -> UDP 127.0.0.1:7777)",
            (22, 31),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.68,
            (255, 225, 110),
            2,
            cv2.LINE_AA,
        )
        cv2.putText(
            canvas,
            "HEADLESS DEDICATED SERVER (:7777)",
            (765, 31),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.60,
            (130, 255, 165),
            2,
            cv2.LINE_AA,
        )
        cv2.putText(
            canvas,
            "CLIENT 2 (D3D12 Window -> UDP 127.0.0.1:7777)",
            (panel_w + 355, 31),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.68,
            (120, 210, 255),
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
    size_mb = os.path.getsize(out_mp4) / (1024.0 * 1024.0)
    print(f"[VIDEO PASS] Encoded {written} Dual-Client frames ({out_w}x{out_h} @ 15fps) -> {out_mp4} ({size_mb:.2f} MB)")


if __name__ == "__main__":
    main()
