# NX500 SRW Reprocessing and 6.5K Oversampling Research

面向三星 **NX500 1.12** 的 LUT、SRW 显影与全宽 4K 研究项目，使用 RAM 补丁和原厂图像处理接口。

## 当前状态

- **SRW**：自动初始化 → BayerStill → LDC → 可选 EP 3D LUT → JPEG，复用一次申请的 192MiB 工作区；源片白平衡、颜色矩阵、C0 19 点 Gamma 和 JPEG 量化表已接入。
- **镜头矫正**：16–50mm Power Zoom，531 个 MakerNote 焦距码档位；含原生锚点和离线插值，未宣称每一档都与原厂像素等价。
- **全宽 4K**：全宽 UD 路径结合缩放 setup 输出 3840×2160，80Mbps、8 位 HEVC 4:2:0。用户已确认视角，蓝边已在 R10c 修复；**真实稳定 29.97p 尚未实现，持续掉帧仍在定位**。“6.5K”描述全宽输入链研究，不表示相机输出 6.5K 视频，也不单凭读出窗口断言每个感光点均参与。
- **R10k**：原生 DCI24、原生 UHD30、全宽 UHD30 使用同一套硬件启动／装载／开帧／完成事件计时。电脑验证通过；相机测试待进行。
- **EP 复核已合并**：同一记录器以双读、IPC 活动头和模式夹取采集辅助 RDMA1／WDMA1 两个配置 bank，关联原生 EP 中断总数和三模式稳态计时。采用被动通路对照；尚无已确认的独立关闭入口，**未实现 EP bypass 开／关 A/B**，也未宣称修复掉帧。

## 上游与致谢

本项目基于并参考以下社区工作，保留原作者的版权和许可证：

- **[WApaperplane/metol](https://github.com/WApaperplane/metol)**：LUT 格式、转换与写入工具，以及 NX500／NX1 上的实现经验。本项目保留派生工具的来源标记；早期基线记录为提交 `cb82f4886c1174261cbba8127000c7b696944ff4`。菜单焦点归还、物理半按和 NXUI 交互经验也参考 metol；本菜单独立绘制。
- **[ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)**：NX500／NX1 社区逆向资料、按键事件／keyscan 定义，以及 SD 脚本与模组接入方法。本项目没有把这些社区基础工作声称为原创。

具体派生文件、改动、字体和其它组件来源见 [NOTICE.md](NOTICE.md) 与 [STRENGTHS_NOTICE.md](STRENGTHS_NOTICE.md)。两项上游均采用 AGPL-3.0；本项目派生代码按 [GNU AGPL v3](LICENSE) 分发，第三方字体／头文件等仍遵循各自许可证。

## 目录

| 路径 | 内容 |
| --- | --- |
| `work/r10k-ep-flow/` | 当前 R10k 源码：菜单、Linux 与 RTOS 前后段、资源契约、LDC／JPEG、视频诊断、测试代码 |
| `work/*r10k*.py`、`work/r10j_timing.S` | 本轮构建、ARM 回放与打包入口；RTOS 计时代码与 R10j 相同 |
| `docs/` | 开发交接、当前状态和测试步骤 |
| `reports/` | 实机结论摘要与电脑验证记录；两者明确区分 |

## 构建与复现

主机使用 Python 3、Zig 0.13.0、ARM soft-float musl 交叉工具链；ARM 回放需要 Unicorn、Capstone、pyelftools。工具链和原厂专有运行库不随仓库分发。

```powershell
python -X utf8 work/build_r10j_timing.py
python -X utf8 work/build_r10j_rtos.py
python -X utf8 work/build_r10k.py probe
python -X utf8 work/build_r10k.py menu
```

现有研究脚本沿用原工作目录布局：工具链放在 `work/tooling/`，自行取得的 NX500 1.12 `P7.bin` 放在 `work/firmware/`。完整历史回归和发布脚本还依赖本地历史包、原始夹具、对照固件及 LUT 资源，仓库不伪装成不带这些输入也可全部执行。具体依赖、固件散列与验证边界见交接文档。R10k 的全宽和 RTOS 二进制冻结为 R10j；完整发布仍需本地封存的 R10j 基线包。

源码目录不是可直接复制到 SD 卡的完整安装包；相机部署使用经过清单与身份核验的发布包。

## 交接与证据

- [开发交接正文](docs/HANDOFF_20261110.txt)
- [PDF 交接](docs/HANDOFF_20261110.pdf)
- [当前状态与 EP 复核修订](docs/CURRENT_STATUS.md)
- [R10k 合并测试步骤](docs/R10k_TEST_STEPS.txt)
- [R10j 历史测试步骤](docs/R10j_TEST_STEPS.txt)
- [R10i 持续掉帧结论](reports/R10i_BUS_QOS_ABA与周期阻塞诊断_20261110.txt)

交接中的“EP 已排除”是当时根据用户说明记录的历史状态；用户随后撤回该判断，当前以 `docs/CURRENT_STATUS.md` 为准。尚未证明 NX500 的不可改变硬件上限；单一 QoS 候选无效也不能排除全部 DDR／内部握手因素。

原厂固件／rootfs、机身共享库、个人 SRW／JPEG／视频、整卡原始日志、RAM 快照、第三方 LUT 和本机工具链留在本地；本仓库提供项目源码、必要的参数事实、软件测试夹具及总结资料。
