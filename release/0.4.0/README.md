# NX LUT Lite 0.4.0 · NX500 1.12

本包合并了 Log117 完整 mod（内部版本 RC6.14.12）与 Eye AF v2.8。完整 mod 提供 LUT、V-Log、SRW 重显影、无线相簿和原厂即时图传。快启恢复后调用原有菜单监听。EV+AEL 打开主菜单，双击 EV 打开 LUT 详细页，EV+Wi-Fi 切换 V-Log。眼部对焦随原厂一般人脸检测启用。

公开包去除了菜单断言中的编译机目录，锐度助手通过原厂库名查找依赖，两项的 ARM 指令与相机已用的版本相同。快启入口同时识别实测版本和公开版本各自的完整清单。已有实测版的机身可直接重建。

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

## 合并短测

1. 确认原厂图标和 MENU 正常。关机装镜头，再开机。按 EV+AEL，确认完整菜单出现并可返回取景。双击 EV，确认 LUT 详细页出现。
2. 用 A-AF 和 Face Detection「Normal」对准光照良好的单人正脸，松开快门看 10 秒，再半按 10 秒，最后让人离开画面。快启记录中的 `same_capture_process=1`、`ADAPTER_ENABLED_AFTER_RESTORE` 和 `schema=28` 可核对同一进程内的人眼模块。菜单就绪记录为 `resume/full-menu.txt` 的 `LISTENER_INPUTS_OPEN`。
3. 拍摄停止时，用 EV+Wi-Fi 开 V-Log，选择「純Log」，短录后停止，再关 V-Log。开发机最近一次关闭时已提交原厂 Gamma 并释放 LUT 状态，但最后一次 LUT 重载返回 `GAMMA_LUT_RELOAD_UNCONFIRMED`。出现该提示请记录，不要把它当作已恢复的凭证。此前亦有完整恢复成功记录。

## 验证与恢复

Metol 的安装脚本固定了 RC5 的 Gamma 引擎。在工作输入 2965，旧目标为全范围 8 bit 132，新版为 117。26 mm、透射镜、关闭创意 LUT 的 327 帧录像均值为 116.16。透射实测支持这台机身的相对曝光标定。数值与范围见 `gray-calibration.json`。

Eye v2.8 的电脑回放中，89 次临时整脸框降至 0，98 组亮度、色度统计一致。一台 NX500 的三张照片均将机内保存的对焦区落在一只眼睛上。逐张光学焦平面以及整包在其他机身的表现仍需各机身实测。

此前一次失败重建后，在机身完全关机、卡灯熄灭后取出模组卡，无卡开机恢复了原厂 UI。保留原卡记录。若一直闪灯、无法关机或无卡也无法启动，请联系原厂维修。若能够运行原厂安装入口，使用本包的 `stage-postflash.py CLEAR`（同样指定盘符与卷序列号）可撤下本代服务，恢复已识别的上一代入口；卡上回执为 `OWNED_CAPTURE_DISABLED`。要还原镜像本身，请使用这台机身的已验证备份。详见 `eye-v2.8/INSTALL.md` 和其中的原厂恢复说明。
