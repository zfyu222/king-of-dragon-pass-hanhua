# -*- coding: utf-8 -*-
"""Roll back the stage/revision/approval fields that `configure` reset.

The config change itself is reverted in workflow.json; nothing else in the
translation state is touched, so all repair work is preserved.
"""
import json
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

cur = Path("TranslateFiles/state/workflow.sqlite3")
bak = Path("TranslateFiles/backups/workflow.sqlite3.bak_before_formatfix_20261028")

b = sqlite3.connect(f"file:{bak}?mode=ro", uri=True)
approvals = b.execute("select value from meta where key='quality_approvals'").fetchone()[0]
revision = b.execute("select value from meta where key='revision'").fetchone()[0]
b.close()

c = sqlite3.connect(cur)
n = c.execute("update records set stage='translate' where stage='source'").rowcount
c.execute("update meta set value=? where key='quality_approvals'", (approvals,))
c.execute("update meta set value=? where key='revision'", (revision,))
c.commit()

print(f"records restored: {n}")
print("revision:", c.execute("select value from meta where key='revision'").fetchone()[0])
print("approvals restored:", bool(json.loads(c.execute("select value from meta where key='quality_approvals'").fetchone()[0])))
for r in c.execute("select stage,count(*) from records group by stage"):
    print(" ", r)
c.close()
