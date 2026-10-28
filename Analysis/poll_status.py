# -*- coding: utf-8 -*-
"""Compact progress poll for the running review stage."""
import sqlite3
import sys

sys.stdout.reconfigure(encoding="utf-8")
c = sqlite3.connect("file:TranslateFiles/state/workflow.sqlite3?mode=ro", uri=True)

print("review tasks:")
for status, n in c.execute(
        "select status, count(*) from tasks where stage='review' group by status order by 2 desc"):
    print(f"  {status:10s} {n}")

print("review api calls:")
for status, n in c.execute(
        "select c.status, count(*) from api_calls c join tasks t on t.id=c.task_id "
        "where t.stage='review' group by c.status order by 2 desc"):
    print(f"  {status:12s} {n}")

print("records stage:")
for stage, n in c.execute("select stage, count(*) from records group by stage"):
    print(f"  {stage:10s} {n}")

row = c.execute("select value from meta where key='review_generation'").fetchone()
if row:
    print("review_generation:", row[0])
