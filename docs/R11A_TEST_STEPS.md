# R11a 合并检测步骤

R10z 已确认安装和保存正常，但细窗口没有阶段向量。本包修正检测器的准备开销和映射扫描，不宣称已经修复帧率。

1. 装回 SD 卡完成安装，完整关机再开机、断开 USB。补丁视频页应显示 **R11a STAGE SAMPLING**。
2. 沿用 NTSC、UHD30p、PRO、原码率和默认 BUS QOS。照片模式应用 **FULL WIDTH 4K 30P**，按提示重选原厂 UHD30p／PRO，再回照片模式。
3. 开启 **STAGE COUNTERS**，确认 **ON / STOP+SAVE**，照片模式等 **5 秒**。
4. **视频取景 35 秒 → 录像 120 秒 → 停录后视频取景 35 秒**。保留适量运动细节，或录像中间缓慢改变构图几秒。只做这一次连续测试，不重复码率或 QoS 对比。
5. 回照片模式停止采样。务必看到 **COUNTERS SAVED - SAFE TO CONNECT USB** 后再接 USB。
6. 保留新 `VIDEO_STAGE_*`、`VIDEO_FULL_*`、安装日志及本次 MP4。发生错误时记下步骤和屏幕原文，不删除失败文件。

仍保存 `counts.bin`、`frame-flow.bin`、`hevc-flow.bin`、`buffer-flow.bin` 四个文件；buffer-flow 采用 schema 3。扫描进度、扫描失败以及窗口内没有足够余量的情况都保留为证据。相机是否实际运行新版，要由最新内部安装日志和采样运行标识 **R11a ASYNC_STAGE_MAPS** 共同确认。

PC 分析：`python -B -X utf8 work/r10k-ep-flow/tools/analyze_r11a.py 路径/VIDEO_STAGE_目录 --out 结果.json`。

预期确认项：细窗口实际阶段向量大于零；完整扫描时间区间包围热窗口；有效输出行读数及变化；同图像、原厂 pthread 和线程生命周期均通过后的 HEVC 等待链；完整内存快照和原有环覆盖。若某项未命中，要先看进度／拒绝原因，不能把缺失当作零耗时。
