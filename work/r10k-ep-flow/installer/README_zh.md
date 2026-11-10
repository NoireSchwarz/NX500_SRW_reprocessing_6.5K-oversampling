# NX LUT Lite · Demo51 Recovery R7 修复补丁

releasecode：NX-LUT-Lite-0.4.0-demo51-recovery-r7-patch。适用于 NX500 1.12 和现有 R6/R5/R4 安装。保留你已补好的 nx-lut-demo/assets，本补丁不重复分发原片。

## 安装与本轮操作

1. 正常关机，把 install.sh、info.tg、nx_cs.adj、整个 nx-lut-demo 合并覆盖到 SD 根目录。保留 assets 和 NXDEMO51。
2. 正常开机安装，确认 NXDEMO51/logs/install.log 中有 install_revision=recovery-r7 和 INSTALL_COMPLETE。
3. 关开机、开启蓝牙，EV+AEL → SRW → CHECK RAW RESET（或显示 VERIFY RESET 的继续项），点一次。它只读取资源；通过后显示 RAW SESSION READY，先正常拍一张原厂 JPEG，再 PREPARE RAW。失败时回传当前 JOB/rearm_… 文件夹及 logs/rearm.log，不需要原片。
4. CAMERA RECOVERY 仍是取景恢复入口。取景需要恢复时点一次；回传新的 recovery/RESTORE_… 文件夹及 logs/restore-camera.log。两项入口的结果分开记录。

安装只更新程序。精确匹配的、所属进程已经不存在的旧 Recovery 锁可允许更新控制面，但其文件会完整保留；更新成功不等于锁已解除或任务已可运行。若新日志报告锁仍被保留，保留现场，不反复点恢复。

## 本次修复

原恢复程序的 ro32 实际通过四次字节读取访问 ISP 寄存器。发布 ARM 指令也确认为 LDRB，这与已验证的对齐 32 位寄存器访问路径不一致。R7 分离 RAM/代码读取与严格白名单的 32 位 MMIO 读取。

本次上传的 restore.log、trace_events.txt、snapshots.jsonl 均为零字节，只写出了 camera_maps_before.txt。它们不能确定实际故障地址，所以本版同时增加读取前后的阶段、地址和访问宽度记录，以及受限只读映射访问的同步故障处理。不可读寄存器会形成失败报告，不会被当成恢复成功。

原厂单次提交、调用帧恢复、detach、新鲜启动完成和稳定 Liveview 判据保持。原片解码、ISP、JPEG422 和 R6 耗时优化保持字节不变。菜单新增 CHECK RAW RESET；会话管理器增加独立 proof lease，只允许经核验后重新准备全新的 SRW 任务。

## 必须区分的两件事

CAMERA RECOVERY 只请求恢复取景，不释放显影 CAP，也不修改 SESSION/STATE.journal。REAL_KERNEL_RESTART_NOT_VERIFIED 由 CHECK RAW RESET 单独检查：只受理尚未开始的、从上一完整任务继承来的重置门槛。它重新核对上一任务权威 journal、前后段旧分配范围、完整回调槽及缓冲引用、内核和硬件当前资源状态；只有全部证明资源已退休，才追加 NEED_STILL_INIT。不会伪造 boot ID 变化，不复用旧 YUV，也不会凭“完成”或旧 PID 消失解除 UNKNOWN。

这是一条保守的资源验证路径，不能保证这一轮就放行。如果旧缓冲仍被引用或寄存器/内核状态无法确认，会保持隔离并记录具体失败项。

电脑故障注入与 ARM 验证不能代替机身验证。R7 的实际恢复效果仍需这一轮记录确认。
