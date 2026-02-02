#!/usr/bin/env python3
"""
Convert video files (MP4, AVI, GIF, etc.) to RGB332 .bin files
for playback on M5Stack CoreS3 SE (320x240, 10fps).

Usage:
  python convert_video.py <input_folder> <output_folder>
  python convert_video.py video.mp4 output/

Converts all video files found in input_folder, or a single file.
Output .bin files go to output_folder (ready to copy to SD card root).
"""

import sys
import os
import struct
import glob
import time
import cv2
import numpy as np

TARGET_W = 320
TARGET_H = 240
TARGET_FPS = 10

VIDEO_EXTS = {'.mp4', '.avi', '.mkv', '.mov', '.webm', '.gif', '.m4v', '.flv', '.wmv'}


def rgb_to_332(frame):
    """Vectorized RGB to RGB332 conversion using numpy."""
    r = frame[:, :, 2] & 0xE0              # Red: top 3 bits
    g = (frame[:, :, 1] >> 3) & 0x1C       # Green: top 3 bits, shifted
    b = (frame[:, :, 0] >> 6) & 0x03       # Blue: top 2 bits
    return (r | g | b).astype(np.uint8)


def convert_file(input_path, output_path):
    """Convert a single video file to .bin format."""
    cap = cv2.VideoCapture(input_path)
    if not cap.isOpened():
        print(f"  ERROR: Cannot open {input_path}")
        return False

    src_fps = cap.get(cv2.CAP_PROP_FPS)
    src_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    src_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    src_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    duration = src_frames / src_fps if src_fps > 0 else 0

    # Calculate frame skip to achieve target FPS
    frame_interval = max(1, round(src_fps / TARGET_FPS)) if src_fps > 0 else 1
    est_out_frames = src_frames // frame_interval

    print(f"  Source: {src_w}x{src_h} @ {src_fps:.1f}fps, {src_frames} frames ({duration:.1f}s)")
    print(f"  Output: {TARGET_W}x{TARGET_H} @ {TARGET_FPS}fps, ~{est_out_frames} frames")
    est_size = est_out_frames * TARGET_W * TARGET_H + 8
    print(f"  Est. size: {est_size / 1024 / 1024:.1f} MB")

    # First pass: count actual output frames
    out_frames = []
    frame_idx = 0
    t0 = time.time()

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        if frame_idx % frame_interval == 0:
            # Resize to target, maintaining aspect ratio with letterbox/pillarbox
            h, w = frame.shape[:2]
            scale = min(TARGET_W / w, TARGET_H / h)
            new_w = int(w * scale)
            new_h = int(h * scale)
            resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_AREA)

            # Create black canvas and center the frame
            canvas = np.zeros((TARGET_H, TARGET_W, 3), dtype=np.uint8)
            x_off = (TARGET_W - new_w) // 2
            y_off = (TARGET_H - new_h) // 2
            canvas[y_off:y_off+new_h, x_off:x_off+new_w] = resized

            # Convert to RGB332
            converted = rgb_to_332(canvas)
            out_frames.append(converted.tobytes())

            # Progress
            if len(out_frames) % 500 == 0:
                elapsed = time.time() - t0
                pct = frame_idx / src_frames * 100 if src_frames > 0 else 0
                print(f"  {pct:5.1f}% — {len(out_frames)} frames ({elapsed:.0f}s)")

        frame_idx += 1

    cap.release()

    if not out_frames:
        print(f"  ERROR: No frames extracted")
        return False

    elapsed = time.time() - t0
    print(f"  Extracted {len(out_frames)} frames in {elapsed:.1f}s")

    # Write .bin file
    frame_duration_ms = 1000 // TARGET_FPS
    with open(output_path, 'wb') as f:
        f.write(struct.pack('<HHHH', TARGET_W, TARGET_H, len(out_frames), frame_duration_ms))
        for fd in out_frames:
            f.write(fd)

    file_size = os.path.getsize(output_path)
    print(f"  Wrote {output_path} ({file_size / 1024 / 1024:.1f} MB)")
    return True


def main():
    if len(sys.argv) < 3:
        print("Usage: python convert_video.py <input_path> <output_folder>")
        print("  input_path: a video file or folder of videos")
        print("  output_folder: where to write .bin files")
        sys.exit(1)

    input_path = sys.argv[1]
    output_dir = sys.argv[2]
    os.makedirs(output_dir, exist_ok=True)

    # Collect input files
    if os.path.isfile(input_path):
        files = [input_path]
    elif os.path.isdir(input_path):
        files = []
        for ext in VIDEO_EXTS:
            files.extend(glob.glob(os.path.join(input_path, f'*{ext}')))
            files.extend(glob.glob(os.path.join(input_path, f'*{ext.upper()}')))
        files = sorted(set(files))
    else:
        print(f"ERROR: {input_path} not found")
        sys.exit(1)

    if not files:
        print("No video files found")
        sys.exit(1)

    print(f"Found {len(files)} video(s) to convert\n")
    total_size = 0
    t_start = time.time()

    for i, f in enumerate(files):
        name = os.path.splitext(os.path.basename(f))[0]
        # Zero-pad index for playback ordering
        out_name = f"{i:03d}_{name}.bin"
        out_path = os.path.join(output_dir, out_name)

        print(f"[{i+1}/{len(files)}] {os.path.basename(f)}")
        if convert_file(f, out_path):
            total_size += os.path.getsize(out_path)
        print()

    elapsed = time.time() - t_start
    print(f"Done! {len(files)} files, {total_size / 1024 / 1024:.0f} MB total, {elapsed:.0f}s")


if __name__ == '__main__':
    main()
