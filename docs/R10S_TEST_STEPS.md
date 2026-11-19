# R10s：LDC 对象读址修正短测

R10r 已确认 event10 在稳定录像每帧命中一次，但对象类型快照误读了参数区。R10s 仅修正只读对象快照和版本标记；当前 event10 入口、RTOS计数代码、全宽应用 helper、几何、码率、时钟和 QoS 保持 R10r 字节/设置。它是诊断版，尚未证明提升帧率。

1. 安全弹出SD，装入相机完成安装；随后完整关机再开机，断开USB。视频页应显示 **R10s LDC OBJECT CHECK**。
2. 在照片模式应用 **FULL WIDTH 4K 30P**。按提示到原厂菜单重新选择 **NTSC、UHD30p、PRO**，保持此前相同设置后回照片模式。不要切换100/80/70码率，不开BUS QOS A/B。
3. 按 **STAGE COUNTERS**，确认 **ON / STOP+SAVE**；退出补丁菜单进入视频取景。
4. **取景3秒 → 录像6秒 → 停录后取景3秒**。只测这一段全宽，不需要裁切对照。
5. 回照片模式，再按 **STAGE COUNTERS** 停止保存，等 **COUNTERS SAVED - SAFE TO CONNECT USB** 再连接USB。尽量让开始至按停止的总采样少于25秒；若菜单操作较慢，不必抢按或中途断电，按正常停止保存即可，预算标志会如实记录。

如应用或启动报错，记下屏幕原文和发生步骤；如卡死，记录卡死位置，重启恢复后保留SD原样。不要清理日志、格式化或修复文件系统。

保留新的 VIDEO_FULL_*、VIDEO_STAGE_*、对应MP4以及安装日志。后续将检查：

- 安装revision为 recovery-r10s-ldc-active-object，采集文件revision为 R10s LDC_ACTIVE_OBJECT_COUNTERS。
- producer仍为9；route格式升为3，活动策略来自0x8142efb8，owner=策略+12，精确主/次类型匹配。
- 稳定录像每帧一次、无饱和/owner变化，并检查边界顺序、覆盖和预算标志。

全宽应用helper与RTOS二进制保留R10r，应用阶段日志仍可能显示R10r；不要据此误判整个包版本，以安装清单和采集revision/route3共同核对。仅部署到SD不表示相机已安装或运行R10s。

对象关联通过仍不证明DMA缓冲身份、连续对象生命周期或纯硬件忙时间。缺失、多次、变化或旧route2记录仍不进入正式LDC分段统计。HEVC断点停用标记是预期状态。
