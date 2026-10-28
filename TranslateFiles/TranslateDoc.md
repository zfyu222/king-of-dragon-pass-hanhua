# 翻译操作记录

## 2026-10-28 接手恢复与最终验收

- 延续 `custom/api` 与现有 API 优先级，不固定接口、不增加预算；正文冻结术语为 `glossary/r3/freeze_c04a1e8d/built/glossary_reviewed.json`。
- 标准档 `/root/review_resolution` 对四条停止项提供六维审校证据；标准档 `/root/final_disputes` 处理固定 23 条最终争议。裁决与原始字节证据保存在项目 `Analysis/`，数据库保留原候选与实际归属。
- Opal `\x01`、`\xNN`、printf、字面转义及内部键仍按原配置保护。非 @ 命令长度尚未从引擎证实，未放宽正则；动态所有格/数词待回填验证。
- Humakti 定语、Humakt 复合专名、Spirit Fetch 单复数及一段 weaponthane 单复数冲突通过具体语句的有据例外处理，未全局改写冻结表或豁免术语。
- 定向 API 返修只处理 8 条，2 次请求、0 失败、39,828 个服务端报告 Token。8 条审校通过，其中 7 条改译、1 条保留。最终标准档第 2 轮 approved，报告 hash `0424b1d851e0559df3000a0d9b153512a0c02e47b8c0d86ee5aa001f29550b9c`。
- 全部 34,421 条完成独立审校。正式交付以流程程序 export 成功后的 `reports/final_validation.json` 为准；旧 release/Dispatch 并未据此更新。

- 工作流：custom
- 执行路线（codex=SubAgent，api=API）：api
- 原文：项目根目录 `king-of-dragon-pass_text.csv`
- 正式译文：项目根目录 `king-of-dragon-pass_text_translated.csv`
- 中间产物：`TranslateFiles/`

由 game-translate 技能调用共用流程程序；路线为 SubAgent（codex）或 API（api）。正文前完成全文术语整理及冻结。
试译：固定随机样本 → pilot_translate → pilot-report → 主 AI pilot-decide；按需 pilot-retry。
试译用于校准流程和风格，不要求逐句零错误；正式译文仍须完整独立审校和导出验收。
