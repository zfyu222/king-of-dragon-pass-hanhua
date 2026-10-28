# -*- coding: utf-8 -*-
"""Explain which exclusion rule fails for the residual scan failures."""
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

cfg = json.loads(Path("TranslateFiles/config/workflow.json").read_text(encoding="utf-8"))
rules = []
for i, ex in enumerate(cfg.get("exclusions") or []):
    rules.append((f"exclusion_{i}", re.compile(ex["regex"]), ex.get("info", "")))

report = json.loads(Path("TranslateFiles/reports/full_draft_scan.json").read_text(encoding="utf-8"))
for f in report["failures"]:
    src, tr = f["source"], f["translation"]
    print(f"=== {f['ordinal']} {f['id']}")
    for name, pat, info in rules:
        a = Counter(m.group(0) for m in pat.finditer(src))
        b = Counter(m.group(0) for m in pat.finditer(tr))
        if a != b:
            print(f"  {name} ({info}): src={dict(a)} tr={dict(b)}")
    print()
