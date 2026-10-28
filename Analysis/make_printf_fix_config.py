# -*- coding: utf-8 -*-
"""Build a workflow config with the printf rule excluding Opal \\x01 commands.

`\\x01@% clan` is an Opal command whose next byte is '%' and whose following
word starts with 'c', so the printf regex reads a bogus "% c". A negative
lookbehind keeps real printf tokens intact.
"""
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

src = Path("TranslateFiles/config/workflow.json")
dst = Path("Analysis/out/workflow.printf_neg.json")
cfg = json.loads(src.read_text(encoding="utf-8"))

changed = 0
for ex in cfg["exclusions"]:
    if "printf" in ex.get("info", "") and not ex["regex"].startswith("(?<!"):
        ex["regex"] = "(?<!\\\\x01@)" + ex["regex"]
        changed += 1

dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"changed {changed} rule(s) -> {dst}")
for ex in cfg["exclusions"]:
    if "printf" in ex.get("info", ""):
        print("new regex:", ex["regex"])
