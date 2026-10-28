# -*- coding: utf-8 -*-
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
pkg = Path(sys.argv[1])
data = json.loads((pkg / "input.json").read_text(encoding="utf-8"))
recs = data["records"]
print("records:", len(recs), "type0:", type(recs[0]).__name__)
for r in recs:
    if isinstance(r, str):
        print("STR:", r[:200])
        continue
    iid = r.get("id")
    img = r.get("image") or {}
    path = (img.get("sources") or [{}])[0].get("path") if isinstance(img, dict) else None
    cands = r.get("candidates") or r.get("paddle") or []
    txts = []
    for c in cands if isinstance(cands, list) else []:
        if isinstance(c, dict):
            txts.append(f"{c.get('text')}({c.get('score')})")
        else:
            txts.append(str(c))
    print(f"{iid} | {path} | {' / '.join(txts)[:120]}")
