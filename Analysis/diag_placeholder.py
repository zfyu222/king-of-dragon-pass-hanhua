# -*- coding: utf-8 -*-
"""Show which literal becomes [P1] for the two residual scan failures."""
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, r"D:\StaticHanHua\Tools\localization")
import ainiee_bridge as bridge  # noqa: E402

cfg = json.loads(Path("TranslateFiles/config/workflow.json").read_text(encoding="utf-8"))
rules = bridge.patterns(cfg.get("protect_named_curly") is True,
                        cfg.get("exclusions") or [],
                        cfg.get("placeholder_family_overrides") or {})
report = json.loads(Path("TranslateFiles/reports/full_draft_scan.json").read_text(encoding="utf-8"))

for f in report["failures"]:
    src, tr = f["source"], f["translation"]
    masked, mapping = bridge.protect(src, f["id"], rules)
    print(f"=== {f['ordinal']} {f['id']}")
    print("  masked:", masked)
    print("  mapping:", [(m["placeholder"], m["original"]) for m in mapping])
    for m in mapping:
        print(f"    {m['placeholder']} in translation: {tr.count(m['placeholder'])}  original count: {tr.count(m['original'])}")
    print()
