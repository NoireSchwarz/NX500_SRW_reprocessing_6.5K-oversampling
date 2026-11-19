# R10v 短测：IPC bit6 与同请求地址原值

本版继续定位全宽 4K 的长区间，尚未证明帧率改善。保留 event10、控制器 0 首末输入绑定和成功通知关窗，增加原厂接受的 IPC bit6 计数、首次时间及实际回调核验。

只测当前 **NTSC / UHD30p / PRO / FULL WIDTH 4K 30P**，沿用现有码率、机位、曝光和默认 BUS/QOS，不做码率或原生裁切对比。

1. SD 卡装回相机，等待安装完成，确认补丁视频页显示 **R10v IPC BIT6 COUNTERS**。
2. 完整关机并取出电池约 10 秒，再装回开机、断开 USB。R10v 更换了 RTOS 观测代码，这一步必须做。
3. 照片模式应用 **FULL WIDTH 4K 30P**。成功后按提示到原厂菜单重选 **NTSC / UHD30p / PRO**，再回照片模式。
4. 先做启停短测：开始 **STAGE COUNTERS**，确认 ON / STOP+SAVE；等 5 秒，确认菜单与按键正常，再按一次，等待 **COUNTERS SAVED - SAFE TO CONNECT USB**。照片模式没有逐帧记录不算失败。
5. 短测正常后，再开始 **STAGE COUNTERS**，退出补丁菜单进入视频取景。
6. **取景 5 秒 → 录像 10 秒 → 停录后取景 5 秒**。
7. 回照片模式，停止并保存 **STAGE COUNTERS**，等待 **COUNTERS SAVED - SAFE TO CONNECT USB** 后再接 USB。
8. 保留全部新 `VIDEO_FULL_*`、`VIDEO_STAGE_*` 和对应 MP4。照片、录像和历史日志继续保留，不格式化、清理或修复文件系统。

若应用报错或相机失去响应，记录发生阶段和屏幕原文，停止重复应用；断电恢复后保留卡上目录。

下次检查会核对安装日志 `recovery-r10v-ipc-bit6-observer` 与本次包的内部清单；采集器 `R10v IPC_BIT6_COUNTERS`、producer **11 / magic 0x42544c56**、route4、`NXL_IPC_LINK2` 配对原值。SD 部署完成不代表相机已安装运行新版。

重点检查 bit6 实际回调槽、使能原值，每请求次数和首次时间、窗口外总计数，event10 每请求命中，以及 RTOS／Linux 同一活动请求的输入描述符、PA、输出描述符和 DMA 配置。bit6 缺失、多次、回调变化或窗外事件会明确标记，不能冒报内部阶段耗时。

仍每 100ms 读取、四槽完整 26MHz 时间戳、三文件保存，HEVC 断点仍停用。完成通知继续发布记录，但原完成时间字段现在保存首次 bit6 时间；最终 IRQ 时间也改为原厂接受 bit2、ACK 前的时刻，不能把这些变化误报为硬件变快。
