# King of Dragon Pass 汉化项目

> 本文档记录本项目的初始化与后续汉化方案；游戏原始文件只通过 `Game/` junction 引用，不纳入版本控制。

## 游戏信息
- 原名：King of Dragon Pass
- 中文名：龙隘之王（暂译）
- 开发商：A Sharp
- 发行商：HeroCraft PC（Steam 版）
- 发行平台：Steam（Windows、macOS）
- Steam 发行日期：2015-07-28
- 引擎：Marmalade / S3E 运行时（根据本机 `s3e*.dll`、`Marmalade` 相关 DLL、`.group` 资源包推断，待提取阶段验证）
- 原语言：英语（Steam 商店仅列英语）
- 参考资料：[Steam 商店页](https://store.steampowered.com/app/352220/King_of_Dragon_Pass/)；[A Sharp 官方页面](https://www.a-sharp.com/kodp/)

## 游戏简介

玩家在神话世界 Glorantha 中领导一个氏族，通过资源管理、外交、战争、信仰与大量分支事件，逐步壮大为部族乃至龙隘之王。游戏将互动叙事与策略管理结合，Steam 版约有近 600 个手绘事件场景。[Steam 商店页](https://store.steampowered.com/app/352220/King_of_Dragon_Pass/)；[开发商介绍](https://www.a-sharp.com/kodp/)

## 汉化目标
- 范围：待文本提取后确认；优先覆盖剧情事件、UI、帮助与内置手册文本。
- 目标语言：简体中文

## 技术栈
| 环节 | 工具 | 版本 |
|------|------|------|
| 提取 | 待分析（重点检查 `Game/KoDp/` 下的 `.plist`、`.xml`、`.txt`、`.html`、`.dat` 与 `.group`） |  |
| 翻译 | AINiee / 人工校对 | 待定 |
| 回填 | 待定（需先确认 `.group` / `.dat` 资源格式） |  |

## 文本提取
- 文本位置：已发现可直接读取的 `Game/KoDp/Help/*.txt`、`Game/KoDp/Manual/*.html`、`Game/KoDp/*.plist`、`Game/KoDp/Text.xml`；剧情事件疑似位于 `Game/KoDp/scenes.dat` 或资源包中，尚待验证。
- 工具/命令：待第 1 步分析确定。
- 步骤：未开始。
- 注意事项：当前字体资源为 `.gxfont` 与 `.tga` 位图字体，预计不含中文字符；回填前需要评估并制作中文字库。资源包格式不是 Unity / .NET 标准布局，应避免直接套用 AssetRipper 或 dnSpy 流程。

## 翻译
- 词典：待建立。
- 策略：待提取后，根据事件、界面、帮助与手册来源分组翻译，并保留变量、标记与控制符。

## 回填
- 工具/命令：待定。
- 步骤：未开始。
- 验证方法：待定。

## 已知问题
- 剧情事件文件 `Game/KoDp/scenes.dat` 约 4.7 MB，资源格式尚未识别。
- 中文显示依赖位图字体扩展或替换，不能仅完成文本翻译。

## 版本记录
- 2026-07-29 - 初始化汉化项目工作区（start-translate skill）。
- 2026-07-29 - 补充 Steam / 官方资料、本机资源布局与初步技术判断。
