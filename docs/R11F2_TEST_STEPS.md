# R11f2：原厂 mv 兼容修复与 R11f A/B/A

R11f1 确实执行，但原厂 mv 6.9 对有硬链接的目标先调用 unlink，出现 Input/output error，原库未被替换。R11f2 保留新旧库实体，改由核对身份的专用 ARM 工具执行一次 rename，提交与回滚均无预先 unlink、无复制回退。若 rename 本身失败，停止并保存原始 errno、挂载表和内核日志。

1. 安全断开 USB，完整关机后再开机，让 R11f2 安装。
2. 安装结束后，再完整关机并开机一次以加载新库。界面仍为 `R11f HEVC A/B/A TEST`。若仍提示失败，保留记录并连接电脑检查，不反复安装或应用全宽。
3. 新安装日志版本应为 `recovery-r11f2-single-rename`，首次替换应有 `NXL_ATOMIC_RENAME committed single_rename=1 pre_unlink=0`、正确模块清单与 `INSTALL_COMPLETE`，同次会话不得随后失败。已是目标库时允许 `already_expected=1` 而不再次替换。写卡成功不代表相机已安装；实际加载版本还需冷启动后的采样入口校验。

安装成功后，按下面原定的一次 120 秒 A/B/A 测试；不做码率对比。采集器、菜单、HEVC 门控库、RTOS、event10、bit6 和首末绑定均与 R11f 逐字节相同。

一次全宽录像即可：稳定录像预热约 10 秒 → A1 正常编码 30 秒 → B 约四分之一普通帧送入 HEVC 30 秒 → A2 恢复正常编码 30 秒。三段自动切换。录像 120 秒覆盖全部阶段和恢复余量。

1. 按上面的安装步骤完成完整关机／开机，断开 USB。采集器会另外核对实际内存入口。
2. 补丁视频页应显示 `R11f HEVC A/B/A TEST`。照片模式应用 `FULL WIDTH 4K 30P`，按提示在原厂菜单重新选择 NTSC／UHD30p／PRO，再回照片模式。BUS QOS 保持原先默认，不更改码率。
3. 启动 `STAGE COUNTERS`，应为 `READY / STOP+SAVE`。如提示 RESTART_REQUIRED、UNAVAILABLE 或其他失败，保留记录，勿反复应用全宽。
4. 退出菜单，进入视频取景约 5 秒，再录像 **120 秒**。保持机位、焦距、曝光、白平衡、对焦不变，不切换输出或 BUS QOS。A1、B、A2 自动执行，无需中途操作。B 的测试视频可能减帧或音画不同步，不能用视频表观帧率评价加速。
5. 停录后等 5 秒，回照片模式查看 STAGE COUNTERS。完成应显示 `DONE A/B/A / SAVE`；若为 ABORTED，仍停止并保存，不重跑。
6. 停止并保存 STAGE COUNTERS，明确看到 `SAVED / TEST COMPLETE / SAFE TO CONNECT USB` 后再接 USB。ABORTED／INCOMPLETE 也要等保存成功提示后接 USB。

保留新 VIDEO_STAGE_* 目录及对应 MP4。需要四个 .bin、READY、DONE、probe.log 和安装日志。正常录像前后均恢复全量编码；门控默认关闭，控制失联或命令无效时自动放行，租约最长 1.2 秒。

分析使用实际入编码计数、原厂 HAL 成功输出计数和 RTOS 周期；每段排除开始 3 秒与结束 0.5 秒的切换区域。B 段实际编码输出必须显著下降，RAW／输出路线和频率必须可比，A2 必须恢复，才判定干预有效。

若有效干预显著缩短 bit6→bit2，指向编码侧相关的访存／缓存／缓冲信用／调度这一组因素，不直接宣称唯一 DDR 根因。若干预有效但周期几乎不变，将降低 HEVC 输入负载的优先级，转向 RSZ2／WDMA2／EP 内部处理。一次 A/B/A 还需要人工核对路线快照、QOS 和视频收尾。

原厂库备份：`/usr/lib/libmmfcore.so.nxl-r11f-oem`。本包卸载／回滚入口会先恢复验证过的原厂库；恢复后也需要完整关机再开机。未知库或未知备份会被拒绝覆盖。
