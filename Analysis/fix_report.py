# -*- coding: utf-8 -*-
"""Show ordered control-token sequences for rows flagged by the validator."""
import csv
import re
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

OPAL_COMMAND = re.compile(r"\\x01(?:(?:\\x[0-9A-Fa-f]{2})|[^\r\n]){2}")
OPAL_BYTE = re.compile(r"\\x[0-9A-Fa-f]{2}")
LITERAL = re.compile(r"\\[nrt]")

rows = list(csv.DictReader(Path(sys.argv[1]).open(encoding="utf-8-sig")))
for r in rows:
    src, tr = r["原文"], r["当前译文"]
    print(f"### line {r['行号']}  {r['问题']}")
    print("  src cmds:", [m.group(0) for m in OPAL_COMMAND.finditer(src)])
    print("  tr  cmds:", [m.group(0) for m in OPAL_COMMAND.finditer(tr)])
    print("  src byte:", [m.group(0) for m in OPAL_BYTE.finditer(src)])
    print("  tr  byte:", [m.group(0) for m in OPAL_BYTE.finditer(tr)])
    ls, lt = [m.group(0) for m in LITERAL.finditer(src)], [m.group(0) for m in LITERAL.finditer(tr)]
    if ls or lt:
        print("  src lit :", ls, " tr lit:", lt)
    if src.count("/") != tr.count("/"):
        print("  slash   :", src.count("/"), "->", tr.count("/"))
    print()
