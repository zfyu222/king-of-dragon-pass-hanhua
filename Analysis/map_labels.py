# -*- coding: utf-8 -*-
"""List candidate labels for the two map sprites, normalised to 0..1000."""
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
pkg = Path(sys.argv[1])
manifest = json.loads((pkg.parents[2] / "manifest.json").read_text(encoding="utf-8"))
data = json.loads((pkg / "input.json").read_text(encoding="utf-8"))
sizes = {i["image_id"]: (i["width"], i["height"]) for i in manifest["images"]}
for r in data["records"]:
    if r.get("id") not in set(sys.argv[2].split(",")):
        continue
    ctx = r.get("context", "").split("/")[-1].rsplit(".", 1)[0]
    w, h = sizes.get(ctx, (0, 0))
    print(f"=== id {r['id']} image {w}x{h}")
    for c in r.get("paddle") or []:
        x1, y1, x2, y2 = c["box"]
        if not w or not h:
            continue
        print("  P %-18s [%4d,%4d,%4d,%4d]" % (c["text"], x1 / w * 1000, y1 / h * 1000, x2 / w * 1000, y2 / h * 1000))
    for p in (r.get("qwen") or {}).get("proposals") or []:
        x1, y1, x2, y2 = p["box"]
        print("  Q %-18s [%4d,%4d,%4d,%4d]" % (p["text"], x1, y1, x2, y2))
