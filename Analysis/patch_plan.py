# -*- coding: utf-8 -*-
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
proj = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
man = json.loads((proj / "ImageText" / "work" / "manifest.json").read_text(encoding="utf-8"))
sizes = {i["image_id"]: (i["width"], i["height"]) for i in man["images"]}
paths = {i["image_id"]: [s["path"] for s in i["sources"]] for i in man["images"]}
payload = json.loads((proj / "ImagePatch" / "build-v1" / "IMAGE_PATCH_MANIFEST.json").read_text(encoding="utf-8"))
by = {}
for e in payload["entries"]:
    by.setdefault(e["image_id"], []).append(e)
for iid, rows in by.items():
    w, h = sizes[iid]
    print(f"=== {iid[:12]} {w}x{h} regions={len(rows)} src={paths[iid]}")
    for e in rows[:6]:
        x1, y1, x2, y2 = e["box"]
        print("   %-30s box=%.0fx%.0f" % (e["text"][:30], x2 - x1, y2 - y1))
    if len(rows) > 6:
        print(f"   ... {len(rows) - 6} more")
