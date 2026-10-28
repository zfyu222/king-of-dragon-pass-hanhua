# -*- coding: utf-8 -*-
"""Restore the pre-change workflow config (printf rule untouched).

The config hash feeds the pilot approval key, so the file and the DB copy are
written back verbatim from the backup taken before the experiment.
"""
import json
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

bak = Path("TranslateFiles/backups/workflow.sqlite3.bak_before_formatfix_20261028")
b = sqlite3.connect(f"file:{bak}?mode=ro", uri=True)
cfg_text = b.execute("select value from meta where key='config'").fetchone()[0]
b.close()

cfg = json.loads(cfg_text)
Path("TranslateFiles/config/workflow.json").write_text(
    json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8"
)

c = sqlite3.connect("TranslateFiles/state/workflow.sqlite3")
c.execute("update meta set value=? where key='config'", (cfg_text,))
c.commit()
c.close()

for ex in cfg["exclusions"]:
    if "printf" in ex.get("info", ""):
        print("restored printf regex:", ex["regex"])
print("config restored")
