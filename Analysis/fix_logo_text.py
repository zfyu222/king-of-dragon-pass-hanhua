# -*- coding: utf-8 -*-
"""Swap the two Logo rows so the stacked artwork reads 龙隘 / 之王 (top to bottom)."""
import csv
import json
import shutil
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
proj = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")
src = proj / "king-of-dragon-pass_image_text.csv"
dst = proj / "king-of-dragon-pass_image_text_translated.csv"
backup = proj / "TranslateFiles" / "image-text" / "king-of-dragon-pass_image_text_translated.pre_logo_fix.csv"
shutil.copy2(dst, backup)

rows = list(csv.reader(dst.open(encoding="utf-8-sig", newline="")))
originals = [r[0] for r in csv.reader(src.open(encoding="utf-8-sig", newline=""))]
changed = []
for i, orig in enumerate(originals):
    key = orig.strip().upper()
    if key == "KING OF":
        changed.append((i, rows[i][0], "龙隘"))
        rows[i][0] = "龙隘"
    elif key == "DRAGON PASS" and orig.strip() == "DRAGON PASS":
        changed.append((i, rows[i][0], "之王"))
        rows[i][0] = "之王"

with dst.open("w", encoding="utf-8-sig", newline="") as f:
    csv.writer(f).writerows(rows)

note = {
    "kind": "manual_text_revision",
    "reviewer": "main",
    "reason": "Logo 贴图把 'KING OF' 与 'DRAGON PASS' 拆成两条，机器译文分别为『之王』『龙隘』，按上行/下行堆叠会读成『之王 龙隘』，语义与读序都错。中文标题需自上而下读作『龙隘之王』，故把上行（原 KING OF 区域，窄框）定为『龙隘』、下行（原 DRAGON PASS 区域，宽框）定为『之王』。",
    "changes": [{"row": i + 1, "source": originals[i], "before": b, "after": a} for i, b, a in changed],
    "backup": str(backup),
}
(proj / "TranslateFiles" / "image-text" / "manual_revision_logo.json").write_text(
    json.dumps(note, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(note["changes"], ensure_ascii=False))
