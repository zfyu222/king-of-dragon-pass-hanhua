# -*- coding: utf-8 -*-
"""Inspect leftover API reservations so a later resume starts from a known state."""
import sqlite3
import sys

sys.stdout.reconfigure(encoding="utf-8")
c = sqlite3.connect("file:TranslateFiles/state/workflow.sqlite3?mode=ro", uri=True)

print("== reserved api_calls")
for row in c.execute(
        "select session_id, count(*), min(time), max(time) from api_calls "
        "where status='reserved' group by session_id"):
    print("  ", row)

print("== batch_runs waiting / needs_attention")
for row in c.execute(
        "select id, token, worker, scope, status from batch_runs "
        "where status in ('waiting','needs_attention','interrupted') order by rowid desc limit 8"):
    print("  ", row)

print("== running tasks (lease)")
for row in c.execute(
        "select id, status, lease_until, backend from tasks where status='running' and stage='review' limit 12"):
    print("  ", row)
