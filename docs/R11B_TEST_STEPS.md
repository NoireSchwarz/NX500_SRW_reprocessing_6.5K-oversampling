# R11b 合并短测

这是一次检测包：修复 HEVC 等待候选识别，补齐底行右边缘观察。尚未证明帧率改善。沿用原码率，不重复码率对比。

## 安装与版本

1. 电脑安全弹出 SD 卡，将卡装回相机，完成自动安装。
2. 完整关机再开机，断开 USB。补丁视频页应显示 **R11b VERIFIED WAIT**。
3. 照片模式应用 **FULL WIDTH 4K 30P**。按提示在原厂菜单重选 NTSC／UHD30p／PRO，再回照片模式。沿用原来的码率、默认 BUS QOS 和曝光设置。
4. 如应用卡死或报错，停止后续测试；保留最新安装日志和 VIDEO_FULL 目录，不反复应用。

## 一次合并采样

1. 照片模式开启 **STAGE COUNTERS**，确认 **ON / STOP+SAVE**。
2. 照片模式等 **5 秒**，检查菜单响应。
3. 退出补丁菜单，视频取景 **35 秒**。
4. 录像 **120 秒**，让画面底部左右两侧都有适量运动细节，避免全黑或完全静止画面。
5. 停录后继续视频取景 **35 秒**。
6. 回照片模式，停止并保存 **STAGE COUNTERS**。
7. 必须等到 **COUNTERS SAVED - SAFE TO CONNECT USB**，再连接 USB。不要在保存完成前连接。

保留新 VIDEO_STAGE_* 目录的 READY、DONE、counts.bin、frame-flow.bin、hevc-flow.bin、buffer-flow.bin，以及对应 MP4、安装日志和 VIDEO_FULL_* 记录。不要删除或移动历史数据。HEVC 断点停用提示正常。

## 下轮检查

先确认安装日志的 `recovery-r11b-verified-wait-right-edge` 与实际采样 `R11b VERIFIED_WAIT_EDGE` 一致；SD 上存在新版文件不等于相机已运行新版。

再检查快读覆盖、细窗口覆盖、buffer schema4、六探针成功掩码、联合判定及拒绝原因。原始系统调用号为 0 或旧值会原样保存；只有原厂 PC、运行中指令字节、相同 HEVC 等待对象、线程生命周期、返回栈和缓冲关联都通过，才可作为等待链证据。未命中必须保留为未知。

Y／UV 底行左右探针用于观察可见写入先后，不是整帧 EOF；HEVC 等待包含固件、IRQ 和调度，不能当作纯编码耗时。下一轮据此继续分解 bit6→bit2，不能提前归因于 DDR。
