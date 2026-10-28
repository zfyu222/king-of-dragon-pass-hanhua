# -*- coding: utf-8 -*-
"""Set escalation_backend to a value this workflow version accepts.

The project was initialised with `escalation_backend: "codex"`, which the current
workflow validates as unsupported (only `subagent` / `api`). Routing is API-only
here, so `api` keeps the behaviour the config intended.
"""
import json
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

path = Path("TranslateFiles/state/workflow.sqlite3")
c = sqlite3.connect(path)
old = json.loads(c.execute("select value from meta where key='config'").fetchone()[0])
print("before:", old.get("escalation_backend"))

new = dict(old)
new["escalation_backend"] = "api"

text = json.dumps(new, ensure_ascii=False)
c.execute("update meta set value=? where key='config'", (text,))
c.execute("insert into events(time, kind, detail) values (datetime('now'), 'configure_escalation', ?)",
          (json.dumps({"before": old.get("escalation_backend"), "after": "api",
                       "reason": "tool rejects 'codex'; API-only routing", "revision_kept": True}),))
c.commit()
c.close()

Path("TranslateFiles/config/workflow.json").write_text(
    json.dumps(new, ensure_ascii=False, indent=2), encoding="utf-8"
)
print("after: api")
