# -*- coding: utf-8 -*-
"""Export OCR candidates for an agent pack to a table (normalised + pixel boxes).

Usage:
    python label_table.py <agent_pack_dir> [out.csv] [id,id,...]

P (paddle) boxes are stored in pixels and printed both ways.
Q (qwen proposals) boxes are already pixels; normalised values are derived.
"""
import csv
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

pkg = Path(sys.argv[1]).resolve()
out_csv = Path(sys.argv[2]) if len(sys.argv) > 2 else None
only = set(sys.argv[3].split(",")) if len(sys.argv) > 3 else None

manifest = json.loads((pkg.parents[2] / "manifest.json").read_text(encoding="utf-8"))
data = json.loads((pkg / "input.json").read_text(encoding="utf-8"))

sizes = {}
names = {}
for img in manifest["images"]:
    iid = img["image_id"]
    sizes[iid] = (img["width"], img["height"])
    srcs = img.get("sources") or []
    names[iid] = srcs[0]["path"] if srcs else ""

rows = []
for rec in data["records"]:
    rid = str(rec.get("id"))
    if only and rid not in only:
        continue
    ctx = rec.get("context", "")
    iid = Path(ctx).stem
    w, h = sizes.get(iid, (0, 0))
    name = names.get(iid) or ctx.split("/")[-1]
    if not w or not h:
        continue
    cands = []
    for c in rec.get("paddle") or []:
        x1, y1, x2, y2 = c["box"]
        cands.append(("P", c["text"], (x1, y1, x2, y2)))
    for p in (rec.get("qwen") or {}).get("proposals") or []:
        x1, y1, x2, y2 = p["box"]
        cands.append(("Q", p["text"], (x1, y1, x2, y2)))
    if not cands:
        continue
    print(f"=== id {rid} {name} ({w}x{h})")
    for tag, text, (x1, y1, x2, y2) in cands:
        nx1, ny1 = x1 / w * 1000, y1 / h * 1000
        nx2, ny2 = x2 / w * 1000, y2 / h * 1000
        print(
            "  %s %-22s norm[%4d,%4d,%4d,%4d] px[%4d,%4d,%4d,%4d]"
            % (tag, text[:22], nx1, ny1, nx2, ny2, x1, y1, x2, y2)
        )
        rows.append(
            {
                "id": rid,
                "image": name,
                "width": w,
                "height": h,
                "source": tag,
                "text": text,
                "norm_x1": round(nx1),
                "norm_y1": round(ny1),
                "norm_x2": round(nx2),
                "norm_y2": round(ny2),
                "px_x1": x1,
                "px_y1": y1,
                "px_x2": x2,
                "px_y2": y2,
            }
        )

if out_csv:
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    with out_csv.open("w", encoding="utf-8-sig", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(rows[0].keys()) if rows else [])
        if rows:
            writer.writeheader()
            writer.writerows(rows)
    print(f"\nwrote {len(rows)} rows -> {out_csv}")
