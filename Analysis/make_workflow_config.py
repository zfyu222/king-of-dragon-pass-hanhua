# -*- coding: utf-8 -*-
"""Build the translation workflow config for King of Dragon Pass."""
import json
from pathlib import Path

PROJECT = Path(r"d:\StaticHanHua\Projects\king-of-dragon-pass")

exclusions = json.loads((PROJECT / "ainiee_exclusion_list.json").read_text(encoding="utf-8-sig"))

style = (
    "《龙隘之王》（King of Dragon Pass）是格洛兰萨（Glorantha）神话背景的氏族经营与互动叙事游戏，"
    "文本由事件叙述、选项、顾问建议、UI 标签和动态拼接片段混合而成。"
    "叙述文字要求史诗感、古朴凝练、有画面感，用词贴近神话与部落生活，但不要写成文言文，也不要滥用成语；"
    "人物对白和顾问意见要求自然口语、直接有力，保留说话人的立场、情绪与偏见，不加解释性补充。"
    "玩家选项使用简短祈使句，动词开头，句末用句号；同一场景内多个选项的句式与长度尽量对齐。"
    "UI 标签、按钮、资源名、季节名使用简体中文的短词，不加句号，尽量不超过 6 个字。"
    "专名（神祇、氏族、人物、地名、部族、英雄任务）必须使用冻结术语表的统一译名；"
    "术语表未收录的新专名按音译为主、意译为辅，音译不夹杂间隔号。"
    "数值、牛只头数、年份、百分比一律使用阿拉伯数字，不得改写成中文数字。"
    "原文中的 \\x01 命令、\\xNN 控制字节、printf 格式符、字面 \\n\\r\\t 以及随机短语的 / 分支结构必须原样保留，"
    "随机短语的每个英文分支分别译为中文，分支数量与斜杠位置不变。"
    "许多条目是整句被拆开的片段（半句、短语、单个词），必须按其在句中承担的成分翻译："
    "冠词与介词片段保持可拼接的短形式，不要补主语或补句号，不要改写词性，不要输出解释。"
    "遇到含义含糊的片段，按该词在氏族管理语境中最常见的含义翻译，不要臆造专名或剧情。"
)

config = {
    "workflow": "custom",
    "backend": "api",
    "model": "gpt-6-luna",
    "source_language": "英语",
    "target_language": "简体中文",
    "style": style,
    "batch_records": None,
    "batch_tokens": 1024,
    "context_records": 0,
    "case_sensitive": False,
    "whole_word": True,
    "polish": False,
    "exclusions": exclusions,
}

target = PROJECT / "Analysis" / "workflow_init.json"
target.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")
print("wrote", target)
print("exclusions:", len(exclusions))
