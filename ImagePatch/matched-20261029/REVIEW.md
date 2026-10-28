# 最新 matched 流程试建（2026-10-29）

此前接手只查看旧 build-v10 并生成联系表，未将旧贴图当作最新流程成果。

本次读取最新 game-image-repack 技能，核对美化改动提交 a169ecd、2819076、8071501；使用正式入口 `Tools/image_text/image_workflow.py build --look matched`，以 work2 原图快照、原正式图片源文/译文及图片子工程 final_validation.json 构建全新 build-v11。

## 输入与配置

- 源 CSV 逐字节复制，保持正式校验哈希；译文未改。
- mapping 为独立快照，从 110 个区域移除 2 个已确认无烘焙文字的底板（RecruitCaption、DismissCaption），保留 108 区域；原项目 mapping 未覆盖。依据为项目既有视觉确认及 README 纯底板记录。
- 所有 image_id 明确使用 look=matched；不包含 mask_rgba、blur_radius、erase_text_ink、erase_light_ink、dark_rows 等 legacy 触发字段。
- 地图/标题采用 NotoSerifSC-variable，按钮采用 NotoSansSC-variable；Logo 使用透明清字、白字细黑描边。默认 grain 修补，不把 LaMa 全局套用。
- 构建成功：12 图、108 区域，installed=false；清单 look=matched，6 条区域带 warnings。程序成功不等于美观通过。

## 初步视觉结果：未通过

已查看旧、新 12 图联系表及原 Logo。新联系表保存 `Analysis/preview/repack_v11_contact.png`。

- Logo 的旧英文黑色轮廓较 v10 明显改善；仍须逐尺寸全图复核透明边缘和字形效果。
- ClanButton 的字体与底图配合较自然，MapArch 保持了拱形透明轮廓；联系表不足以签收细节。
- 多张地图在标签周边出现明显纹理修补斑块，高分辨率地图局部尤其突出。需要逐图/逐区域调整 ink、角度、掩膜与修补方法，再复核。没有把警告或自动取色视为质量结论。
- **本版不交引擎安装，不标记 visual_review=passed。** 旧版与本版都保留作对照。

原回填任务还包括新版 Hook 全部动态拼接检查；该工作未因本次图片流程核对而被标记完成。
