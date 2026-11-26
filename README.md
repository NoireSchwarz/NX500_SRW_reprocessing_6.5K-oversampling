# NX500 SRW Reprocessing and 6.5K Oversampling Research

面向三星 **NX500 1.12** 的 LUT、SRW 重显影和全宽视频研究项目，使用 RAM 补丁及原厂图像处理接口。

> [!CAUTION]
> **Unified R1 快启安装已收到软砖反馈。请暂缓使用 R1 的 `EYE`／`REBUILD`，也不要叠装社区版 `nx-on-wake`。** 如果开机后原厂 UI 消失、USB 无法连接或卡灯持续闪烁，先看 [NX500 原厂按键恢复](release/0.4.0/RECOVERY_zh.md)。社区已在 NX500 上实测，可用官方 1.12 固件和一张独立的 FAT32 卡恢复。

## NX LUT Lite 0.4.0 · Unified R1.1 验证包

R1.1 把首装和已核验的 R1 更新接入同一套写入前核验。完整包可仅安装 Log117 主模块，也可择一安装 Eye AF v2.8 或上次实测的 YuNet Focus2。新安装流程已通过电脑和原厂 ARM 回放，实机冷启动及快门行为待验收。

[R1.1 预览包与 SHA-256](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/releases/tag/v0.4.0-r1.1-preview) · [安装与短测](release/0.4.0/rebuild/repair/README_zh.md) · [软砖调查](release/0.4.0/rebuild/repair/AUDIT_zh.md) · [交接](docs/CURRENT_STATUS.md)

- **眼部对焦** · 在原厂一般人脸检测中优先选眼。三张照片的机内 AF 区域覆盖一只眼睛。录像、多人、侧脸与暗光的稳定程度仍需实测。
- **Log117** · 同一工作输入 2965 的目标从 132 降至 117；26 mm 透射镜对照录像的稳定段亮度均值为 116.16（全范围 8 bit）。
- **无线连接** · 短按 Wi-Fi 后选择「5 GHz 相簿」「2.4 GHz 相簿」或「2.4 GHz 圖傳」。双码连接。相簿可下载原档及查看相机资讯，2.4 GHz 原厂图传支持浏览器全屏。
- **快捷键** · EV+Wi-Fi 切换 V-Log，双击 EV 打开当前 LUT 详情，EV+AEL 打开补丁菜单。
- **LUT 与视频** · 繁体中文半屏菜单、四项参数及 M3 A/B 混合。OK 装载，半按返回取景。支持全宽 UHD24、UHD 转 2.5K 和恢复原厂 UHD。
- **SRW** · 一键初始化并重显影，输出相册用 JPEG 与缩略图并更新相册列表。

旧 R1 在开发机上曾通过同一次普通冷启动的人眼框和 EV+AEL 测试。已知的关闭 V-Log 后 LUT 重载提示见[历史使用说明](release/0.4.0/README.md)。

## 能力与研究边界

**全宽视频**优先采用原生 24p 的可用路线，用户已自行检查认可 R11g 的使用效果。真实稳定全宽 30p 尚未实现；bit6→bit2 区间的 RSZ2／WDMA2／HEVC／DDR 因果拆分仍未完成，也没有证明不可改变的硬件上限。“6.5K”描述全宽输入链研究，不表示输出 6.5K 视频或已独立证明全部感光点的参与方式。

**Log** 当前为 8bit V‑Log／V‑Gamut 兼容路线，不是 Main10 或厂商认证。已有相对曲线、曝光与颜色研究，物理 18% 灰和源原色／白点的独立标定仍有边界；MP4 原厂颜色标签未自动改写。视频原色监看与 Log 写卡尚未独立分流。

**SRW 与镜头矫正**复用已验证的原厂接口及查表路径。剩余颜色误差和插值不宣称与原厂逐像素等价；正式菜单隐藏诊断开关，不把新观测实验塞进日常重显影。

## 安装与使用

R1.1 中，`module/` 部署 Log117。`install-fast-start.py` 可准备或核对一个快启版本，启动前会检查旧 R1 的真实状态及独立唤醒入口。用包内 `README_zh.md` 的顺序安装。出现软砖时优先使用独立恢复卡。[安装说明](release/0.4.0/rebuild/repair/README_zh.md) · [原厂恢复](release/0.4.0/RECOVERY_zh.md)

进行 SRW 前先还原 V‑Log、手动关闭影片 LUT，沿用既有初始化并重显影流程。不格式化、清理或修复 SD 文件系统，不重复码率对照。

## 目录与构建

| 路径 | 内容 |
| --- | --- |
| `work/r10k-ep-flow/` | 当前组件源码；目录名是历史名称，本版菜单、Wi-Fi 入口、安装器和研究源码 |
| `docs/` | 更新说明、当前状态、交接续篇和各轮研究记录 |
| `reports/releases/RC61410/` | RC6.14.10 的历史验证 |
| `release/0.4.0/` | 整包构建、快启源码、安装说明、灰点数据与合并回放 |

主机采用 Python 3、Zig 0.13.0、ARM soft-float musl；ARM 回放依赖 Unicorn、Capstone 和 pyelftools。工具链、开发者自行取得的完整固件／rootfs 不随 Git 正文提交。菜单和按键可用 `base/nx-lut-lite/tools/build_zig.py --keys-menu-only` 定向重建；参数、外部输入和身份绑定见交接，不批量运行历史生成脚本。

发布脚本依赖既有工作区、原厂依赖和测试支持，不宣称裸仓库无外部输入即可完整复现。修改后必须重建相关组件、重新绑定清单并验证，不能沿用旧 ZIP 的哈希。

## 上游与许可

- [WApaperplane/metol](https://github.com/WApaperplane/metol)：LUT 格式、工具和 M3 经验；RC4 的完整单次即时加载参考固定提交 `b10ec8413ce5f9a03079288cb9880e4d0fac983f`。本项目菜单独立绘制，不将社区基础工作声称为原创。
- [ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)：NX500／NX1 逆向资料、实际按键定义、SD 脚本和模组接入惯例。

项目派生代码遵循 [GNU AGPL v3](LICENSE)，来源、字体及第三方组件见 [NOTICE.md](NOTICE.md) 和 [STRENGTHS_NOTICE.md](STRENGTHS_NOTICE.md)。该许可不改变第三方 LUT、字体、运行库及原厂资源的归属。本版随附已公开的 SRW 初始化资产，完整包文件说明见[本版安装说明](release/0.4.0/README.md)。完整固件、整卡照片录像和用户备份不随 Git 正文提交。

旧 [2026-11-10 交接 TXT](docs/HANDOFF_20261110.txt)、[PDF](docs/HANDOFF_20261110.pdf) 和 DOCX 保留历史状态；当前接续见[最新交接](docs/CURRENT_STATUS.md)。
