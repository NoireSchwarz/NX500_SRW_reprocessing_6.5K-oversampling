# R10m：采样启动卡死修复版

R10l 的故障记录停在采样器启动前的 RTOS 挂载事务，没有生成新采集的 READY 文件。现有记录无法确定具体卡死指令；R10m 移除了采样键到该事务的调用，因此按采样键不再暂停相机线程、申请临时 RTOS CAP 或安装计时代码。

此版保留 100ms 读取和停止后的三文件保存。全宽配置使用与已在相机上运行过的 R10k 逐字节相同的 RTOS 代码，取消 R10l 的通用中断入口改写。HEVC 断点线程在本版停用，导出的 hevc-flow.bin 会明确记录停用状态 0x200。

本地验证通过不等于实机卡死已经排除，以下短测用于确认修复效果。

## 安装后先做短测

1. 装卡完成安装后，完整关机再开机，断开 USB。
2. 打开补丁视频页，确认显示 **R10m PASSIVE SAMPLING**。
3. 保持照片模式，按 **STAGE COUNTERS**，应显示 ON / STOP+SAVE，相机应能正常响应。
4. 等 5 秒，再按 STAGE COUNTERS 停止保存，等待 **COUNTERS SAVED - SAFE TO CONNECT USB**。
5. 此时无需应用全宽。刚开机尚无逐帧计时器是正常情况，采样仍应成功；分析文件会标记逐帧计时不可用，不能据此报告完整帧覆盖。

若显示 TRACE_RESTART_REQUIRED_OLD_IRQ_TRACE，请完整关机重开，清除旧版留在 RAM 的计时代码。

## 短测通过后做对照

固定机位、焦距、曝光、白平衡与对焦。原厂选择 NTSC、UHD30p、PRO。两段目标码率均为 80Mbps。

先全宽、后裁切，可在两段中沿用同一套已验证计时器：

1. 照片模式执行 FULL WIDTH 4K 30P，按提示在原厂菜单重新选择 UHD30p / PRO，再回照片模式。
2. 启动 STAGE COUNTERS，进入原厂视频取景，取景约 5 秒、录像约 25 秒、停录后等约 5 秒。
3. 回照片模式停止并保存 STAGE COUNTERS，等待保存成功。
4. 执行 CROP 4K 30P / 80M，重新选择 UHD30p / PRO，再按同样流程录制裁切对照并保存。

本轮保持 BUS QOS A/B 默认。HEVC 断点采集已停用，不需要操作窗口开关。

## 取回与异常

保存成功后再接 USB。保留本轮全部 VIDEO_STAGE_* 目录，包括：

- counts.bin
- frame-flow.bin
- hevc-flow.bin
- DONE

同时保留两段对应 MP4，并注明全宽与裁切。如果短测卡住、采样开始失败或全宽应用失败，记录屏幕文字及当时执行的步骤，保留最新 VIDEO_FULL_* 目录和 NXDEMO51/logs/install.log。

采集期间不要关机或连接 USB。若显示 RAM PENDING，保持开机，断开 USB，停止录像并回到照片模式，再按 STAGE COUNTERS 重试保存。

PC 分析命令：

    python PC/video-stage-counters/analyze_r10m.py NXDEMO51/VIDEO_STAGE_xxx --out analysis.json

此版用于恢复可靠采样，尚未证明超采样帧率瓶颈已修复。
