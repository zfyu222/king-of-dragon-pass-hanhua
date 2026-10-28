# -*- coding: utf-8 -*-
import collections
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
path = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\TranslateFiles\glossary\r3\freeze_c04a1e8d\built\glossary_reviewed.json")
data = json.loads(path.read_text(encoding="utf-8"))
terms = data["terms"]
chars = data.get("characters") or []
print("terms:", len(terms), "characters:", len(chars))
print("kinds:", collections.Counter(t.get("term_kind") for t in terms).most_common(8))
print("enforcement:", collections.Counter(t.get("enforcement") for t in terms).most_common(5))
print("categories:", collections.Counter(t.get("category_path") for t in terms).most_common(12))
if chars:
    print("char sample:", json.dumps(chars[0], ensure_ascii=False)[:300])

index = {}
for t in terms:
    index.setdefault(t["source"].lower(), []).append(t.get("recommended_translation"))
for c in chars:
    src = c.get("source") or c.get("name")
    if src:
        index.setdefault(src.lower(), []).append(c.get("recommended_translation") or c.get("translation"))

for n in ["Orlanth", "Ernalda", "Eurmal", "Elmal", "Humakt", "Issaries", "Lhankor Mhy", "Chalana Arroy", "Kero Fin",
          "Dragon Pass", "Tarsh", "Lunar Empire", "Waha", "Kolat", "Storm Bull", "Sartar", "Heort", "Glorantha",
          "Tula", "Thrall", "Cow", "Clan", "Tribe"]:
    print(f"  {n:16s} -> {index.get(n.lower())}")
