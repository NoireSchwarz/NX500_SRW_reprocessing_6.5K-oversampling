# NX LUT Lite 0.4.0 · Unified R1.1 验证包

> [!CAUTION]
> **旧 R1 快启安装已收到软砖反馈，请停止叠装和重复重建。** 蓝屏原厂恢复可用独立 FAT32 卡和官方 NX500 1.12 固件。后屏完全翻到自拍位，同时按住 **MOBILE／Wi-Fi＋AEL＋全按快门** 再开机。[原厂恢复完整步骤](../../RECOVERY_zh.md)

R1.1 修正旧版首装顺序、启动冲突检查和拒绝写镜像时的开机交接。Windows 安装准备、真实 ARM 原厂服务管理器及两套对焦载荷已完成电脑验证。R1.1 的新安装流程仍待实机验证。已有 R1 可正常使用的机身，可保留当前快照。

## 选择版本

| 安装选择 | 内容 |
| --- | --- |
| **仅主模块** | Log117、LUT、SRW、相簿及图传。安装 `module/`，保留原快照。 |
| **`EYE28`** | 默认快启选项，沿用 R1 发布的 Eye AF v2.8。 |
| **`YUNET`** | 可选快启，沿用上一轮实机短测的 YuNet Focus2 对焦库。保留原厂主脸入口、左眼优先、按人脸大小调整对焦框和显示对齐。[YuNet 模型出处及 MIT 许可](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/blob/416f46bbde9839afd0a9feb97abe9042e2911a79/release/0.4.0/eye-af/model/YUNET.md) · [对应实现源码](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/tree/416f46bbde9839afd0a9feb97abe9042e2911a79/release/0.4.0/eye-af) |

## 安装

1. **确认机型。** 本包面向三星 NX500 原厂 1.12。先保留原卡上的照片、录像、原始镜像备份与 `NXDEMO51` 日志。卡上已有其他安装入口或独立 `nx-on-wake` 时，先核对其归属。旧快照只识别完整、可核验的 Unified R1。安装器会保留未知服务和历史文件并停止。旧独立 Eye、MNP、Primary2 采用各自原来的升级路径。
2. **安装主模块。** 已安装 `release-0.4.0-rc6.14.12-log117` 可跳过。首次安装时先在电脑核对 ZIP 的 SHA-256，再把 `module/` 内的内容写入准备好的项目 exFAT 卡。安全弹出。正常关机、卡灯熄灭后取电池约 30 秒，装回开机一次。安装日志在 `NXDEMO51/logs/install.log`，成功标记为 `INSTALL_COMPLETE SUCCESS`。核对后，仅主模块的用户可在蓝牙钩子原路径使用菜单。
3. **先设置相机，再择一准备快启。** 准备满电、English、实体 M 档、熄屏 5 分钟、省电 10 分钟。关闭 Wi-Fi 和蓝牙，关机卸镜头并盖机身盖。随后在包的解压目录打开 PowerShell。将 `X:` 和 `ABCD-1234` 改成项目 SD 卡的盘符和 exFAT 卷序列号。

   ```powershell
   python .\install-fast-start.py REBUILD EYE28 --drive X: --serial ABCD-1234
   ```

   选择 YuNet Focus2 时，把同一条命令的 `EYE28` 换成 `YUNET`。如需先检查已安装的快启身份，可以把 `REBUILD` 换成 `CHECK`。`CHECK` 只生成核验回执，通过时为 `reason=CHECKED_READY`。两次都沿用原安装入口。`STATUS` 在电脑只读查询该选项的回执。
4. **先冷启动触发重建。** 写好快启入口后，安全弹出 SD 并拔 USB。正常关机，等卡灯熄灭，取电池约 30 秒，装回并开机一次。**FAST START** 提示可能一闪而过。从进入重建开始至少等 5 分钟且卡灯熄灭，再取电池 10 秒、装回静置至少 4 分钟且灯灭。拨到 OFF，取电池 5 秒后装回并开机。若冷启动后仍停在正常取景且看不到重建迹象，等灯灭接回 USB 查回执。若超过上述等待时间仍持续黑屏或闪灯，记录停在哪一步。出现固件更新进度时等更新完成。若无法正常开机，见 [专用恢复流程](../../RECOVERY_zh.md)。
5. **验收普通冷启动。** 卡根目录已没有 `info.tg`、`install.sh`、`nx_cs.adj` 后，关机、灯灭再装镜头并普通冷启动。确认原厂菜单、半按取景和全按拍照正常，再检查 EV+AEL 及 A-AF＋一般人脸检测下的眼框。接回后运行 `STATUS`。`CAPTURE_REQUESTED` 只表示已发出重建请求，`EYE_AND_MENU_READY` 仍需实测眼框和菜单行为。

[调查结论与验证边界](AUDIT_zh.md)
