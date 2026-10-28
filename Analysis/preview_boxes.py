# -*- coding: utf-8 -*-
"""Render OCR boxes onto the source texture so the boxes can be eyeballed.

Usage:
    python preview_boxes.py <agent_pack_dir> <out_dir> [id,id,...]

Red  = Q (Qwen proposal boxes, pixels)
Blue = P (Paddle detection boxes, pixels)
Small textures are upscaled 4x with nearest neighbour.
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.stdout.reconfigure(encoding="utf-8")

pkg = Path(sys.argv[1]).resolve()
out_dir = Path(sys.argv[2]).resolve()
only = set(sys.argv[3].split(",")) if len(sys.argv) > 3 else None

manifest = json.loads((pkg.parents[2] / "manifest.json").read_text(encoding="utf-8"))
data = json.loads((pkg / "input.json").read_text(encoding="utf-8"))
input_root = manifest["input_root"]

meta = {}
for img in manifest["images"]:
    srcs = img.get("sources") or []
    meta[img["image_id"]] = (
        (img["width"], img["height"]),
        srcs[0]["path"] if srcs else "",
    )

out_dir.mkdir(parents=True, exist_ok=True)
for rec in data["records"]:
    rid = str(rec.get("id"))
    if only and rid not in only:
        continue
    ctx = rec.get("context", "")
    iid = Path(ctx).stem
    (w, h), rel = meta.get(iid, ((0, 0), ""))
    if not w or not rel:
        continue
    src = Path(input_root) / rel
    if not src.exists():
        print(f"skip id {rid}: missing {src}")
        continue
    scale = 4 if max(w, h) <= 400 else 1
    im = Image.open(src).convert("RGB")
    im = im.resize((w * scale, h * scale), Image.NEAREST)
    draw = ImageDraw.Draw(im)

    for c in rec.get("paddle") or []:
        x1, y1, x2, y2 = c["box"]
        draw.rectangle([x1 * scale, y1 * scale, x2 * scale, y2 * scale], outline=(0, 90, 255), width=1)

    for p in (rec.get("qwen") or {}).get("proposals") or []:
        x1, y1, x2, y2 = p["box"]
        draw.rectangle([x1 * scale, y1 * scale, x2 * scale, y2 * scale], outline=(255, 0, 0), width=2)

    dest = out_dir / f"{rid}_{Path(rel).stem}_x{scale}.png"
    im.save(dest)
    print(f"id {rid} {rel} {w}x{h} -> {dest.name}")
