# -*- coding: utf-8 -*-
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
out = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass\ImageText\work")
import importlib.util

spec = importlib.util.spec_from_file_location("tiered", r"D:\StaticHanHua\Tools\image_text\tiered.py")
tiered = importlib.util.module_from_spec(spec)
sys.path.insert(0, r"D:\StaticHanHua\Tools\image_text")
spec.loader.exec_module(tiered)

m, s, p = tiered.load_plan(out)[1:]
decisions = json.loads((out / "tiered" / "decisions.json").read_text(encoding="utf-8"))
findings = json.loads((out / "tiered" / "findings.json").read_text(encoding="utf-8"))
expected = {x["image_id"] for x in p["routes"] if x["route"] != "excluded"}
print("routes:", len(p["routes"]), "expected:", len(expected), "decisions:", len(decisions["images"]))
for row in p["routes"]:
    v = findings["images"].get(row["image_id"])
    if row["reason"].endswith("_audit") and row["route"] == "qwen" and v and v["presence"] == "no_text" and row["reason"] != "ordinary_audit":
        expected.discard(row["image_id"])
unresolved = expected - set(decisions["images"])
print("unresolved:", len(unresolved))
routes = {x["image_id"]: x for x in p["routes"]}
import collections

c = collections.Counter((routes[i]["route"], routes[i]["reason"]) for i in unresolved)
print(c.most_common(10))
paths = {}
for img in m["images"]:
    paths[img["image_id"]] = [x["path"] for x in img["sources"]]
for i in list(unresolved)[:25]:
    print(" ", i[:12], routes[i]["route"], routes[i]["reason"], paths.get(i))
