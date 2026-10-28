# -*- coding: utf-8 -*-
"""Build side-by-side (original | patched) previews for the texture patch."""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.stdout.reconfigure(encoding="utf-8")

build = Path(sys.argv[1] if len(sys.argv) > 1 else "ImagePatch/build-v4")
src_root = Path(sys.argv[2] if len(sys.argv) > 2 else "ImageText/input2")
out_dir = build / "compare"
out_dir.mkdir(parents=True, exist_ok=True)

manifest = json.loads((build / "IMAGE_PATCH_MANIFEST.json").read_text(encoding="utf-8"))

for index, entry in enumerate(manifest["files"], start=1):
    iid = entry["image_id"]
    patched = build / entry["path"]
    rel = entry["sources"][0]["path"]
    original = src_root / rel
    if not original.exists():
        print(f"{index}: missing original {original}")
        continue
    a = Image.open(original).convert("RGB")
    b = Image.open(patched).convert("RGB")
    if a.size != b.size:
        b = b.resize(a.size)

    target_h = min(320, a.height)
    scale = target_h / a.height
    size = (max(1, int(a.width * scale)), target_h)
    a = a.resize(size, Image.LANCZOS)
    b = b.resize(size, Image.LANCZOS)

    canvas = Image.new("RGB", (size[0] * 2 + 6, size[1] + 18), (24, 24, 24))
    canvas.paste(a, (0, 18))
    canvas.paste(b, (size[0] + 6, 18))
    draw = ImageDraw.Draw(canvas)
    draw.text((4, 3), f"original {rel}", fill=(220, 220, 220))
    draw.text((size[0] + 10, 3), "patched", fill=(120, 220, 120))
    dest = out_dir / f"{index:02d}_{Path(rel).stem}.png"
    canvas.save(dest, optimize=True)
    print(f"{index:02d} {rel} -> {dest.name} ({dest.stat().st_size // 1024} KB)")
