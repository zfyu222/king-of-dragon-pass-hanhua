# -*- coding: utf-8 -*-
import json
import statistics
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
work = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImageText\work")
manifest = json.loads((work / "manifest.json").read_text(encoding="utf-8"))
scan = json.loads((work / "scan.json").read_text(encoding="utf-8"))
imgs = manifest["images"]

path_map = {}
for im in imgs:
    for src in im["sources"]:
        path_map[src["path"].replace("\\", "/")] = im["image_id"]

scan_imgs = scan["images"]
print("scan images type:", type(scan_imgs).__name__, "len:", len(scan_imgs))
first = scan_imgs[0] if isinstance(scan_imgs, list) else next(iter(scan_imgs.items()))
print("scan item sample:", json.dumps(first, ensure_ascii=False)[:500])

targets = ["Menu/Logo.png", "Menu/Logo@2x.png", "M/ClanButton.png", "M/MapArch.png"]
# normalize scan into dict by image_id
if isinstance(scan_imgs, list):
    by_id = {item.get("image_id"): item for item in scan_imgs}
else:
    by_id = scan_imgs

heights = []
for im in scan_imgs if isinstance(scan_imgs, list) else scan_imgs.values():
    for reg in (im.get("regions") or []):
        box = reg.get("box") or reg.get("rect")
        if box and len(box) == 4:
            heights.append(box[3] - box[1])

print("regions heights count:", len(heights))
if heights:
    heights.sort()
    q = statistics.quantiles(heights, n=10)
    print("height p10/p50/p90:", heights[0], round(q[0]), round(statistics.median(heights)), round(q[-1]), heights[-1])

for t in targets:
    iid = path_map.get(t)
    item = by_id.get(iid) if iid else None
    print("\n", t, "->", iid)
    if item:
        print("  status:", item.get("status"))
        for reg in (item.get("regions") or []):
            print("   ", json.dumps(reg, ensure_ascii=False)[:220])
