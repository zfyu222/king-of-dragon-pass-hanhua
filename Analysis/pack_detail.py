# -*- coding: utf-8 -*-
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
pkg = Path(sys.argv[1])
ids = set(sys.argv[2].split(","))
data = json.loads((pkg / "input.json").read_text(encoding="utf-8"))
for r in data["records"]:
    if r.get("id") not in ids:
        continue
    print("=== id", r["id"], "| image:", r.get("image"), "| ctx:", r.get("context"))
    print("  paddle:", json.dumps(r.get("paddle"), ensure_ascii=False)[:600])
    q = r.get("qwen") or {}
    print("  qwen presence:", q.get("presence"), "| reason:", str(q.get("reason"))[:200])
    for p in (q.get("proposals") or []):
        print("    proposal:", p.get("box"), "|", p.get("text"))
