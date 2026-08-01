#!/usr/bin/env python3
"""Build ESP32 animation assets (.crli) from the Clawd mascot GIFs.

Reads the clawd-on-desk GIF animations, composites them onto a solid
background, resizes, quantizes to a shared palette and encodes each frame as
run-length-encoded palette indices — the compact "CRLI" format decoded on the
device by lib/ClawdDisplay/AnimationManager.cpp.

Usage:
    python3 tools/build_assets.py <src_gif_dir> <out_dir> [width] [frames] [colors]

    <src_gif_dir>  path to clawd-on-desk/assets/gif
    <out_dir>      output directory (use ./data to feed LittleFS)
    width          target width in px       (default 240)
    frames         max frames per animation (default 0 = keep ALL frames)
    colors         palette size             (default 32)

Example:
    python3 tools/build_assets.py ../clawd-on-desk/assets/gif ./data 240 24 32

Requires Pillow:  pip install Pillow

CRLI format (little-endian):
    'C','R', ver=2, palColors u8, width u16, height u16, frameCount u16, maxRunCount u16
    palette: palColors x u16 (RGB565)
    frames (sequential): { delayMs u16, runCount u16, runCount x {count u8, index u8} }
"""
import os
import sys
import struct
from PIL import Image, ImageSequence

# state name -> source GIF (see docs/clawd-esp32/01-PROTOCOL.md §6.3)
STATE_TO_GIF = {
    "idle":         "clawd-idle.gif",
    "thinking":     "clawd-thinking.gif",
    "working":      "clawd-typing.gif",
    "juggling":     "clawd-juggling.gif",
    "carrying":     "clawd-carrying.gif",
    "sweeping":     "clawd-sweeping.gif",
    "attention":    "clawd-happy.gif",
    "notification": "clawd-notification.gif",
    "error":        "clawd-error.gif",
    "sleeping":     "clawd-sleeping.gif",
}
BACKGROUND = (0, 0, 0)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rle_indices(indices):
    """Run-length encode palette indices -> (bytes, run_count)."""
    out = bytearray()
    runs = 0
    n = len(indices)
    i = 0
    while i < n:
        color = indices[i]
        j = i + 1
        while j < n and indices[j] == color and (j - i) < 255:
            j += 1
        out += struct.pack("<BB", j - i, color)
        runs += 1
        i = j
    return bytes(out), runs


def build_state(src_gif, out_path, width, max_frames, colors):
    im = Image.open(src_gif)
    w, h = im.size
    th = max(1, round(h * width / w))

    all_frames = list(ImageSequence.Iterator(im))
    n = len(all_frames)
    # Keep every frame by default; only sub-sample if a positive cap < n is given.
    if max_frames and 0 < max_frames < n:
        k = max_frames
        sample = [round(i * (n - 1) / (k - 1)) for i in range(k)]
    else:
        k = n
        sample = list(range(n))
    # Preserve each frame's own duration for faithful playback (floor 20 ms).
    delays = [max(20, all_frames[i].info.get("duration", 100)) for i in sample]

    rgb_frames = []
    for idx in sample:
        frame = all_frames[idx].convert("RGBA")
        bg = Image.new("RGBA", frame.size, BACKGROUND + (255,))
        rgb_frames.append(
            Image.alpha_composite(bg, frame).convert("RGB").resize((width, th), Image.LANCZOS)
        )

    # one shared palette across all frames for stable colours + better RLE
    montage = Image.new("RGB", (width, th * k))
    for i, f in enumerate(rgb_frames):
        montage.paste(f, (0, i * th))
    pal_img = montage.quantize(colors=colors, dither=Image.Dither.NONE)
    pal = pal_img.getpalette()[: colors * 3]
    palette565 = [rgb565(pal[c * 3], pal[c * 3 + 1], pal[c * 3 + 2]) for c in range(colors)]

    body = bytearray()
    max_runs = 0
    for f, delay in zip(rgb_frames, delays):
        q = f.quantize(palette=pal_img, dither=Image.Dither.NONE)
        run_bytes, run_count = rle_indices(list(q.getdata()))
        max_runs = max(max_runs, run_count)
        body += struct.pack("<HH", delay, run_count) + run_bytes

    header = struct.pack("<BBBBHHHH", ord("C"), ord("R"), 2, colors, width, th, k, max_runs)
    palette_bytes = b"".join(struct.pack("<H", c) for c in palette565)
    data = header + palette_bytes + bytes(body)
    with open(out_path, "wb") as fp:
        fp.write(data)
    return len(data), k, max_runs


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src_dir = os.path.expanduser(sys.argv[1])
    out_dir = os.path.expanduser(sys.argv[2])
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    frames = int(sys.argv[4]) if len(sys.argv) > 4 else 0    # 0 = keep all frames
    colors = int(sys.argv[5]) if len(sys.argv) > 5 else 32

    os.makedirs(out_dir, exist_ok=True)
    total = 0
    print(f"config: {width}px  {frames}f  {colors}col")
    print(f"{'state':13s} {'bytes':>9s}  frames  maxRuns")
    for state, gif in STATE_TO_GIF.items():
        size, k, mr = build_state(
            os.path.join(src_dir, gif), os.path.join(out_dir, state + ".crli"),
            width, frames, colors,
        )
        total += size
        print(f"{state:13s} {size:9,d}  {k:5d}  {mr}")
    print(f"\nTOTAL: {total:,} bytes ({total / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
