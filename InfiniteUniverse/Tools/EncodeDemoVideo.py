"""
Encodes captured UE 5.8 live D3D12 gameplay frames (DemoFrames/frame_*.png)
into Documentation/Media/Step1_CoreGameplayLoop_Demo.mp4.
"""

import glob
import os
import sys
import cv2


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    proj_root = os.path.dirname(script_dir)

    candidates = [
        os.path.join(proj_root, "Saved", "PackagedBuilds", "Windows", "InfiniteUniverse", "Saved", "DemoFrames"),
        os.path.join(proj_root, "Saved", "DemoFrames"),
    ]
    frames_dir = next((d for d in candidates if os.path.isdir(d) and glob.glob(os.path.join(d, "frame_*.png"))), candidates[0])

    out_dir = os.path.join(proj_root, "Documentation", "Media")
    os.makedirs(out_dir, exist_ok=True)
    out_mp4 = os.path.join(out_dir, "Step1_CoreGameplayLoop_Demo.mp4")

    frames = sorted(glob.glob(os.path.join(frames_dir, "frame_*.png")))
    if not frames:
        print(f"[ERROR] No PNG frames found in {frames_dir}")
        sys.exit(1)

    first = cv2.imread(frames[0])
    if first is None:
        print(f"[ERROR] Failed to read {frames[0]}")
        sys.exit(1)

    h, w = first.shape[:2]
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(out_mp4, fourcc, 15.0, (w, h))
    if not writer.isOpened():
        print(f"[ERROR] Failed to open VideoWriter for {out_mp4}")
        sys.exit(1)

    written = 0
    for f_path in frames:
        img = cv2.imread(f_path)
        if img is not None:
            if img.shape[:2] != (h, w):
                img = cv2.resize(img, (w, h))
            writer.write(img)
            written += 1

    writer.release()
    size_mb = os.path.getsize(out_mp4) / (1024.0 * 1024.0)
    print(f"[VIDEO PASS] Encoded {written} live D3D12 frames ({w}x{h} @ 15fps) -> {out_mp4} ({size_mb:.2f} MB)")


if __name__ == "__main__":
    main()
