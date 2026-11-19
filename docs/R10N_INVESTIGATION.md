# R10n 调查记录

## 已确认

- 本地分支 codex/r10l-stage-trace，初始 HEAD 为 36cd0a560bfccfba54c8e7f4edde46274a9ba76b；工作区干净。检查了仓库、其父目录及当前工作区适用位置，没有找到 AGENTS.md。
- SD 仍为 H:、exFAT、可移动卷，序列号 4040321079（F0D26837）。没有进行文件系统修复。
- 本次读取的 install.log 最后一次安装 revision 为 recovery-r10m-passive-sampling，INSTALL_RUNTIME_COHERENT 的模块 manifest 为 ab7fcb0ff2b0a3843b32ab67caa923ac12668ffdbadf01e5d561c958ef87899e，与已校验 R10m ZIP 一致，之后有 INSTALL_COMPLETE SUCCESS。
- 安装触发文件 install.sh、nx_cs.adj、info.tg 在检查时都不存在。SD 中菜单、采集器和启动脚本与 R10m 包相符。这证明新版已安装；没有直接读取相机 RAM 中进程的 executable，不能把 SD 文件哈希当作运行中进程身份。
- 最新 VIDEO_STAGE_1794485253_432 有 READY（pid=444，record_bytes=1280，capacity=6000），无导出或 DONE。最近三组 VIDEO_FULL_* 均成功返回 NATIVE_CROP_OR_QHD_CONFIGURATION、RTOS_entered=0，不是旧 R10l 的半途 RTOS 挂载记录。
- 用户确认最新现象是“按开始时提示失败，相机仍可操作”。不再将这次失败描述为卡死。
- R10m 采集器设置并输出 period_ms=100。菜单 video_probe_state.h 只接受 200、500、1000；menu_video.inc 在解析失败时将 probe_status 清零，下一次按键又发送 start。这一链路已经用真实 ARM 输出和菜单函数复现。

## 分段检查

| 环节 | 证据与判断 |
| --- | --- |
| 菜单按键 | STAGE COUNTERS 依据 probe_status.state 选择 start 或 stop；错误清零使 stop 不可达。 |
| Shell 入口 | 校验采集器身份、检查 --status/--ready、启动后台采集器、等待匹配 report 的 ON。R10m 不调用 fullwidth trace。已有 shell 测试没有把返回结果交给菜单解析器，因此漏掉了边界缺陷。 |
| 采集器启动与固件校验 | READY 的写入点在版本、三库哈希、ROM/计时代码校验、RAM 分配及预热读取之后；最新 READY 表明有一次采样运行到达这里。READY 文本本身没有版本字段。 |
| mmap / pread / MMIO | 读取通过只读设备/进程描述符；物理地址有范围限制，RAM 加载有 SIGBUS/SIGSEGV 保护。模拟器不验证实际设备映射、总线和驱动行为。没有证据要求本次改变这些路径。 |
| READY / 状态发布 | READY 与 current 发布发生在循环前，菜单必须接受后续 100ms 状态；这是此次可确定的缺陷。READY 单独存在不证明之后持续采集成功。 |
| 循环与导出 | 快速读取保持 100ms，较重快照独立退让；stop 在空闲后保存三文件和 DONE。实际 ARM 模型可完成此链路。菜单拒绝状态足以阻断正常 stop；原相机 RAM 已无法从 SD 恢复，所以无法证明最后一轮实际采集了多少条。 |

## 最小修复及未决项

R10n 仅让菜单接受 100ms，并保留 200、500、1000 兼容性和严格字段检查；更新可见版本文字。保持 R10m 采集器、启动 shell、全宽配置和 RTOS 字节不变。部署时补充 R10m 所有权 manifest、重算依赖哈希、先备份被替换文件、最后写安装触发文件。

首轮实机只做照片模式下开始—5秒—停止保存，确认版本、ON、三文件和 DONE。若仍失败，按安装/菜单状态、采集中断、停止导出三个阶段分别取证；不要同时更改 MMIO、RTOS 和存储路径。旧 SD 读取错误是独立未决问题；新读写遇到错误应停止激活并报告具体文件，保留卡上数据。

根因表述的范围：已证明存在可复现的菜单协议兼容性缺陷，且与最新症状吻合；尚未通过相机短测证明该缺陷是本次唯一原因。R10l 原有卡死、当前全部硬件路径和超采样帧率瓶颈仍未得到实机排除或修复证明。
