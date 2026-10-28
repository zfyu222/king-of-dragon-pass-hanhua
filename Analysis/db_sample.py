# -*- coding: utf-8 -*-
"""Sample untranslated (translation == source) rows and exception payloads."""
import sqlite3
import sys

sys.stdout.reconfigure(encoding="utf-8")
c = sqlite3.connect("file:TranslateFiles/state/workflow.sqlite3?mode=ro", uri=True)

print("== translation == source (sample)")
rows = c.execute(
    "select id, source, exceptions from records where translation = source limit 8"
).fetchall()
for rid, src, exc in rows:
    print(f"  {rid} | {src[:70]!r} | exceptions={str(exc)[:50]}")

print("\n== exceptions column sample")
for (exc,) in c.execute("select exceptions from records limit 3"):
    print("  ", str(exc)[:150])

print("\n== longest sources (spot check quality)")
for rid, src, tr in c.execute(
    "select id, source, translation from records order by length(source) desc limit 3"
):
    print(f"  {rid}\n    EN: {src[:180]}\n    ZH: {tr[:180]}")
