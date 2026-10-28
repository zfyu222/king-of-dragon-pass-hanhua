# -*- coding: utf-8 -*-
"""Apply hand-written translations to the workflow DB after rule checks.

Usage: python apply_record_fixes.py <fixes.json>
fixes.json = {"<ordinal>": "<new translation>", ...}
"""
import csv
import json
import re
import sqlite3
import sys
from collections import Counter
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

fixes = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
source = [r[0] for r in csv.reader(Path("king-of-dragon-pass_text.csv").open(encoding="utf-8"))]
cfg = json.loads(Path("TranslateFiles/config/workflow.json").read_text(encoding="utf-8"))
rules = [(f"exclusion_{i}", re.compile(ex["regex"])) for i, ex in enumerate(cfg["exclusions"])]

def counts(pattern, text):
    return Counter(m.group(0) for m in pattern.finditer(text))

con = sqlite3.connect("TranslateFiles/state/workflow.sqlite3")
con.row_factory = sqlite3.Row
ok = True
for ordinal, text in fixes.items():
    n = int(ordinal)
    src = source[n - 1]
    bad = [name for name, pat in rules if counts(pat, src) != counts(pat, text)]
    row = con.execute("select id from records where ordinal=?", (n,)).fetchone()
    if row is None:
        print(f"{n}: missing record"); ok = False; continue
    print(f"{n} {row['id']} {'ok' if not bad else 'FAIL ' + ','.join(bad)}")
    if bad:
        ok = False
        continue
    con.execute("update records set translation=? where id=?", (text, row["id"]))
    con.execute("update draft_candidates set translation=? where record_id=?", (text, row["id"]))
if ok:
    con.commit()
    print("committed")
else:
    print("no changes committed")
con.close()
