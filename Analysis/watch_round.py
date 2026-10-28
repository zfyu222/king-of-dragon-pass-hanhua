# -*- coding: utf-8 -*-
"""Drive an API stage to completion: run, recover stale in-flight tasks, repeat."""
import json
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

PROJECT = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
PY = Path(r"D:\StaticHanHua\Tools\AINiee\venv\Scripts\python.exe")
ROUND = Path(r"D:\StaticHanHua\Tools\localization\run_api_round.py")
FLOW = Path(r"D:\StaticHanHua\Tools\localization\translation_workflow.py")
DB = PROJECT / "TranslateFiles" / "state" / "workflow.sqlite3"


def call(args):
    return subprocess.run([str(PY), str(FLOW), "--project", str(PROJECT), *args],
                          capture_output=True, text=True, encoding="utf-8", errors="replace")


def recover_stale():
    db = sqlite3.connect(DB)
    db.row_factory = sqlite3.Row
    rows = [dict(r) for r in db.execute("select id, lease_token from tasks where status='running'")]
    db.close()
    for row in rows:
        if not row["lease_token"]:
            print(f"[watch] running task without lease: {row['id']}", flush=True)
            continue
        call(["fail", "--task", row["id"], "--token", row["lease_token"], "--code", "worker_stopped"])
        out = call(["recover", "--task", row["id"], "--worker-stopped"])
        print(f"[watch] recovered {row['id']}: {(out.stdout or out.stderr).strip()[:120]}", flush=True)
    return len(rows)


def main():
    stage = sys.argv[1]
    max_cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 15
    for cycle in range(1, max_cycles + 1):
        print(f"[watch] === cycle {cycle} stage={stage} ===", flush=True)
        res = subprocess.run([str(PY), str(ROUND), "--project", str(PROJECT), "--stage", stage, "--resume"],
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        tail = (res.stdout or "").strip().splitlines()
        payload = None
        for line in reversed(tail):
            line = line.strip()
            if line.startswith("{"):
                try:
                    payload = json.loads(line)
                    break
                except json.JSONDecodeError:
                    continue
        if not payload:
            print("[watch] no receipt json; stderr:", (res.stderr or "")[-500:], flush=True)
            time.sleep(20)
            recover_stale()
            continue
        print("[watch] status={} stage_clean={} accepted={} failed={} attn={}".format(
            payload.get("status"), payload.get("stage_clean"), payload.get("accepted"),
            payload.get("failed"), payload.get("needs_attention_count")), flush=True)
        if payload.get("stage_clean"):
            print("[watch] STAGE CLEAN", flush=True)
            return 0
        recovered = recover_stale()
        print(f"[watch] recovered {recovered} stale task(s); continuing", flush=True)
        if recovered == 0 and payload.get("needs_attention_count") == 0:
            print("[watch] nothing to recover and no attention item; inspecting status", flush=True)
            time.sleep(30)
    print("[watch] max cycles reached", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
