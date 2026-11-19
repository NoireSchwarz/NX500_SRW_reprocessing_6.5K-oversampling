R11e 合并测试：内存准入修复与并行访存四条件实验

本包是检测包。修复 R11d 在 MemFree=22792KiB 时拒绝实验、菜单却显示普通 ON 的问题；尚未证明全宽帧率已改善。不要重复 100/80/70 码率对比。

1. 安全弹出 SD，断开 USB，将卡留在相机内完成安装，然后完整关机再开机。进入补丁视频页，必须显示 R11e MEMORY A/B TEST。SD 部署成功不能代替相机安装成功。
2. 在照片模式应用 FULL WIDTH 4K 30P，按原提示在原厂菜单重新选择 NTSC / UHD30p / PRO，再回照片模式。BUS QOS A/B 保持默认。若应用卡死或报错，停止后续测试，保留本次日志。
3. 在录像之前按 STAGE COUNTERS。必须看到 MEMORY TEST READY / START VIDEO，行内显示 READY / STOP+SAVE。若出现 UNAVAILABLE、ABORTED、UNKNOWN 或启动失败：不要开始长录，等5秒，回照片模式停止保存这份短记录，看到 SAFE TO CONNECT USB 后连接电脑检查。
4. 只有 READY 后才进行同一次合并采样：视频取景35秒 → 连续录像240秒 → 停录后取景35秒。全程 USB 断开，保持机位、曝光、白平衡、对焦和拍摄设置。不要中途切换配置或开关采样。
   实验在识别到稳定目标录像10秒后自动开始：四条件（不加读、4KiB小工作集、4MiB低剂量加小工作集、4MiB高剂量）共8组、32段，每段6秒。无需手动切换，也不增加录像次数。
5. 回到照片模式，重新打开补丁视频页（等状态读取完成），预期 MEMORY TEST COMPLETE 32/32、行内 DONE 32/32 / SAVE。若显示 OFF / START，先停止操作并记录现象，不要重新启动覆盖这次实验。若显示 ABORTED / UNAVAILABLE / UNKNOWN，仍保存现有数据，不要补录。
6. 再按 STAGE COUNTERS 停止并保存。必须明确看到 SAVED / TEST COMPLETE / SAFE TO CONNECT USB。若是 TEST INCOMPLETE 或 TEST UNAVAILABLE，表示文件可以安全连接USB，但实验并不完整。没有 SAFE 就不要连接 USB；PENDING 时回照片模式再次请求保存。
7. 仅在 SAFE 后连接 USB。保留新 VIDEO_STAGE_* 中 ADMISSION、READY、counts.bin、frame-flow.bin、hevc-flow.bin、buffer-flow.bin、DONE，保留对应 MP4 和安装/启动日志。不要清理历史目录。

若误提前连接USB但相机仍开机：安全弹出并断开USB，不要断电，回照片模式再按 STAGE COUNTERS 尝试补保存；明确 SAFE 后再连接。RAM 恢复不能挽回重启后消失的数据。

判定要同时检查安装 revision=recovery-r11e-memory-arena、实际采样 R11e MEMORY_ARENA、buffer schema7、ADMISSION 身份、32段覆盖、CPU与频率匹配、实际读取剂量、RTOS首末绑定及撤载恢复。菜单 COMPLETE 只表示计划段结束，不保证这些证据全部合格。
