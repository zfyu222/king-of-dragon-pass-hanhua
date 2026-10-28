# -*- coding: utf-8 -*-
"""Quick read-only statistics for TranslateFiles/state/workflow.sqlite3."""
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
db = Path(sys.argv[1] if len(sys.argv) > 1 else "TranslateFiles/state/workflow.sqlite3")
con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)

tables = [r[0] for r in con.execute("select name from sqlite_master where type='table' order by name")]
for t in tables:
    cols = [r[1] for r in con.execute(f'pragma table_info("{t}")')]
    n = con.execute(f'select count(*) from "{t}"').fetchone()[0]
    print(f"{t} ({n}) :: {', '.join(cols)}")

for t in tables:
    cols = [r[1] for r in con.execute(f'pragma table_info("{t}")')]
    group_cols = [c for c in ("stage", "status", "state", "kind", "phase") if c in cols]
    if not group_cols:
        continue
    sel = ", ".join(group_cols)
    print(f"\n== {t} by {sel}")
    for row in con.execute(f'select {sel}, count(*) from "{t}" group by {sel} order by 1, 2'):
        print("  ", row)
