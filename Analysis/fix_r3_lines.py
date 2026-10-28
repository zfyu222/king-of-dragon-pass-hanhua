# -*- coding: utf-8 -*-
"""Repair the 23 control-code/marker defects in the r3 retranslation.

Each fix keeps the Opal command multiset, control-byte multiset, literal escape
multiset and random-phrase slash count identical to the source line; the guard
below refuses to write any row that still violates them.
"""
import csv
import json
import re
import sqlite3
import sys
from collections import Counter
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

OPAL_COMMAND = re.compile(r"\\x01(?:(?:\\x[0-9A-Fa-f]{2})|[^\r\n]){2}")
OPAL_BYTE = re.compile(r"\\x[0-9A-Fa-f]{2}")
LITERAL = re.compile(r"\\[nrt]")

FIXES = {
    4378: "\\x05是\\x01Bs的好朋友，而她若不给氏族惹麻烦就不痛快。",
    4438: "不幸的是，\\x01Bp确实比\\x01@\\x03更有领导力，而且如果我们不要求\\x01@\\x03辞职，他就能维持一个强大的、反对\\x01@\\x03的派系。",
    8359: "\\x02\\x03这个氏族，这些贪婪的人/我们真正需要的/上次发生这种事时，它只是\\x02——我想说的是，\\x02\\x03他们如此重要/让我们抬起头来/辛苦劳作并不是\\x02——你不能为此怪他们。",
    8444: "氏族议事会围在\\x01@\\x03身边；此人又因旧伤倒下。医者说，那伤口从未完全愈合，这一次会夺走\\x04性命。",
    8452: "议事圈悲恸地围在\\x01@\\x03周围；此人被一头怪异的混沌怪物击倒，那怪物\\x02\\x06长着巨型蜗牛的身体和龙的头/形似鹿却长着巨魔的脸/长着蝎子的身体和人的头/藏在池塘底部/住在我们的一头牛体内/潜伏在我们图拉边缘的树林间\\x02。",
    9604: "\\x02\\x03烦扰/持续的头痛/反复争斗\\x02。",
    9607: "他们\\x02\\x03强大/强壮/受魔法强化\\x02，而我们并非如此；交出奴隶吧。",
    10272: "我们与\\x01@\\x13谈过，此人是\\x01@!氏族的首领，想知道\\x06是否愿意加入我们的部族。\\x05问我们愿意向\\x08提供什么。",
    10290: "如果部族在没有\\x01@!氏族的情况下成立，该氏族有理由感到恐惧。",
    12184: "\\x02\\x03偿付/清偿/补偿\\x02他们，让这件尴尬的事就此了结。",
    17200: "斥责\\x01D\\x07，因其令氏族蒙羞。",
    17201: "我们斥责了\\x01D\\x07，因其令氏族蒙羞。",
    19018: "其中一人说：“杀了他，我要他的睾丸。”\\x01@t说他的指控者也是个诡术师，于是那人说：“现在我还要他的舌头。”他们把\\x01@t吊在树上，将他残害。",
    19019: "其中一人说：“杀了她，我要她的耳朵和脚趾。”\\x01@t说她的指控者也是个诡术师，于是那男人说：“现在我还要她的舌头。”他们把\\x01@t吊在树上，将她残害。",
    21502: "\\x01@\\nton",
    21503: "\\x01@\\r顿",
    23629: "\\x01@\\x05率领队伍。我们离开隧道后，此人看上去安然无恙。但不久之后，\\x06吐出一团绿色、蠕动的东西，随后很快死去。",
    25762: "第一场比试由外邦人提出。这是一场看谁能忍受最大痛苦的比试。外邦人毫不畏缩地将矛头刺穿自己胸部的肌肉，然后把另一支矛递给\\x01@K，指望此人照样去做。",
    26221: "“有听话的奴隶是好事，”奥兰斯说，然后把\\x01@K拖走，让此人永远在诸神领域服侍他。\\x01@K此后再也没有见到\\x04族人。神语者说，我们的氏族魔法会变弱，今后数日乃至数周，恐怕还会有别的灾祸降临到我们头上。",
    26222: "奥兰斯把\\x01@K拖走，让此人作为奴隶永远在诸神领域服侍他。\\x01@K此后再也没有见到\\x04族人。神语者预言氏族魔法会变弱，还会有其他灾祸。",
    26271: "奥兰斯打伤了\\x01@K，此人因此被逐出诸神领域。医者认为\\x01@K的伤会致命，但\\x01@K没有死。神语者说，我们的氏族魔法也受到了重创。",
    26344: "兰科尔米拒绝了，说他不信任\\x01@K，毕竟当初就是此人弄丢了真理。",
    27542: "当\\x01@\\x03试图让\\x01@\\x14立下受缚者誓言时，此人却一次次忍不住狂笑。最后他脱下裤子，蹲下，在氏族大厅里拉了一坨屎。围观者被一股恶臭熏得连连后退，他却跑掉了，再也没人见过他。",
}

ms = lambda pattern, text: Counter(m.group(0) for m in pattern.finditer(text))


def violations(original, translation):
    out = []
    if ms(OPAL_COMMAND, original) != ms(OPAL_COMMAND, translation):
        out.append("cmds")
    if ms(OPAL_BYTE, original) != ms(OPAL_BYTE, translation):
        out.append("bytes")
    if ms(LITERAL, original) != ms(LITERAL, translation):
        out.append("literal")
    if "\\x02" in original and original.count("/") != translation.count("/"):
        out.append("slash")
    return out


src_path = Path(sys.argv[1] if len(sys.argv) > 1 else "king-of-dragon-pass_text.csv")
db_path = Path(sys.argv[2] if len(sys.argv) > 2 else "TranslateFiles/state/workflow.sqlite3")
log_path = Path(sys.argv[3] if len(sys.argv) > 3 else "TranslateFiles/results/r3_format_fix_log.json")
apply = "--apply" in sys.argv

source = [r[0] for r in csv.reader(src_path.open(encoding="utf-8"))]
log = []
con = sqlite3.connect(db_path)
con.row_factory = sqlite3.Row

for line, new_text in sorted(FIXES.items()):
    original = source[line - 1]
    row = con.execute(
        "select id, translation from records where ordinal = ?", (line,)
    ).fetchone()
    if row is None:
        print(f"line {line}: no record, skipped")
        continue
    bad = violations(original, new_text)
    status = "ok" if not bad else "VIOLATION:" + ",".join(bad)
    print(f"line {line} {row['id']} {status}")
    log.append(
        {
            "line": line,
            "record_id": row["id"],
            "source": original,
            "before": row["translation"],
            "after": new_text,
            "status": status,
        }
    )
    if apply and not bad:
        con.execute(
            "update records set translation = ? where id = ?", (new_text, row["id"])
        )
        con.execute(
            "update draft_candidates set translation = ? where record_id = ?",
            (new_text, row["id"]),
        )

if apply:
    con.commit()
    print("committed")
con.close()
log_path.write_text(json.dumps(log, ensure_ascii=False, indent=1), encoding="utf-8")
print(f"log -> {log_path}")
