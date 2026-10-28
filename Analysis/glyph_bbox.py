# -*- coding: utf-8 -*-
"""Measure exact glyph bounding boxes of bright-on-dark textures (e.g. logos).

Usage:
    python glyph_bbox.py <image> [threshold] [min_gap]

Splits the image into text bands at empty rows, then prints, per band, the
tight bbox in pixels plus the 0..1000 normalised equivalent.
"""
import sys
from pathlib import Path

from PIL import Image

sys.stdout.reconfigure(encoding="utf-8")

path = Path(sys.argv[1])
thr = int(sys.argv[2]) if len(sys.argv) > 2 else 200
min_gap = int(sys.argv[3]) if len(sys.argv) > 3 else 3

im = Image.open(path).convert("RGBA")
w, h = im.size
px = im.load()

rows = [False] * h
for y in range(h):
    hit = False
    for x in range(w):
        r, g, b, a = px[x, y]
        if a > 128 and r >= thr and g >= thr and b >= thr:
            hit = True
            break
    rows[y] = hit

bands = []
start = None
gap = 0
for y in range(h):
    if rows[y]:
        if start is None:
            start = y
        gap = 0
    elif start is not None:
        gap += 1
        if gap >= min_gap:
            bands.append((start, y - gap))
            start = None
if start is not None:
    bands.append((start, h - 1))

print(f"{path.name} {w}x{h} bands={len(bands)}")
for y1, y2 in bands:
    xs = []
    for y in range(y1, y2 + 1):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 128 and r >= thr and g >= thr and b >= thr:
                xs.append(x)
    if not xs:
        continue
    x1, x2 = min(xs), max(xs)
    print(
        "  px[%4d,%4d,%4d,%4d]  norm[%4d,%4d,%4d,%4d]  %dx%d"
        % (
            x1, y1, x2, y2,
            x1 / w * 1000, y1 / h * 1000, x2 / w * 1000, y2 / h * 1000,
            x2 - x1 + 1, y2 - y1 + 1,
        )
    )
