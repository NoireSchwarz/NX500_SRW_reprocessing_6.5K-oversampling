# R10x 一次合并短测

这次只测全宽，不加原生4K或多码率对比。照片、录像和历史日志均保留。

1. SD装回相机，等安装完成，然后完整关机、取出电池约10秒再开机，断开USB。打开补丁视频页，确认 **R10x OUTPUT BUFFER LINK**。全宽helper仍可能显示R10v，这是本版预期。
2. 照片模式应用 **FULL WIDTH 4K 30P**。按提示在原厂菜单重新选择 **NTSC／UHD30p／PRO**，再回照片模式。沿用当前码率，BUS QOS A/B保持默认，不切换其他档位。
3. 按 **STAGE COUNTERS** 开始，确认显示 **ON / STOP+SAVE**。照片模式约2秒，确认菜单、按键仍正常。
4. 退出菜单，视频取景 **5秒 → 录像60秒 → 停录后继续取景5秒**。保持机位、场景、曝光、白平衡和对焦。不要暂停采样或连接USB。
5. 回照片模式，再按一次 **STAGE COUNTERS** 停止保存。**必须看到 COUNTERS SAVED - SAFE TO CONNECT USB 后，再连接USB。** 保存失败或仍显示PENDING/SAVING时，保留状态并记录屏幕原文。

只做这一段。若全宽应用失败、开始失败或相机无响应，记录发生在哪一步和屏幕提示，保留本轮新目录，不连续反复测试。

回传/保留新`VIDEO_FULL_*`、`VIDEO_STAGE_*`（READY、DONE及三份bin）、安装日志和对应MP4。不要删除历史文件。第三文件名仍为`hevc-flow.bin`，内容已换成输出/HEVC只读元数据，断点追踪继续停用。

下一次核对：内部安装revision应为`recovery-r10x-output-buffer-link`，菜单为R10x，采集器为`R10x OUTPUT_BUFFER_LINK`，FAST4／PIPELINE2／RTOS producer11。先确认预算退让后仍有窗口、重读账本和覆盖，再检查WDMA2—HEVC地址关联及完成pending；没有有效标记时明确报告观测不足，不把零值当瓶颈。
