# R11d 合并测试：一次录像检验访存影响

本包会短时增加采集器自己的内存读取负载，用来做对照；它是检测包，尚未修复帧率。相机图像处理、码率、QOS和时钟配置保持现有设置。

1. 把SD卡装回相机完成安装，然后完整关机、再开机，断开USB。
2. 打开补丁视频页，确认显示 **R11d MEMORY A/B TEST**。
3. 在照片模式应用 **FULL WIDTH 4K 30P**，按提示在原厂菜单重选NTSC／UHD30p／PRO，再回照片模式。
4. 开启 **STAGE COUNTERS**。应显示 **ON / STOP+SAVE**，等5秒确认菜单和按键正常。若显示 **TEST UNAVAILABLE** 或 **TEST ABORTED**，停止并保存这次短记录即可，不继续录长段。
5. 固定机位、场景、曝光、白平衡和对焦；进入视频取景，等 **35秒**。
6. 连续录像 **240秒（4分钟）**。四种条件和8组对照会自动切换，期间保持同一场景和设置。
7. 停录后保持视频取景 **35秒**，再回照片模式。
8. 再按一次 **STAGE COUNTERS** 停止保存。必须看到 **COUNTERS SAVED - SAFE TO CONNECT USB** 后才连接USB。

只做这一段全宽录像。沿用当前码率和默认BUS QOS A/B，不额外切码率、CPU档位或QOS，不需再录一段原生裁切4K。

保存新生成的VIDEO_STAGE_*目录、对应MP4及安装日志。采样仍保存四个bin；访存对照明细在新版buffer-flow.bin里。录像会包含检测负载带来的性能变化，分析时按条件拆开。

下一轮先核验相机内部安装revision为recovery-r11d-memory-ab，运行counts为R11d MEMORY_AB、buffer schema6，再检查32段覆盖、实际CPU匹配、读取剂量、频率和撤载恢复。仅“SD上文件已更新”不能代表相机实际运行新版。

