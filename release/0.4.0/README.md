# NX LUT Lite 0.4.0 · NX500 1.12

> [!CAUTION]
> **收到 Unified R1 快启安装后软砖的反馈，暂缓执行 R1 的 `EYE`／`REBUILD`。** 社区实测的 [NX500 原厂按键恢复](RECOVERY_zh.md) 适用于原厂界面或 USB 已无法使用的情况。使用独立的 FAT32 卡及官方 1.12 固件，保留原卡和旧镜像。

本包合并了 Log117 完整 mod（内部版本 RC6.14.12）与 Eye AF v2.8。完整 mod 提供 LUT、V-Log、SRW 重显影、无线相簿和原厂即时图传。快启恢复后调用原有菜单监听。EV+AEL 打开主菜单，双击 EV 打开 LUT 详细页，EV+Wi-Fi 切换 V-Log。眼部对焦随原厂一般人脸检测启用。

公开包去除了菜单断言中的编译机目录，锐度助手通过原厂库名查找依赖，两项的 ARM 指令与相机已用的版本相同。快启入口同时识别实测版本和公开版本各自的完整清单。已有合并快照的机身见下文的恢复依赖和制作端核验。

## Unified R1 · 合并测试成功

维护者确认：开发机同一次普通冷启动中，人眼框和 EV+AEL 主菜单均正常。电脑端另通过 30 组用例与 8 组原厂 ARM 回放。当前附件为完整安装包，新增 `unified-rebuild/` 同代重建目录，并保留原主模块与首次安装内容。

已有本代 0.4.0 合并快照的机身，在完整 ZIP 解压目录使用自己的盘符及 exFAT 序列号：

```powershell
python .\stage-postflash.py REBUILD-PLAN --drive X: --serial ABCD-1234
python .\stage-postflash.py REBUILD --drive X: --serial ABCD-1234
python .\stage-postflash.py REBUILD-STATUS --drive X: --serial ABCD-1234
```

REBUILD 先归档旧 SD 测试目录和日志，再准备本机原厂快照重建。详细步骤见 [统一重建流程](rebuild/README_zh.md)。已有状态不再反复执行首次 EYE。首次安装先按下方原流程安装主模块和首次 Eye 快照，形成 0.4.0 状态后，再使用 REBUILD 纳入统一唤醒入口。不要只复制重建更新目录给未安装主模块的机身。

## 两步安装

1. 下载完整 ZIP 和同名 SHA-256 校验文件，在 Windows 电脑解压。先用 `Get-FileHash` 核对 ZIP。**支持三星 NX500 原厂固件 1.12。** 先保存照片、现有 mod 状态及这台机身自己的可用快启备份。包中保留上一公开版已随附的 SRW 显影测试输入。每台相机用原厂流程生成自己的快启镜像。
2. **安装完整 mod。** 已有成功的 `release-0.4.0-rc6.14.12-log117` 安装记录可直接进入第 3 步。首次安装时，把解压目录 `module` 里的内容放到准备好的 SD 卡根目录。已有安装触发文件时先完成或确认它们的来历。安全退出卡，正常关机、灯灭后取电池约 30 秒，再装回开机。等待安装结束，核对安装日志的版本及成功结果。完整 mod 与快启使用同一组 SD 安装文件名，请分两次准备。
3. **核对再重建。** 在完整 ZIP 的解压目录运行下列命令。将 `X:` 和 `ABCD-1234` 换成实际 exFAT 卡盘符与卷序列号（可用 `vol X:` 查看）。CHECK 先让机身核对固件、既有入口、完整 mod 的 133 项文件及空闲状态。

   ```powershell
   python .\stage-postflash.py CHECK --drive X: --serial ABCD-1234
   ```

   取卡触发原有安装流程，读回最新 `NXDEMO51/FAST_START_EYE_AF_ROOTLIB_*/result.txt` 的 `reason=CHECKED_READY`。EYE 自身也会先执行核验。若看到 `FULL_MODULE_INSTALL_REQUIRED`，先完成第 2 步。若报告文件不匹配，保留原状并核对文件身份。
4. **重建前准备。** 电池充满，语言 English，实体拨盘 M，Auto Display Off 5 分钟，Power Save 10 分钟，关闭 Wi-Fi 和蓝牙。关机后卸镜头并盖机身盖。在同一个解压目录写入重建请求。

   ```powershell
   python .\stage-postflash.py EYE --drive X: --serial ABCD-1234
   ```

5. 安全弹出并拔线。若正常取景超过 60 秒、未提示且卡灯已灭，正常关机，灯灭后取电池 30 秒，再装回开机一次。看到 **FAST START: WAIT 5 MINUTES / KEEP POWER** 后至少等 5 分钟，且写卡灯熄灭，再取电池 10 秒并装回。静置至少 4 分钟且灯灭，拨到 OFF，取电池 5 秒，装回开机。**若持续闪灯或黑屏，保留供电和卡，停止后续拔电池步骤。** 写入时断电可能导致机身无法正常启动。

## 已安装 0.4.0 的机身，检查 SD 挂载入口

这是针对已安装 0.4.0 合并快照和联合唤醒程序的诊断。Windows 工具保存原件，在项目卡上写入 `nx-on-wake/on-wake` 并核对读回内容。要让这个文件执行，机身必须已经在运行 SD 挂载监听。独立版与整包带有相同的监听程序，只能证明文件版本相同。已在机身观察到监听停止的情况下，重复写入这个文件无法启动监听。

电脑上的 `SIMPLE-WAKE` 只写卡，`SIMPLE-STATUS` 读取本轮回执，`SIMPLE-RESTORE` 恢复卡上原件并保留记录。这三项操作的 `camera_install_requested` 均为 `false`。状态里的 `sd_callback=trial` 只表示卡上选中了试验脚本。只有机身写回与本轮标识吻合的 `entry=sd_mount`，工具才报告 `camera_callback_receipt=entry_recorded`。这项回执也只证明机身执行了回调。

```powershell
python .\stage-postflash.py SIMPLE-WAKE --drive X: --serial ABCD-1234
python .\stage-postflash.py SIMPLE-STATUS --drive X: --serial ABCD-1234
python .\stage-postflash.py SIMPLE-RESTORE --drive X: --serial ABCD-1234
```

每轮最多保留三次机身调用记录，包含相机时间和开机秒数，可用于分辨拔 USB 后的重新挂载。验收眼框与菜单时，使用安装入口已经撤下的普通冷启动。若没有本轮回执，请先回查原挂载监听的机身记录。

## 已有合并快照的核对

开发版 Windows 安装器增加了只读的 `REBUILD-CHECK`。将盘符和序列号换成项目卡的实际值。它只读取卡上的回执，可在相机使用 USB 存储连接时运行。

```powershell
python .\stage-postflash.py REBUILD-CHECK --drive X: --serial ABCD-1234
```

输出包含已有快照的启动标识、登记时和恢复后的 `Wants`，以及卡上还存在的安装入口。退出码 3 表示已识别已有快照，所读回执不足以许可新一次写入。已有合并快照走首次安装的 EYE 操作时，也会在改动卡上的包或请求前报告这个状态。

此次修正把人眼与菜单使用的同一个恢复服务纳入 0.4.0 构建。原厂 `hib_post.path` 触发 `hib_post.service` 后，服务管理器按依赖顺序调用 `nxl-full040-resume.service`。它使用已经安装的人眼与菜单程序。磁盘上存在依赖文件和已经恢复的服务管理器实际引用此服务是两项独立的检查。

制作 NX500 1.12 文件系统时，`compose.py --image-partitions PLATFORM_TAR OPT_USR_TAR` 在现有三个位置参数之后接收两个已包含 0.4.0 的分区归档。它在输出目录 `image-partitions/` 生成新的一对归档。系统分区保存恢复依赖、写入前核对程序和受保护的原厂写入脚本。`opt-usr.tar` 对应独立挂载的 `/opt/usr`，保存联合唤醒程序。构建会校验原厂版本及已有组件，保留旧快照记录，并在两个输出都通过读回后发布。只有服务文件的 `services.tar` 和包含原厂写入保护的 `rootfs.tar` 有不同的文件名。

解压包里的原有电脑安装器也能完成这项制作。两个输入分别以系统根和 `/opt/usr` 为归档根。输出目录须为新目录。

```powershell
python .\stage-postflash.py BUILD-RESUME-IMAGE --image-partitions .\platform.tar .\opt-usr.tar --image-output .\prepared
```

写镜像前与写镜像头前都查询当次开机的服务管理器。独立的原厂维护命令在已有镜像时会先擦除再重新开机，受保护的流程在这次擦除前停止。上述分区归档是镜像制作者的输入。相机通过 USB 暴露的是 SD 卡，现行 SD 安装器对已有合并快照执行 EYE 时会返回检查状态。把归档存入 SD 卡不会安装它。

正式重建的验收需要制作该代镜像时的写入前回执。旧安装成功和旧镜像的恢复记录都不能替代它。获得新镜像和同代回执后，先核对卡上没有安装入口，再正常关机，等灯灭后取电池约 30 秒，装回普通开机。用 A-AF 和一般人脸检测对准一张正脸，松开快门、半按快门各观察 10 秒，再按住 EV 点按 AEL，实际打开补丁主菜单。退出、灯灭后接回 USB，核对这一次产生的人眼计数增长、菜单监听和原厂恢复事件回执。

## 合并短测

1. 确认原厂图标和 MENU 正常。关机装镜头，再开机。按 EV+AEL，确认完整菜单出现并可返回取景。双击 EV，确认 LUT 详细页出现。
2. 用 A-AF 和 Face Detection「Normal」对准光照良好的单人正脸，松开快门看 10 秒，再半按 10 秒，最后让人离开画面。快启记录中的 `same_capture_process=1`、`ADAPTER_ENABLED_AFTER_RESTORE` 和 `schema=28` 可核对同一进程内的人眼模块。菜单就绪记录为 `resume/full-menu.txt` 的 `LISTENER_INPUTS_OPEN`。
3. 拍摄停止时，用 EV+Wi-Fi 开 V-Log，选择「純Log」，短录后停止，再关 V-Log。开发机最近一次关闭时已提交原厂 Gamma 并释放 LUT 状态，但最后一次 LUT 重载返回 `GAMMA_LUT_RELOAD_UNCONFIRMED`。出现该提示请记录，不要把它当作已恢复的凭证。此前亦有完整恢复成功记录。

## 验证与恢复

Metol 的安装脚本固定了 RC5 的 Gamma 引擎。在工作输入 2965，旧目标为全范围 8 bit 132，新版为 117。26 mm、透射镜、关闭创意 LUT 的 327 帧录像均值为 116.16。透射实测支持这台机身的相对曝光标定。数值与范围见 `gray-calibration.json`。

Eye v2.8 的电脑回放中，89 次临时整脸框降至 0，98 组亮度、色度统计一致。一台 NX500 的三张照片均将机内保存的对焦区落在一只眼睛上。逐张光学焦平面以及整包在其他机身的表现仍需各机身实测。

此前一次失败重建后，在机身完全关机、卡灯熄灭后取出模组卡，无卡开机恢复了原厂 UI。保留原卡记录。若一直闪灯、无法关机或无卡也无法启动，请联系原厂维修。若能够运行原厂安装入口，使用本包的 `stage-postflash.py CLEAR`（同样指定盘符与卷序列号）可撤下本代服务，恢复已识别的上一代入口；卡上回执为 `OWNED_CAPTURE_DISABLED`。要还原镜像本身，请使用这台机身的已验证备份。详见 `eye-v2.8/INSTALL.md` 和其中的原厂恢复说明。
