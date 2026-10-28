# -*- coding: utf-8 -*-
"""Dump the current retranslation from the workflow DB to a one-column CSV.

Writes a scratch file only; never touches the shipped *_text_translated.csv.
"""
import csv
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

db = Path(sys.argv[1])
out = Path(sys.argv[2])

con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
rows = con.execute("select ordinal, translation from records order by ordinal").fetchall()
out.parent.mkdir(parents=True, exist_ok=True)
with out.open("w", encoding="utf-8", newline="") as fh:
    w = csv.writer(fh)
    for _, tr in rows:
        w.writerow([tr])
print(f"dumped {len(rows)} rows -> {out}")
