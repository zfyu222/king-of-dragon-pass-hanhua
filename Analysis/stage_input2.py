# -*- coding: utf-8 -*-
"""Collect the confirmed image sources into a small, precise re-confirmation input set."""
import json
import shutil
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
proj = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
manifest = json.loads((proj / "ImageText" / "work" / "manifest.json").read_text(encoding="utf-8"))
payload = json.loads((proj / "ImagePatch" / "build-v4" / "IMAGE_PATCH_MANIFEST.json").read_text(encoding="utf-8"))
wanted = {e["image_id"] for e in payload["entries"]}
out = proj / "ImageText" / "input2"
if out.exists():
    raise SystemExit("input2 already exists")
count = 0
for img in manifest["images"]:
    if img["image_id"] not in wanted:
        continue
    for src in img["sources"]:
        rel = Path(src["path"])
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(proj / "Game" / "KoDp" / "textures" / rel, target)
        count += 1
print("copied", count, "files for", len(wanted), "images")
