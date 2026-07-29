# -*- coding: utf-8 -*-
"""Repair the reviewed formatting defects in the first AINiee translation."""

import csv
import re
import shutil
from pathlib import Path


TRANSLATION = Path("king-of-dragon-pass_text_translated.csv")
BACKUP = Path("king-of-dragon-pass_text_translated.csv.pre_format_fix.bak")
RANDOM_BLOCK = re.compile(r"(\\x02\\x[0-9A-Fa-f]{2})(.*?)(\\x02)")


def replace_once(text, old, new, row_number):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"第 {row_number} 行预期替换内容出现 {count} 次，而不是 1 次: {old!r}"
        )
    return text.replace(old, new, 1)


def main():
    with TRANSLATION.open("r", encoding="utf-8-sig", newline="") as file:
        rows = [row[0] for row in csv.reader(file)]

    if len(rows) != 34421:
        raise RuntimeError(f"译文应有 34421 行，实际为 {len(rows)} 行")

    if not BACKUP.exists():
        shutil.copy2(TRANSLATION, BACKUP)

    # AINiee converted alternative delimiters to full-width slashes.
    rows = [
        RANDOM_BLOCK.sub(
            lambda match: match.group(1)
            + match.group(2).replace("／", "/")
            + match.group(3),
            text,
        )
        for text in rows
    ]

    repairs = {
        120: [
            (r"\x02\x03/我们若", r"\x02\x03我们若"),
        ],
        447: [
            (
                r"\x02\x02向\x02 \x01@6骑手的侧翼猛冲",
                r"对\x01@6骑手\x02\x02发起正面猛攻/发动侧翼突击\x02。",
            ),
        ],
        636: [(r"\x01@[\n", r"\x01@\n")],
        1501: [(r"\x01@[\n", r"\x01@\n")],
        1691: [("/大型无翼鸟", "")],
        2057: [("[P23]", r"\x06")],
        3709: [(r"，\x01BX 被杀死", "，后者被杀死")],
        4731: [
            (
                r"“这是\n的 \x01@\x12  \x01@[！",
                r"“这是\x01@\x12的\x01@\n！",
            ),
        ],
        4732: [(r"\n\x01@[", r"\x01@\n")],
        8391: [("。认为这条亚龙正蹲在", r"。\x01@I认为这条亚龙正蹲在")],
        17110: [
            (
                r"\x01B{ \x02\x03玛利亚自从在那场宅院火灾中被困以来，就一直/头脑不正常/说过其他奇怪的话做过其他奇怪的事/遭受了某种灵魂衰败\x02 。",
                r"\x01B{自从在那场宅院火灾中被困以来，\x02\x03精神一直不正常/还说过并做过其他怪事/遭受了某种灵魂衰败\x02。",
            ),
        ],
        19230: [
            (
                r"\x02\x03我们必须向 \x01@a 部落/复仇/进行报复/发起攻击\x02 ！",
                r"我们必须\x02\x03讨伐/报复/攻击\x02\x01@a氏族！",
            ),
        ],
        19231: [
            (
                r"\x02\x03/粉碎/教训一顿/打击\x02   \x01@! 部落！",
                r"\x02\x03粉碎/教训/打击\x02\x01@!氏族！",
            ),
        ],
        19238: [(r"\x02\x03/骚蹄子", r"\x02\x03骚蹄子")],
        19323: [
            (
                r"\x02\x03这些肮脏的懦夫/我的剑歌吟唱/这些胆小鬼/我腹中知晓，\x02  \x01@a 部族那些没胆的废物干了这事",
                r"\x02\x03肮脏懦弱的/我的剑歌吟唱，毫无骨气的/我心知肚明，没胆的\x02\x01@a氏族干了这事",
            ),
        ],
        26222: [(r"让\x01@K永远", "让他永远")],
        27526: [(r"\x01@[\n", r"\x01@\n")],
        28110: [
            (
                r"\x02\x02咔嗒作响/纱线/舞蹈与尖啸\x02",
                r"\x02\x02咔嗒声与纱线/舞蹈与尖啸\x02",
            ),
        ],
        28193: [(r"\x02\x02经常\x02", r"\x02\x02定期/频繁地\x02")],
    }

    for row_number, replacements in repairs.items():
        text = rows[row_number - 1]
        for old, new in replacements:
            text = replace_once(text, old, new, row_number)
        rows[row_number - 1] = text

    with TRANSLATION.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.writer(file)
        writer.writerows([[text] for text in rows])

    print(f"已修复 {len(repairs)} 个需人工判断的行，并规范化随机分支内的全角斜杠。")
    print(f"原译文备份: {BACKUP}")


if __name__ == "__main__":
    main()
