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
| 回填 | x86 启动器 + 注入 DLL + `CIwUIDrawableText`/底层字体准备双层 Hook + Marmalade 内存 TTF 渲染 | MSVC 19.50 |

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
- 最终方案：采用外置启动器与注入 DLL，不修改 Steam 游戏目录。`KoDPCNLauncher.exe` 正常启动游戏，检测到可见窗口并等待 2.5 秒后注入 `KoDPCNHook.dll`，避免过早注入干扰 Marmalade 字体缓存。
- 文字接管：DLL 在 `.s86` 映像稳定后 Hook `CIwUIDrawableText::Prepare/Draw`，并补充 Hook Marmalade 底层字体字符串准备入口；普通 UI caption 与绕过控件层的地图标签均在游戏计算字宽、换行和裁剪前替换，再交回原生 UI 流程绘制。
- 中文字体：Hook 从 Windows `simhei.ttf` 读取字体数据到内存，通过 Marmalade 的内存 TTF renderer 创建按字号缓存的 `CIwGxFont`；不再依赖外置 GDI 叠加窗口，也无需向游戏目录复制字体。
- 字号映射：Steam 2.4.0 的旧位图字体高度映射为约三分之一的 Marmalade TTF 点值（实测 18–30 → 6–10），由原 drawable 保持锚点、边界、对齐、裁剪和自动换行。
- 译文匹配：加载 33,726 条有变化的精确/运行时别名译文及 57 条格式模式，并支持空白归一化、末尾冒号变体、“年份 + 换行正文”复合 caption，以及氏族名后的 `N/ɴ` 邻近标记（显示为“（邻近）”）。
- 动态组合：对顾问提示中由多个语料片段、数字、单复数词或动态名称拼接的完整 caption，优先匹配自然语言 `%d` 模板，并以“所有英文字母必须由已知译文片段完整覆盖”的组合规则兜底；加载词库时还会从 Opal 控制码语料自动派生“以……闻名”的 45 个氏族描述别名。数字、空白和标点可原样保留，出现任何未知英文单词则整句不替换，避免内部键或半句话被误翻。
- 外置资源：曾生成重建版 `scenes.dat`、UI/HTML 资源和透明 GDI 叠加层用于试验；最终发布版均不启用，运行时统一走游戏原生文字管线。
- 构建命令：`powershell -ExecutionPolicy Bypass -File .\hook\build_hook.ps1`。
- 验证：已在 1368×793 窗口实机验证主菜单与氏族管理页；“游玩 / 设置 / 信息”、人口分类和资源标签均由游戏原生绘制，按钮内居中、字号和 UI 锚点正常，窗口缩放后无叠加层漂移。新增版本已通过猪群、农夫/耕牛、氏族邻近标记及“巨石战争力量/酋长/关系”完整拼接句内置自检，并完成启动稳定性检查。
- 产物：`release/KoDPCNHook/`。双击 `KoDPCNLauncher.exe` 启动汉化；直接从 Steam 启动即可恢复英文原版。

## 已知问题
- 旧版透明 overlay 在窗口缩放时会出现坐标二次变换和 UI 错位；最终版已停用 overlay，改由游戏原生 drawable 排版和绘制。
- 中文译文通常比英文占用更多 UTF-8 字节；回填 `scenes.dat` 时不能原位覆盖，必须重算字符串长度、场景字数及所有后续场景偏移。
- `.s86` 中的硬编码字符串需要区分显示文本与内部键名，并评估定长引用或重新定位。
- 中文显示依赖 Windows 的 SimHei（`simhei.ttf`）；字体缺失时 Hook 会保留英文并在 `KoDPCNHook.log` 记录错误。
- 贴图内文字以及翻译库未收录的动态键仍可能保留英文；地图矢量标签已纳入底层字体准备 Hook，词库已收录的地图专名会动态替换。
- inline Hook 依赖当前 Steam 2.4.0 版 `.s86` 布局（原文件 SHA-256：`BDC90ADACDE29B6E86ECE3886F28BA60C97A7A67F0E3BAB7A1D2BB1B641138C5`）；游戏更新后需要重新确认 RVA。

## 版本记录
- 2026-07-29 - 初始化汉化项目工作区（start-translate skill）。
- 2026-07-29 - 补充 Steam / 官方资料、本机资源布局与初步技术判断。
- 2026-07-29 - 完成剧情、程序、UI、帮助、手册和地图文本提取，共 34,421 条唯一文本。
- 2026-07-30 - 完成 AINiee 全量翻译，修复 32 行格式异常并通过项目专用校验。
- 2026-07-30 - 生成外挂式技术测试包：外置重建 `scenes.dat`，并以 x86 硬件断点 Hook `.s86` 运行时显示文本；原游戏文件零修改。
- 2026-07-31 - 完成透明 GDI 中文层、Marmalade 坐标/裁剪适配和多行正文匹配；实机验证主菜单、游戏管理、地图与传说页面。
- 2026-07-31 - 修复中文 overlay 界面持续闪烁：`RunCaptionOverlay` 改为仅在窗口位置/可见性变化时 `SetWindowPos`、仅 caption 实际变化时重绘，并将过期清理移出 `WM_PAINT`；重新构建 `KoDPCNHook.dll`。
- 2026-08-09 - 停用透明 overlay，改为 `CIwUIDrawableText` 排版前替换译文并使用 Marmalade 内存 TTF 原生绘制；修正 TTF 字号比例，在 1368×793 窗口验证 UI 对齐。
- 2026-08-09 - 修复农业页顾问动态提示漏译：新增多数字运行时模板和安全的数字/已知片段组合匹配，内置示例自检通过。
- 2026-08-09 - 将动态提示兜底升级为完整覆盖组合器，支持译文片段间的数字、空白、标点及纯文本组合；为农夫/耕牛需求提示增加自然中文模板，复数、单数和无数字组合自检通过。
- 2026-08-10 - 增加 Marmalade 底层字体准备 Hook，覆盖地图标签等绕过 UI caption 的文本；自动派生氏族描述别名并处理 `N/ɴ` 邻近标记，修复袭击页氏族列表、地图专名及“以……闻名/酋长/关系”动态介绍残留英文。
