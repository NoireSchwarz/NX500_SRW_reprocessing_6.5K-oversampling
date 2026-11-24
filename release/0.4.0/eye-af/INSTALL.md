# Installation and recovery · 安装与恢复

**Experimental, for NX500 firmware 1.12 only.** This path modifies the camera application service and asks the manufacturer's rescue sequence to rebuild the hibernation image. Losing power while storage is active can leave the camera unable to start. Allow time for recovery. Keep your own verified pre-change image, known-good firmware, SD media and an independent copy of your photographs before considering a rebuild. The installer does not create a fresh hibernation backup. Consult the [Samsung NX500 support page](https://www.samsung.com/uk/support/model.EV-NX500ZBMJDE/) for the official firmware for your body.

1. Unzip the standalone release into a PC folder. Use a clean exFAT card, Windows and Python 3.9+. Find its actual drive and serial with `vol X:` in Command Prompt. Replace both examples below. Existing or unrecognized launch entries cause a stop. Preserve any existing mod card.
2. With the device awake, first run the read-only camera preflight. Staging writes only the selected package and request to the card.

   ```powershell
   python .\stage-postflash.py CHECK --drive X: --serial ABCD-1234
   ```

   Boot the camera with the prepared card and let CHECK finish. Read the newest `NXDEMO51/FAST_START_EYE_AF_ROOTLIB_*/result.txt` on the PC. Proceed only after `reason=CHECKED_READY`. If an entry or service conflicts, keep it intact and inspect the recorded reason.
3. Charge fully. Choose English, physical M mode, Auto Display Off 5 minutes and Power Save 10 minutes. With power off, remove the lens and attach the body cap. Turn Wi-Fi and Bluetooth off and disconnect cables and accessories.
4. Stage the explicit rebuild and safely eject.

   ```powershell
   python .\stage-postflash.py EYE --drive X: --serial ABCD-1234
   ```

   If ordinary live view continues for a minute with the card lamp off, shut down normally and wait for the lamp to go out. Remove the battery for 30 seconds, reinstall it and start once. Wait for **FAST START: WAIT 5 MINUTES / KEEP POWER**. Only after at least five minutes **and** an extinguished lamp, remove the battery for ten seconds and reinsert it. Wait another four minutes minimum **and** for the lamp to go out. Switch OFF, then remove the battery for five seconds and reinsert it. Start and confirm that the stock icons and MENU work. If a deadline passes while the lamp is still flashing, leave power and card in place.
5. Shut down to attach the lens. Use A-AF, normal face detection and a single well-lit frontal face. Check first with the shutter released, then half-pressed, then with the subject out of view. Successful restore is recorded under `resume/runtime.log` as `same_capture_process=1` and `ADAPTER_ENABLED_AFTER_RESTORE`. Check `resume/eye-maps.txt` and `schema=28` in `resume/eye-af.status`. A card copy or an OEM rescue request alone does not establish activation.

## Recovery seen on the test camera

- If a screen goes black or the card lamp keeps flashing, keep power connected to the battery and let an active write finish. A timer expiring by itself is insufficient grounds to interrupt it. If the body will not shut down or access never settles, use manufacturer service assistance. Avoid cycling power repeatedly.
- Once the camera is fully off and card access has stopped, remove the mod SD and start once without it. This returned the original UI on the test unit after an earlier failed rebuild. Keep the suspect SD out while testing. Use a reader to preserve its `NXDEMO51` records and the known activation files. A card with a pending request may start the operation again.
- To disarm a recognized v2.8 installation, use `python .\stage-postflash.py CLEAR --drive X: --serial ABCD-1234` from the same public package. The expected result is `reason=OWNED_CAPTURE_DISABLED`. It archives this version's recognized launcher and state, and restores a recognized previous wake entry when present. A saved hibernation image is a separate on-device state. CLEAR does not rewrite that image.
- The tester also recovered the original fast-start image and later performed an official stock firmware reinstall during development. No-card startup is not guaranteed on every unit. Use your own verified backup or the official model-specific recovery process. A copy of another camera's hibernation image is unsuitable.

## 中文速查

**重建会改写机内快启状态，曾出现持续黑屏、闪灯和开机后原厂界面消失。** 使用 NX500 1.12、本人已核验的原厂备份、满电的电池和独立 exFAT 卡。先运行 CHECK 并确认 `CHECKED_READY`，再运行 EYE。拨盘 M、语言 English、自动熄屏 5 分钟、省电 10 分钟、关无线，关机卸镜头并盖机身盖。上面的两段等待都要同时满足时间和灯灭。持续闪灯时保持供电。

我们在测试机上实际用过的恢复办法，是等机身完全关机、访问停止后拔出测试卡，再无卡开机，原厂 UI 恢复。先隔离原卡、保存日志，避免残留入口再次触发。CLEAR 会停用能识别的本版入口，已保存的快启镜像另行恢复。测试机也曾恢复原厂镜像并重装官方固件。遇到持续写入或无法安全关机，可联系三星维修。发布包不附设备快照。
