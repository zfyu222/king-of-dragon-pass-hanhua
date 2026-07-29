# -*- coding: utf-8 -*-
"""King of Dragon Pass project-specific translation validator."""

import argparse
import csv
import re
import shutil
import sys
from collections import Counter
from pathlib import Path

OPAL_COMMAND = re.compile(r"\\x01(?:(?:\\x[0-9A-Fa-f]{2})|[^\r\n]){2}")
OPAL_BYTE = re.compile(r"\\x[0-9A-Fa-f]{2}")
PRINTF = re.compile(
    r"%(?:\d+\$)?[-+ #0']*(?:\d+|\*)?(?:\.(?:\d+|\*))?"
    r"(?:hh|h|ll|l|j|z|t|L)?[diuoxXfFeEgGaAcspn%]"
)
LITERAL_ESCAPE = re.compile(r"\\[nrt]")
INTERNAL_KEY = re.compile(
    r"^(?:"
    r"sceneR?\d{3}[a-z]?|"
    r"H\d+[a-z]{1,2}\d?|Q\d[A-Za-z]+|T\d+|"
    r"RaidGoodNews|RaidBadNews|GoodNews|BadNews|EmissaryNews|"
    r"GoExploringNews|MissionNews|BigBigTrouble|CallToArms|"
    r"CouldBeBad|CouldBeGood|IsItAdventure|ItIsBad|ItIsGood|"
    r"MightBeSad|QuiteAnAdventure|ReallyReallyGood|WeAreSad|"
    r"WeDidIt|WoeAlasAlack|IntroOverture|MythMusic"
    r")$"
)
FULLWIDTH_MARKERS = re.compile(r"[％＼ｘＸ]")
OUTPUT_ARTIFACTS = re.compile(r"\[P\d+\]|</?textarea>|```|�", re.IGNORECASE)


def load_single_column(path: Path):
    with path.open("rb") as file:
        physical_lines = file.read().count(b"\n")
    with path.open("r", encoding="utf-8-sig", newline="") as file:
        raw_rows = list(csv.reader(file))

    malformed = [index + 1 for index, row in enumerate(raw_rows) if len(row) != 1]
    rows = [row[0] if row else "" for row in raw_rows]
    return rows, physical_lines, malformed


def write_single_column(path: Path, rows):
    with path.open("w", encoding="utf-8-sig", newline="") as file:
        writer = csv.writer(file)
        writer.writerows([[row] for row in rows])


def multiset(pattern, text):
    return Counter(pattern.findall(text))


def printf_tokens(text):
    """Return printf tokens, excluding percent signs used inside Opal commands."""
    opal_spans = [match.span() for match in OPAL_COMMAND.finditer(text)]
    return [
        match.group()
        for match in PRINTF.finditer(text)
        if not any(start <= match.start() < end for start, end in opal_spans)
    ]


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser()
    parser.add_argument("source_csv")
    parser.add_argument("translated_csv")
    parser.add_argument("--fix-newlines", action="store_true")
    parser.add_argument("--retranslate-out", default="need_retranslate.csv")
    args = parser.parse_args()

    source_path = Path(args.source_csv)
    translated_path = Path(args.translated_csv)
    source, source_physical, source_bad_columns = load_single_column(source_path)
    translated, translated_physical, translated_bad_columns = load_single_column(
        translated_path
    )

    print(
        f"原文 {source_path.name}: 逻辑行={len(source)} "
        f"物理行={source_physical} 非单列={len(source_bad_columns)}"
    )
    print(
        f"译文 {translated_path.name}: 逻辑行={len(translated)} "
        f"物理行={translated_physical} 非单列={len(translated_bad_columns)}"
    )

    if source_bad_columns:
        print(f"[致命] 原文存在非单列记录: {source_bad_columns[:10]}")
        return 2
    if translated_bad_columns:
        print(f"[致命] 译文存在非单列记录: {translated_bad_columns[:10]}")
        return 2
    if len(source) != len(translated):
        print(f"[致命] 逻辑行数不一致: {len(source)} vs {len(translated)}")
        return 2

    embedded_newlines = [
        index + 1
        for index, text in enumerate(translated)
        if "\n" in text or "\r" in text
    ]
    if embedded_newlines and args.fix_newlines:
        backup = translated_path.with_suffix(translated_path.suffix + ".bak")
        if not backup.exists():
            shutil.copy2(translated_path, backup)
        translated = [
            text.replace("\r\n", "\n")
            .replace("\r", "\n")
            .replace("\n", r"\n")
            for text in translated
        ]
        write_single_column(translated_path, translated)
        print(
            f"已将 {len(embedded_newlines)} 行字段内真实换行还原为字面 \\\\n；"
            f"备份: {backup.name}"
        )
        embedded_newlines = []

    failures = []
    issue_counts = Counter()
    for index, (original, translation) in enumerate(zip(source, translated), start=1):
        issues = []

        if not translation and original:
            issues.append("译文为空")
        if multiset(OPAL_COMMAND, original) != multiset(OPAL_COMMAND, translation):
            issues.append("Opal \\x01 命令不一致")
        if multiset(OPAL_BYTE, original) != multiset(OPAL_BYTE, translation):
            issues.append("Opal \\xNN 控制字节不一致")
        if printf_tokens(original) != printf_tokens(translation):
            issues.append("printf 格式符或顺序不一致")
        if multiset(LITERAL_ESCAPE, original) != multiset(
            LITERAL_ESCAPE, translation
        ):
            issues.append("字面换行/制表转义不一致")
        if "\\x02" in original and original.count("/") != translation.count("/"):
            issues.append("Opal 随机短语分支数量不一致")
        if INTERNAL_KEY.fullmatch(original) and translation != original:
            issues.append("内部脚本键被修改")
        if FULLWIDTH_MARKERS.search(translation):
            issues.append("发现全角控制字符")
        if OUTPUT_ARTIFACTS.search(translation):
            issues.append("发现未还原占位符或模型输出包装")
        if "\n" in translation or "\r" in translation:
            issues.append("字段内存在真实换行")

        if issues:
            failures.append((index, original, translation, "；".join(issues)))
            issue_counts.update(issues)

    print("=" * 60)
    print(f"需重翻/修复: {len(failures)} 行")
    for issue, count in issue_counts.most_common():
        print(f"- {issue}: {count}")

    if failures:
        output_path = translated_path.parent / args.retranslate_out
        with output_path.open("w", encoding="utf-8-sig", newline="") as file:
            writer = csv.writer(file)
            writer.writerow(["行号", "原文", "当前译文", "问题"])
            writer.writerows(failures)
        print(f"待重翻清单: {output_path}")
        return 1

    output_path = translated_path.parent / args.retranslate_out
    if output_path.exists():
        output_path.unlink()
        print(f"已移除过期的待重翻清单: {output_path}")
    print("校验通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
