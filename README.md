# King of Dragon Pass 汉化项目

> 本文档记录本项目的初始化与后续汉化方案；游戏原始文件只通过 `Game/` junction 引用，不纳入版本控制。

## 游戏信息
- 原名：King of Dragon Pass
- 中文名：龙隘之王（暂译）
- 开发商：A Sharp
- 发行商：HeroCraft PC（Steam 版）
- 发行平台：Steam（Windows、macOS）
- Steam 发行日期：2015-07-28
- 引擎：Marmalade SDK / S3E；本机 `README.txt` 确认 Steam 版使用 Marmalade SDK 8.8.0（build 487222q），游戏版本 2.4.0。
- 原语言：英语（Steam 商店仅列英语）
- 参考资料：[Steam 商店页](https://store.steampowered.com/app/352220/King_of_Dragon_Pass/)；[A Sharp 官方页面](https://www.a-sharp.com/kodp/)

## 游戏简介

玩家在神话世界 Glorantha 中领导一个氏族，通过资源管理、外交、战争、信仰与大量分支事件，逐步壮大为部族乃至龙隘之王。游戏将互动叙事与策略管理结合，Steam 版约有近 600 个手绘事件场景。[Steam 商店页](https://store.steampowered.com/app/352220/King_of_Dragon_Pass/)；[开发商介绍](https://www.a-sharp.com/kodp/)

## 汉化目标
- 范围：剧情事件、程序内置文本、UI、开场/教程、帮助、内置手册和地图标签。
- 目标语言：简体中文

## 技术栈
| 环节 | 工具 | 版本 |
|------|------|------|
| 提取 | IDA Pro + IDA MCP；项目脚本 `extract_text.js`；Node.js | Node.js v25.8.0 |
| 翻译 | AINiee + 项目专用格式校验/修复脚本 | AINiee 具体接口/模型待补充 |
| 回填 | 待定（需重建 `scenes.dat` 场景偏移表，并处理 `.s86` 与位图字体） |  |

## 文本提取
- 输出：`king-of-dragon-pass_text.csv`，无表头、单列，共 34,421 条去重文本；标准 CSV 引号用于保护逗号和双引号。
- 来源目录：`Game/KoDp/scenes.dat`、`Game/King of Dragon Pass.s86`、`Game/KoDp/Map.opalmap`、`Game/KoDp/uiFiles/*.ui`、`Game/KoDp/*.plist`、`Game/KoDp/Text.xml`、`Game/KoDp/Help/*.txt`、`Game/KoDp/{Manual,Text}/**/*.html`。
- 来源目录表：`king-of-dragon-pass_text_catalog.tsv`，共 45,268 条来源记录，保存语料索引、源文件、场景号/文件偏移和文本类别。
- 工具/命令：在项目目录运行 `node .\extract_text.js`。
- `scenes.dat` 格式：文件含 1760 个场景块；文件头保存 32 位场景偏移表，每块先读 16 位 Opal 字节码字数，再读取 `2 × 字数` 字节。字符串记录为 `uint16 UTF-8字节数 + 文本 + NUL + 16位对齐填充`。
- 提取结果：从 `scenes.dat` 定位 38,320 条文本记录；另定位 `.s86` 硬编码候选 3,560 条、HTML 文本 2,626 条、UI caption 348 条、plist 文本 333 条、帮助文本 43 条、本地化 XML 25 条和地图标签 13 条。各来源去重后合并为最终 34,421 条。
- 控制码：剧情文本中的 `0x01`、`0x02` 等动态替换/随机短语控制码按 `\xNN` 写入 CSV；实际反斜杠、换行和制表符也采用可逆转义。翻译时必须完整保留。
- 验证：脚本连续运行两次生成相同 SHA-256：`DC925E1A2C6DC054BC6E10F0D08E2646894F00262D545A78F3C83C24073B420E`。
- 注意事项：`.s86` 中仍可能混有少量运行时键名，翻译阶段应结合目录表制作禁翻表。当前 `.gxfont` + `.tga` 位图字体不含完整中文字符，回填前必须解决中文字库。

## 翻译
- 状态：AINiee 全量翻译及格式校验已完成。
- 译文：`king-of-dragon-pass_text_translated.csv`，34,421 条、无表头、单列标准 CSV。
- 工具：AINiee；具体接口/模型待补充。
- 操作文档：`TranslateDoc.md`。
- 占位符报告：`placeholder_report.md`。
- 禁翻表：`ainiee_exclusion_list.json`，包含 Opal `\x01` 命令、通用 `\xNN` 控制字节、printf、字面转义和高置信度内部键。
- 规模：34,421 条文本；6,315 行含 Opal 控制码，共 14,217 个 `\xNN` 标记。
- 策略：AINiee 禁翻表与“自动处理文本代码段”同时开启；先小批试译并校验，再执行全量翻译。
- 术语：初版建议收录于 `TranslateDoc.md`；后续游戏内校对时继续统一神名、社会阶层、季节及资源名称。
- 格式修复：首轮发现 32 行异常；已恢复被改写/遗漏/重复的 Opal 命令与控制字节，将随机短语中的全角斜杠还原为半角，并修复被吞掉或错误拆分的分支。另修正了 `\x01@%` 被误判为 printf 的校验器假阳性。
- 校验：项目专用 `validate_translation_kodp.py --fix-newlines` 检查 CSV 对齐、Opal 命令、控制字节、随机短语分支、printf、内部键、残留 `[P数字]`、`textarea` 包装与异常字符；最终结果为 0 个问题。
- 译文 SHA-256：`3997974294D29060C1DA2F4FF46E55C2CB3E2F525E043B663ADE73D3521AB1EF`。
- 特别注意：不得加入通用方括号禁翻规则，玩家可见的 `[click “Proceed”]` 需要翻译。

## 回填
- 工具/命令：待定。
- 步骤：未开始。
- 验证方法：待定。

## 已知问题
- 中文译文通常比英文占用更多 UTF-8 字节；回填 `scenes.dat` 时不能原位覆盖，必须重算字符串长度、场景字数及所有后续场景偏移。
- `.s86` 中的硬编码字符串需要区分显示文本与内部键名，并评估定长引用或重新定位。
- 中文显示依赖位图字体扩展或替换，不能仅完成文本翻译。

## 版本记录
- 2026-07-29 - 初始化汉化项目工作区（start-translate skill）。
- 2026-07-29 - 补充 Steam / 官方资料、本机资源布局与初步技术判断。
- 2026-07-29 - 完成剧情、程序、UI、帮助、手册和地图文本提取，共 34,421 条唯一文本。
- 2026-07-30 - 完成 AINiee 全量翻译，修复 32 行格式异常并通过项目专用校验。
