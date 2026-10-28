# -*- coding: utf-8 -*-
import json
import random
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
p = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\TranslateFiles\reports\pilot_calibration.json")
data = json.loads(p.read_text(encoding="utf-8"))
print("keys:", list(data)[:12])
recs = data.get("records") or data.get("samples") or []
print("records:", len(recs))
if recs:
    print("sample:", json.dumps(recs[0], ensure_ascii=False)[:400])

def field(r, *names):
    for n in names:
        if n in r and r[n]:
            return r[n]
    return ""

rows = []
for r in recs:
    src = field(r, "source", "text", "src")
    dst = field(r, "translation", "target", "draft", "zh")
    rows.append((src, dst, r.get("row")))

rnd = random.Random(11)
longs = [x for x in rows if len(x[0]) > 200]
shorts = [x for x in rows if 20 <= len(x[0]) <= 80]
print("\n=== 长句 8 ===")
for s, t, row in rnd.sample(longs, min(8, len(longs))):
    print(f"[{row}]\nEN: {s[:300]}\nCN: {t[:300]}\n")
print("=== 短句 12 ===")
for s, t, row in rnd.sample(shorts, min(12, len(shorts))):
    print(f"[{row}] {s} => {t}")
