# -*- coding: utf-8 -*-
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
base = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImageText\work\tiered\qwen\1")
jobs = json.loads((base / "queue" / "jobs.json").read_text(encoding="utf-8"))
print("jobs type:", type(jobs).__name__, "len:", len(jobs))
items = jobs if isinstance(jobs, list) else jobs.get("jobs") or []
for item in items:
    if str(item.get("id")) == "31" or item.get("image_id", "").startswith("558e3f9c"):
        print(json.dumps(item, ensure_ascii=False)[:900])
        break
else:
    print("sample:", json.dumps(items[0], ensure_ascii=False)[:600])
