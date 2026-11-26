# NX500 0.4.0 统一启动快照重建包 · R1

> [!CAUTION]
> **本页是旧 Unified R1 重建说明。旧包快启安装已收到软砖反馈。** 请先看 [NX500 原厂按键恢复](../RECOVERY_zh.md) 和 [R1.1 验证包说明](repair/README_zh.md)。

适用：三星 NX500 原厂固件 1.12，已安装完整 Log117（RC6.14.12）和 0.4.0 合并人眼 v2.8 的机身。维护者已确认开发机合并测试成功，同一次普通冷启动的人眼框和 EV+AEL 菜单均正常。首次安装或其他版本先使用完整包原安装流程，再进行本代统一重建。本包在机身上重新制作本机的快照，ZIP 内没有预先写好的跨机身镜像。

统一入口为原厂 `hib_post.service` → `nxl-full040-resume.service` → 根分区 `wake.sh`。一个入口依次核实恢复的相机进程、启用人眼、启动主菜单按键监听。运行不依赖 SD 挂载回调；旧 `nx-on-wake` 主监听的自动启动链接也会归档，避免旧的 SD／机内回调继续抢先启动菜单。人眼算法、主菜单和色彩引擎沿用已验证二进制。

## 准备 SD

先解压完整包。已有本代状态时，双击 `unified-rebuild/PREPARE_SD.cmd` 并输入自己的盘符和 exFAT 卷序列号；或在该更新目录运行下列命令，将 X:、ABCD-1234 换成自己的值：

```powershell
python -X utf8 .\coexist_rebuild.py PLAN --drive X: --serial ABCD-1234
python -X utf8 .\coexist_rebuild.py PREPARE --drive X: --serial ABCD-1234
```

PREPARE 先校验包，再把旧 `nx-*`、`nxl-*` 测试目录、NXDEMO51 日志、旧固件 NX500.bin、旧安装入口和回退脚本整项移入卡上一个 `NX500_ARCHIVE_时间` 目录。没有永久删除。DCIM 照片录像、ALBM、config、SYSTEM、looks 及未知个人文件保留。根目录仅保留这些资源、归档目录、新重建目录及本轮三个安装触发文件。旧日志目录整项归档，不遍历删除其内容，也不进行文件系统修复。若系统连整目录移动也拒绝，安装入口不会发布，保留错误记录。

## 机身重建

电池充满，语言 English，实体拨盘 M，Auto Display Off 5 分钟，Power Save 10 分钟。关闭无线，正常关机后卸镜头并盖机身盖。安全弹出卡、拔 USB。若正常取景超过 60 秒且卡灯已熄灭、未出现重建提示，再正常关机，灯灭后取电池约 30 秒，装回开机一次。

机身先识别同代安装和完整 mod 文件，备份并逐字节比较整个现有 150 MiB 快照，保存配置归档，归档旧状态和运行文件，再安装统一入口。沿用社区已使用的原厂 rescue 重建路径。镜像制作那次启动在原厂程序前置检查和首次改写镜像头前都核验实际服务图；缺少联合依赖即停止。本包不会另行调用先擦除 80 MiB 的原厂独立维护命令。

看到 **FAST START: WAIT 5 MINUTES / KEEP POWER** 后，至少等 5 分钟且卡灯熄灭，再取电池 10 秒并装回；静置至少 4 分钟且卡灯熄灭，拨到 OFF，取电池 5 秒，装回开机。持续闪灯或黑屏时保持供电并停在该步骤。

## 验收

原厂图标和 MENU 恢复后，关机装镜头。确认根目录三个安装入口已被机身收进本轮日志。进行一次普通冷启动，同一次开机用 A-AF＋一般人脸检测观察清晰正脸：松开快门 10 秒、半按 10 秒；随后 EV+AEL 打开主菜单并退出。两项都实际出现才算通过。

```powershell
python -X utf8 .\coexist_rebuild.py STATUS --drive X: --serial ABCD-1234
```

本轮 `NXDEMO51/FAST_START_EYE_AF_ROOTLIB_*/resume/` 应有 `normal-start.txt` 的 `EYE_AND_MENU_READY`、`full-menu.txt` 的 `LISTENER_INPUTS_OPEN`、人眼计数增长及两份 `decision=PASS` 的制作回执。安装登记成功、电脑回放通过均不能代替普通冷启动实机验收。

## 回退

本轮目录保留 `mmcblk0p9-before.img`、逐字节比较凭证、`startup-before.tar` 及其 SHA-256；快照自身的 SHA-256 由电脑端 STATUS 计算，因为机身现有哈希工具有 128 MiB 上限。机内原状态及原运行目录分别保存在原路径加 `.previous-时间_PID` 的位置，被替换的原厂脚本和服务保存为 `.pre-full040-时间_PID`。进入 rescue 之前的准备失败会撤回本轮修改。进入原厂制作流程后，不自动改写镜像或尝试盲目恢复；应先读回本轮日志，使用这台机身的验证备份恢复。不可把其他机身的镜像写入本机。

R1 的开发机合并测试已获维护者确认；电脑验证范围见随包记录，其他机身仍应独立验收。
