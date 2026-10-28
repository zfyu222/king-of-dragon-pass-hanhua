# -*- coding: utf-8 -*-
"""Translation coverage / recent activity for TranslateFiles/state/workflow.sqlite3."""
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
db = Path(sys.argv[1] if len(sys.argv) > 1 else "TranslateFiles/state/workflow.sqlite3")
con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)

q = lambda s: con.execute(s).fetchone()[0]

print("records            ", q("select count(*) from records"))
print("  translation empty", q("select count(*) from records where translation is null or trim(translation)=''"))
print("  == source        ", q("select count(*) from records where translation = source"))
print("  stage translate  ", q("select count(*) from records where stage='translate'"))
print("  locked           ", q("select count(*) from records where locked=1"))
print("  exceptions       ", q("select count(*) from records where exceptions is not null and exceptions != ''"))

print("\ndraft_candidates   ", q("select count(*) from draft_candidates"))
print("  with translation ", q("select count(*) from draft_candidates where translation is not null and trim(translation) != ''"))

print("\nmeta:")
for k, v in con.execute("select key, value from meta order by key"):
    print(f"  {k} = {str(v)[:120]}")

print("\nlast events:")
for row in con.execute("select time, kind, substr(detail,1,100) from events order by id desc limit 12"):
    print("  ", row)
