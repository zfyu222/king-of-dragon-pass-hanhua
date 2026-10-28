# 回填至可测试状态（2026-10-29）

## 测试载荷

`Repack/Test_20261029/KoDPCNHook/`；操作见项目根 `TEST_GUIDE_20261029.md`。本次没有启动/操作用户游戏，没有改变真实 Game 目录，没有生成对外发布 ZIP。

- 输入正式译文 SHA-256：`cd90ffc6a0dc395535f244709bd99ef171ae0d0884c1c280d54f2be29d193e50`。
- `build_runtime_payload.py` 只构建实际使用的 34,421 条 TSV 词库及 HTML 白名单，不复制旧试验 scenes.dat/UI/plist 覆盖层。逐项核对 2,626 条 HTML 原文，优先使用既有英文备份，生成 226 个页面；对应 Text/_Text/Manual 实际安装位置共 348 个。
- 原生工具链 MSVC x86，沿用已验证的 Marmalade Hook RVA，支持 .s86 SHA-256=`bdc90adacde29b6e86ece3886f28ba60c97a7a67f0e3bab7a1d2bb1b641138c5`。没有改游戏 EXE/.s86。
- 修正两个猪群动态模板把土地 hide 误写“兽皮”的旧硬编码，使用本次正式语料的“海德”；氏族描述别名移除句首语法分隔符与“以”前缀，避免“他们因，以……”；半翻译自检从当前词库取得人情句，不再硬写旧版“恩惠”。
- `build_runtime_check.ps1` 在独立控制台进程中直接调用同一 hook.cpp 查词/拼接实现，不注入、不启动游戏；所有样例及内置综合自检通过。记录 `runtime_check_20261029.txt`，这不是字体、排版或实机通过证据。

## 图片美化

- 最新 matched 正式入口构建 v12–v15。显式指定深色原字避免误擦地形，固定斜向标签角度，选中四类山脉/河道标签使用 LaMa，其余 Telea。v15 增加可逐图配置的 erase_padding（0–12 px，默认行为不变），按分辨率清除原字宽描边，相关 20 项测试通过。
- 本次最终候选为 `ImagePatch/build-v15/`，12 张图片、108 个区域均已实际查看。透明 Logo 使用完整原字行区域清除旧 alpha 阴影，字形加重、轻斜体；四分辨率地图的长地名调整为两行。原源文/正式译文没有变化，映射修订在独立快照中记录。
- 状态 `reviewed_for_testing`，不是最终美观签收；局部地形仍偏软。`Storm Mts.` 不在原确认的 30 行语料中，保持英文并列入已知问题，没有把它静默画成未经正式翻译的新条目。

## 引擎适配与安装

- 12 个独立贴图载荷按 `KoDp/textures/` 的真实路径准备，尺寸逐图一致；两张 JPG 由中文 PNG 转成 JPEG，避免把 PNG 内容写成 .jpg 扩展名。
- 在 `KoDP480.group.bin` 中找到唯一、完整、逐字节一致的 Logo.png RGBA 像素序列，偏移与长度保存在 TEST_MANIFEST。用同尺寸中文 RGBA 等长替换，其他头部、资源偏移、文件长度和字节保持。发布给用户测试的载荷仅有新 RGBA 数据，不复制原始整个资源包。
- 其他分辨率资源绑定未实机核实；若仍显示原贴图，需继续查实际加载路径，不能视为通过。
- `InstallTest.ps1` 是纯系统 Windows PowerShell/.NET，校验支持版本、运行文件、载荷和全部目标基线，先完整备份再安装，失败回滚；重复安装与恢复都有哈希检查。恢复目标是安装前状态，可能含旧版汉化，不宣称总能恢复纯英文。

## 验证证据

`verify_test_package.py` 只对 Repack 下的新建隔离副本运行真实 Windows PowerShell 安装器：361 个目标（348 HTML、12 图片、1 图片资源包）的安装、重复安装、恢复、篡改载荷拒绝全部通过；前后逐个核对真实 Game 基线未变化。报告：`repack_test_verification_20261029.json`，绑定 TEST_MANIFEST 哈希。

正式 .s86 的 REA 6.4.0/Ghidra 12.1.4 分析在将单进程启动上限提高到 1200000ms 后完成，找到 CIwTexture 等字符串，证据 `texture_search.json`；没有据字符串猜测内存布局或新增未经验证的纹理函数 Hook。图像资源的实际适配采用上述完整像素匹配与真实文件路径。

最终仍为 pending_user_test；未调用 record-game-check 或 release-check 伪造通过，也未消费新的翻译 API。
