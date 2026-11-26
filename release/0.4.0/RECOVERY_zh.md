# NX500 原厂恢复模式

> [!CAUTION]
> **开机有取景却没有原厂界面、无法使用 USB，或卡灯持续闪烁时，先停止重复安装。** 社区已在一台 NX500 上用机身自带恢复模式刷回原厂 1.12。恢复会重刷固件，原卡上的照片、录像、备份与日志应另行保留。该按键组合已在 NX500 上验证，NX1 尚未验证。

已确认相机正常关机且卡灯熄灭时，可以先试一次取下模组卡后开机。如果仍无法进入原厂界面，按下面的流程进入原厂恢复。若正在显示固件更新进度，保持电池及 SD 卡原样，等它显示完成。

1. 准备充满电的电池和**另一张已格式化为 FAT32 的 SD 卡**。从 Samsung 下载 NX500 机身固件 `NX500_FW_v1.12.zip`，将解出的原厂 `nx500.bin` 放在这张卡的根目录。恢复卡只放原厂固件，不复制模组或安装触发文件。[Samsung NX500 官方下载](https://www.samsung.com/es/support/model/EV-NX500ZBMIES/)
2. 断开 USB，电源拨到 **OFF**。取出电池，插入恢复卡。把背屏**完全向上翻到自拍位**，然后装回电池，保持电源在 **OFF**。
3. 同时按住 **MOBILE／Wi-Fi、AEL 和全按快门**，保持三个键按下并拨到 **ON**。看到蓝色 **Recovery Mode** 屏幕后松开。[社区恢复实测与按键出处](https://github.com/ottokiksmaler/nx500_nx1_modding/blob/master/doc/NX500_Recovery.md)
4. 按蓝屏提示操作。社区实测是在这里**再次按住快门**开始刷入 `nx500.bin`。出现 **Updating** 后保持供电和卡原样，直到 **Update Finish** 并按屏幕指示完成。[原始实机报告](https://www.reddit.com/r/SamsungNX/comments/1y5wexh/samsung_nx500_hidden_recovery_mode_discovered/)
5. 恢复后，先在没有模组卡的情况下检查原厂取景和 MENU。记录屏幕反馈，并留存原模组卡的 `NXDEMO51` 安装和快启日志。若同一张准备好的 FAT32 卡始终无法进入蓝屏，记录灯和屏幕状态后联系维修。此流程依赖机内原厂引导和恢复程序仍能运行。

暂缓在 Unified R1 机身或卡上叠装独立的 `nx-on-wake`、快启或对焦包。旧安装器会覆盖同名启动程序和服务并触发重建。[独立 nx-on-wake 安装器](https://github.com/ottokiksmaler/nx500_nx1_modding/blob/master/nx-on-wake/install/install.sh)
