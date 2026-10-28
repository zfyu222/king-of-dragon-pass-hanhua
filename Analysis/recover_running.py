# -*- coding: utf-8 -*-
"""Fail and recover stale running tasks through the official CLI entry points."""
import sqlite3
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
project = Path(__file__).resolve().parents[1]
db_path = project / "TranslateFiles" / "state" / "workflow.sqlite3"
py = Path(r"D:\StaticHanHua\Tools\AINiee\venv\Scripts\python.exe")
flow = Path(r"D:\StaticHanHua\Tools\localization\translation_workflow.py")

db = sqlite3.connect(db_path)
db.row_factory = sqlite3.Row
rows = [dict(r) for r in db.execute("select id,stage,attempt,lease_token from tasks where status='running'")]
db.close()
print("running tasks:", len(rows))
if any(r["lease_token"] is None for r in rows):
    print("WARNING: some running tasks have no lease token; STOP and inspect")

for row in rows:
    if not row["lease_token"]:
        continue
    for cmd in (
        ["fail", "--task", row["id"], "--token", row["lease_token"], "--code", "worker_stopped"],
        ["recover", "--task", row["id"], "--worker-stopped"],
    ):
        out = subprocess.run([str(py), str(flow), "--project", str(project), *cmd],
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        text = (out.stdout or "").strip() or (out.stderr or "").strip()
        print(f"{row['id']} {cmd[0]} -> {text[:160]}")
