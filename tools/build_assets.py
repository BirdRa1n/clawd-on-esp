#!/usr/bin/env python3
"""Build ESP32 animation assets (.crli) from the Clawd mascot GIFs.

Auto-crops all animations to their shared content bounding box (so the mascot
fills the screen instead of floating in a mostly-empty canvas), composites onto
a solid background, quantizes to a shared palette and RLE-encodes each frame —
the compact "CRLI" format decoded on the device by AnimationManager.cpp.

Usage:
    python3 tools/build_assets.py <src_gif_dir> <out_dir> [max_w] [max_h] [frames] [colors]

    <src_gif_dir>  path to clawd-on-desk/assets/gif
    <out_dir>      output directory (use ./data to feed LittleFS)
    max_w/max_h    max output size in px    (default 240 x 238)
    frames         max frames per animation (default 0 = keep ALL frames)
    colors         palette size             (default 32)

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
PAD_FRAC = 0.06


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rle_indices(indices):
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


def content_bbox(gif_paths):
    """Union of the non-transparent bounding box across every frame of every GIF."""
    x0 = y0 = 1 << 30
    x1 = y1 = -1
    cw = ch = 0
    for p in gif_paths:
        im = Image.open(p)
        cw, ch = im.size
        for fr in ImageSequence.Iterator(im):
            bb = fr.convert("RGBA").getchannel("A").getbbox()
            if bb:
                x0 = min(x0, bb[0]); y0 = min(y0, bb[1])
                x1 = max(x1, bb[2]); y1 = max(y1, bb[3])
    pw = int((x1 - x0) * PAD_FRAC)
    ph = int((y1 - y0) * PAD_FRAC)
    return (max(0, x0 - pw), max(0, y0 - ph), min(cw, x1 + pw), min(ch, y1 + ph))


def build_state(src_gif, out_path, bbox, out_w, out_h, max_frames, colors):
    im = Image.open(src_gif)
    rgba_all, dur_all = [], []
    for f in ImageSequence.Iterator(im):
        rgba_all.append(f.convert("RGBA").crop(bbox).copy())    # crop to shared content box
        dur_all.append(max(20, f.info.get("duration", 100)))
    n = len(rgba_all)

    if max_frames and 0 < max_frames < n:
        k = max_frames
        sample = [round(i * (n - 1) / (k - 1)) for i in range(k)]
    else:
        k = n
        sample = list(range(n))
    delays = [dur_all[i] for i in sample]

    rgb_frames = []
    for idx in sample:
        frame = rgba_all[idx]
        bg = Image.new("RGBA", frame.size, BACKGROUND + (255,))
        rgb_frames.append(
            Image.alpha_composite(bg, frame).convert("RGB").resize((out_w, out_h), Image.LANCZOS)
        )

    montage = Image.new("RGB", (out_w, out_h * k))
    for i, f in enumerate(rgb_frames):
        montage.paste(f, (0, i * out_h))
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

    header = struct.pack("<BBBBHHHH", ord("C"), ord("R"), 2, colors, out_w, out_h, k, max_runs)
    palette_bytes = b"".join(struct.pack("<H", c) for c in palette565)
    with open(out_path, "wb") as fp:
        fp.write(header + palette_bytes + bytes(body))
    return os.path.getsize(out_path), k, max_runs


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src_dir = os.path.expanduser(sys.argv[1])
    out_dir = os.path.expanduser(sys.argv[2])
    max_w = int(sys.argv[3]) if len(sys.argv) > 3 else 240
    max_h = int(sys.argv[4]) if len(sys.argv) > 4 else 238
    frames = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    colors = int(sys.argv[6]) if len(sys.argv) > 6 else 32

    os.makedirs(out_dir, exist_ok=True)
    paths = [os.path.join(src_dir, g) for g in STATE_TO_GIF.values()]
    bbox = content_bbox(paths)
    bw, bh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    scale = min(max_w / bw, max_h / bh)
    out_w, out_h = round(bw * scale), round(bh * scale)
    print(f"content bbox {bw}x{bh} -> output {out_w}x{out_h}  ({colors} col, {frames or 'all'} frames)")
    print(f"{'state':13s} {'bytes':>9s}  frames  maxRuns")

    total = 0
    for state, gif in STATE_TO_GIF.items():
        size, k, mr = build_state(
            os.path.join(src_dir, gif), os.path.join(out_dir, state + ".crli"),
            bbox, out_w, out_h, frames, colors,
        )
        total += size
        print(f"{state:13s} {size:9,d}  {k:5d}  {mr}")
    print(f"\nTOTAL: {total:,} bytes ({total / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
