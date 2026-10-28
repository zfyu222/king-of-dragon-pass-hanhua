# -*- coding: utf-8 -*-
import sqlite3
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
db = sqlite3.connect(Path(__file__).resolve().parents[1] / "TranslateFiles" / "state" / "workflow.sqlite3")
db.row_factory = sqlite3.Row
row = db.execute("select id,status,attempt,lease_token,backend,stage from tasks where status='running'").fetchone()
print(dict(row) if row else "no running task")
