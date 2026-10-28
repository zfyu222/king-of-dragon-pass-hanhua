# -*- coding: utf-8 -*-
"""Pick one box per distinct label text (Q preferred over P) and print a table.

Usage:
    python pick_boxes.py <labels.csv> [out.csv]

Rules:
  * Q (Qwen) boxes are text-line tight -> preferred.
  * P (Paddle) box is used when no Q candidate carries the same text.
  * Rows whose text looks like noise (no letters, or < 3 chars) are reported apart.
"""
import csv
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

src = Path(sys.argv[1])
out = Path(sys.argv[2]) if len(sys.argv) > 2 else None

rows = list(csv.DictReader(src.open(encoding="utf-8-sig")))
noise = re.compile(r"^[^A-Za-z]*$|^.{0,2}$")

best = {}
for r in rows:
    text = r["text"].strip()
    key = (r["id"], re.sub(r"\s+", " ", text).lower())
    rank = 0 if r["source"] == "Q" else 1
    if key not in best or rank < best[key][0]:
        best[key] = (rank, r)

keep, drop = [], []
for (rid, _), (_, r) in best.items():
    (drop if noise.search(r["text"].strip()) else keep).append(r)

order = lambda r: (int(r["id"]), int(r["norm_y1"]), int(r["norm_x1"]))
keep.sort(key=order)
drop.sort(key=order)

print(f"{len(keep)} text labels, {len(drop)} noise rows")
for r in keep:
    print(
        "id %-2s %-16s %-3s norm[%4s,%4s,%4s,%4s] px[%5s,%5s,%5s,%5s]"
        % (r["id"], r["text"][:16], r["source"], r["norm_x1"], r["norm_y1"],
           r["norm_x2"], r["norm_y2"], r["px_x1"], r["px_y1"], r["px_x2"], r["px_y2"])
    )
if drop:
    print("-- noise --")
    print(", ".join(f"{r['id']}:{r['text']!r}" for r in drop))

if out:
    with out.open("w", encoding="utf-8-sig", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(keep)
    print(f"wrote {out}")
