# R10n：100ms 菜单状态解析修复

这次失败表现为按开始提示失败，相机仍可操作。SD 安装日志已确认 R10m 安装完成；最新采样目录 `VIDEO_STAGE_1794485253_432` 只有 READY。R10m 采集器返回 `period_ms=100`，菜单解析器只接受 200、500、1000，导致正常状态被拒绝，菜单显示 `COUNTER RESPONSE FAILED - RETRY STATUS` 并将状态清为 OFF。再次按采样键仍会发送 start，无法进入 stop/save。

该缺陷已用真实 ARM 采集器输出和真实菜单 C 控制流程复现。R10n 只修正菜单对 100ms 状态的接受范围，并更新菜单版本文字。采集器、启动脚本、全宽 helper、RTOS 代码和读取路径都沿用 R10m。采集器内部仍显示 R10m 是预期结果；菜单应显示 **R10n 100MS STATUS FIX**。

## 安装与最小短测

1. 完成 SD 上安装包的相机安装，完整关机再开机，断开 USB。
2. 打开补丁 VIDEO 页，确认 **R10n 100MS STATUS FIX**。
3. 保持照片模式，直接按 STAGE COUNTERS。首轮不要执行 FULL WIDTH，不改变其他开关。
4. 应显示 **ON / STOP+SAVE**；等 5 秒，再按同一项停止保存。
5. 等待 **COUNTERS SAVED - SAFE TO CONNECT USB**。重新打开 VIDEO 页，状态应为 OFF。
6. 再接 USB，最新 VIDEO_STAGE_* 应有 READY、counts.bin、frame-flow.bin、hevc-flow.bin、DONE。安装日志里的新模块 manifest 应与本包 BUILD.json 的 module_manifest_sha256 一致。

新装包和关机重开后的菜单文字、相机写回的安装日志以及本次输出文件共同用于确认版本。电脑端 SD 读回校验不代表相机已安装或运行新版。

刚开机没有逐帧计时器时，短测仍应能开始和保存；分析会明确报告计时器不可用，不代表完整帧覆盖。HEVC 断点追踪仍停用，hevc-flow.bin 保留明确的 0x200 停用标记。

## 后续分支

- 开始仍报 `COUNTER RESPONSE FAILED`：记录完整屏幕文字和版本；优先查是否安装了新菜单。不要重复应用全宽配置。
- ON 成功，但停止保存失败：保持相机开机，停止录像并回照片模式、断开 USB后，再按 STAGE COUNTERS 重试保存；记录报错。不要在保存前关机，以免丢失 RAM 证据。
- 相机出现卡死：记录是在按开始、采集中、按停止，还是应用 FULL WIDTH 时发生；保留新目录。这与本次已复现的菜单缺陷需要分开调查。
- 五秒短测成功后，再按 R10m 的流程做全宽与裁切对照，每次只改变一个配置。

PC 分析继续使用 `PC/video-stage-counters/analyze_r10m.py`。R10n 没有改变输出格式。

## 证据范围

本地测试覆盖真实 ARM 菜单解析、下一次按键的 start/stop 选择、真实 ARM 采集器的 100ms 输出及三文件保存；操作系统、管道、进程退出和共享内存边界由模型提供。38 组有效/无效协议检查保留严格拒绝损坏数据的行为。这些结果不证明实机内核、mmap、pread、MMIO、信号处理、SD 文件系统或相机硬件没有其他问题，也不证明 4K 帧率瓶颈已修复。

旧 R10l 卡死证据仍只定位到 READY 之前的 RTOS 挂载事务，没有证明具体卡死指令。旧日志中的 WinError1392 和读取错误保留；未做格式化、清理或文件系统修复，不能将其忽略或直接当作本次原因。
