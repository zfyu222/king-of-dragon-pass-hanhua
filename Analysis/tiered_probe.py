# -*- coding: utf-8 -*-
import collections
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
qdir = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImageText\work\tiered\qwen\1")
triage = json.loads((qdir / "triage.json").read_text(encoding="utf-8"))
print("triage keys:", list(triage)[:10])
if isinstance(triage, dict):
    for k, v in triage.items():
        if isinstance(v, list):
            print(k, len(v), json.dumps(v[0], ensure_ascii=False)[:250] if v else None)
        elif isinstance(v, dict):
            print(k, len(v), json.dumps(next(iter(v.items())), ensure_ascii=False)[:250])

jobs = json.loads((qdir / "queue" / "jobs.json").read_text(encoding="utf-8"))
print("\njobs keys:", list(jobs)[:10] if isinstance(jobs, dict) else type(jobs))
if isinstance(jobs, dict):
    for k, v in jobs.items():
        print(" ", k, type(v).__name__, len(v) if hasattr(v, "__len__") else v)
replies = qdir / "vision_replies"
print("vision_replies:", len(list(replies.glob("*.json"))) if replies.exists() else 0)
vr = qdir / "local_vlm" / "replies"
print("local replies:", len(list(vr.glob("*.json"))) if vr.exists() else 0)
