# NX500 SRW Reprocessing and 6.5K Oversampling Research

面向三星 **NX500 1.12** 的 LUT、SRW 重显影和全宽视频研究项目，使用 RAM 补丁及原厂图像处理接口。

## 当前版本 · NX LUT Lite 0.4.0 RC6.14.10

[下载完整安装包与 SHA-256](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/releases/tag/v0.4.0-rc6.14.10) · [更新说明](docs/RELEASE_0.4.0_RC6.14.10_zh.md) · [交接文档](docs/HANDOFF_20261122_RC61410.md) · [合并短测](docs/RC61410_WIFI_TEST_STEPS_zh.md)

- **无线连接** · 短按 Wi-Fi 后选择「5 GHz 相簿」「2.4 GHz 相簿」或「2.4 GHz 圖傳」。双码连接。相簿可下载原档及查看相机资讯，2.4 GHz 原厂图传支持浏览器全屏。
- **快捷键** · EV+Wi-Fi 切换 V-Log，双击 EV 打开当前 LUT 详情，EV+AEL 打开补丁菜单。
- **LUT 与视频** · 繁体中文半屏菜单、四项参数及 M3 A/B 混合。OK 装载，半按返回取景。支持全宽 UHD24、UHD 转 2.5K 和恢复原厂 UHD。
- **SRW** · 一键初始化并重显影，输出相册用 JPEG 与缩略图并更新相册列表。

RC6.14.10 已通过电脑端验证，以预发布提供。RC6.14.9 的相机测试结果和本版软件验证边界见交接文档。

## 能力与研究边界

**全宽视频**优先采用原生 24p 的可用路线，用户已自行检查认可 R11g 的使用效果。真实稳定全宽 30p 尚未实现；bit6→bit2 区间的 RSZ2／WDMA2／HEVC／DDR 因果拆分仍未完成，也没有证明不可改变的硬件上限。“6.5K”描述全宽输入链研究，不表示输出 6.5K 视频或已独立证明全部感光点的参与方式。

**Log** 当前为 8bit V‑Log／V‑Gamut 兼容路线，不是 Main10 或厂商认证。已有相对曲线、曝光与颜色研究，物理 18% 灰和源原色／白点的独立标定仍有边界；MP4 原厂颜色标签未自动改写。视频原色监看与 Log 写卡尚未独立分流。

**SRW 与镜头矫正**复用已验证的原厂接口及查表路径。剩余颜色误差和插值不宣称与原厂逐像素等价；正式菜单隐藏诊断开关，不把新观测实验塞进日常重显影。

## 安装与使用

下载 Release 的完整 ZIP，核对 SHA256 后按包内说明覆盖所需安装文件；Git 源码目录不是可直接复制到卡上的安装包。先保留媒体、原固件、完整备份和历史日志。断 USB、关机待写卡灯熄灭，取出电池约30秒后安装；完成后关机再开机，确认 VID 页 RC6.14.10。

进行 SRW 前先还原 V‑Log、手动关闭影片 LUT，沿用既有初始化并重显影流程。不格式化、清理或修复 SD 文件系统，不重复码率对照。

## 目录与构建

| 路径 | 内容 |
| --- | --- |
| `work/r10k-ep-flow/` | 当前组件源码；目录名是历史名称，本版菜单、Wi-Fi 入口、安装器和研究源码 |
| `docs/` | 更新说明、当前状态、交接续篇和各轮研究记录 |
| `reports/releases/RC61410/` | 本次软件验证与包身份记录；实机覆盖边界单独标注 |
| Release ZIP 的 `SOURCE/`、`EVIDENCE/` | 本次打包的前端源码和电脑端验证 |

主机采用 Python 3、Zig 0.13.0、ARM soft-float musl；ARM 回放依赖 Unicorn、Capstone 和 pyelftools。工具链、开发者自行取得的完整固件／rootfs 不随 Git 正文提交。菜单和按键可用 `base/nx-lut-lite/tools/build_zig.py --keys-menu-only` 定向重建；参数、外部输入和身份绑定见交接，不批量运行历史生成脚本。

发布脚本依赖既有工作区、原厂依赖和测试支持，不宣称裸仓库无外部输入即可完整复现。修改后必须重建相关组件、重新绑定清单并验证，不能沿用旧 ZIP 的哈希。

## 上游与许可

- [WApaperplane/metol](https://github.com/WApaperplane/metol)：LUT 格式、工具和 M3 经验；RC4 的完整单次即时加载参考固定提交 `b10ec8413ce5f9a03079288cb9880e4d0fac983f`。本项目菜单独立绘制，不将社区基础工作声称为原创。
- [ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)：NX500／NX1 逆向资料、实际按键定义、SD 脚本和模组接入惯例。

项目派生代码遵循 [GNU AGPL v3](LICENSE)，来源、字体及第三方组件见 [NOTICE.md](NOTICE.md) 和 [STRENGTHS_NOTICE.md](STRENGTHS_NOTICE.md)。该许可不改变第三方 LUT、字体、运行库及原厂资源的归属。完整开发包包括必要运行资源、内置 SRW 初始化资产、历史校准测试图及对应证据；内容清单见 [发布文件说明](docs/RC5_PACKAGE_CONTENTS.md)。完整固件、整卡照片录像和用户备份不随 Git 正文提交。

旧 [2026-11-10 交接 TXT](docs/HANDOFF_20261110.txt)、[PDF](docs/HANDOFF_20261110.pdf) 和 DOCX 保留历史状态；最新接续以 [RC5 交接续篇](docs/HANDOFF_20261119_RC5.md) 为准。
