# NX500 SRW Reprocessing and 6.5K Oversampling Research

面向三星 **NX500 1.12** 的 LUT、SRW 重显影和全宽视频研究项目，使用 RAM 补丁及原厂图像处理接口。

## 当前版本：NX LUT Lite 0.4.0 RC5

[下载完整安装包与校验文件](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/releases/tag/v0.4.0-rc5) · [更新说明](docs/RELEASE_0.4.0_RC5_zh.md) · [接手交接](docs/HANDOFF_20261119_RC5.md) · [当前状态](docs/CURRENT_STATUS.md) · [合并短测](docs/RC5_TEST_STEPS_zh.txt)

- **普通 LUT 默认用于视频**：选择和参数只暂存，OK 才装载；每次非零普通 LUT 提交默认开启影片 LUT，仍可在菜单手动关闭。冷启动不自动套用。
- **EV+WiFi 开关 V‑Log**；**双击 EV 打开当前已应用的 LUT／Log LUT 详细页**；EV+AEL 菜单保留。切换 V‑Log 前停录并回照片待机。
- **半屏详细页**保留上半屏取景，提供浓淡、色相、锐度、对比及 M3 A/B 混合；装载后菜单保持打开，半按才返回取景。
- **繁体中文菜单、输入隔离及视频规格重载**；全宽 UHD24、UHD 转 2.5K、恢复原厂 UHD 通过原厂命令重新提交，不再要求手动重选原厂尺寸。
- **32 张正式 LUT**：25 普通、7 V‑Log；Log LUT 仅在 VID 专用入口选择，普通 A/B 和 SRW 不可选。
- **SRW** 保持已恢复的原生重显影链和五项精简菜单，初始化并重显影自动准备工作阶段。

RC4 已获用户反馈“效果很好”。RC5 已通过电脑验证并部署到 SD，**尚未收到 RC5 独立相机安装及短测回执**，因此以 RC 预发布提供。模拟 OS、MMIO、中断和 Gamma 端点的通过，不代表所有相机状态均已验收。

完整包为 `NX_LUT_Lite_0.4.0_RC5_full.zip`，85,921,118 字节：

```text
SHA256 74615b39bd1644ebc251b0d845de942acd0ee42435d5adc7e7dbb99761449be3
```

## 能力与研究边界

**全宽视频**优先采用原生 24p 的可用路线，用户已自行检查认可 R11g 的使用效果。真实稳定全宽 30p 尚未实现；bit6→bit2 区间的 RSZ2／WDMA2／HEVC／DDR 因果拆分仍未完成，也没有证明不可改变的硬件上限。“6.5K”描述全宽输入链研究，不表示输出 6.5K 视频或已独立证明全部感光点的参与方式。

**Log** 当前为 8bit V‑Log／V‑Gamut 兼容路线，不是 Main10 或厂商认证。已有相对曲线、曝光与颜色研究，物理 18% 灰和源原色／白点的独立标定仍有边界；MP4 原厂颜色标签未自动改写。视频原色监看与 Log 写卡尚未独立分流。

**SRW 与镜头矫正**复用已验证的原厂接口及查表路径。剩余颜色误差和插值不宣称与原厂逐像素等价；正式菜单隐藏诊断开关，不把新观测实验塞进日常重显影。

## 安装与使用

下载 Release 的完整 ZIP，核对 SHA256 后按包内说明覆盖所需安装文件；Git 源码目录不是可直接复制到卡上的安装包。先保留媒体、原固件、完整备份和历史日志。断 USB、关机待写卡灯熄灭，取出电池约30秒后安装；完成后关机再开机，确认 VID 页 RC5。

进行 SRW 前先还原 V‑Log、手动关闭影片 LUT，沿用既有初始化并重显影流程。不格式化、清理或修复 SD 文件系统，不重复码率对照。

## 目录与构建

| 路径 | 内容 |
| --- | --- |
| `work/r10k-ep-flow/` | 当前组件源码；目录名是历史名称，内容已更新到 RC5 路线 |
| `docs/` | 更新说明、当前状态、交接续篇和各轮研究记录 |
| `reports/validation/RC5/` | 本次软件验证与包身份记录；实机覆盖边界单独标注 |
| Release ZIP 的 `SOURCE/`、`EVIDENCE/` | 对应发布源码、历史支持和完整验证材料 |

主机采用 Python 3、Zig 0.13.0、ARM soft-float musl；ARM 回放依赖 Unicorn、Capstone 和 pyelftools。工具链、开发者自行取得的完整固件／rootfs 不随 Git 正文提交。菜单和按键可用 `base/nx-lut-lite/tools/build_zig.py --keys-menu-only` 定向重建；参数、外部输入和身份绑定见交接，不批量运行历史生成脚本。

发布脚本依赖既有工作区、原厂依赖和测试支持，不宣称裸仓库无外部输入即可完整复现。修改后必须重建相关组件、重新绑定清单并验证，不能沿用旧 ZIP 的哈希。

## 上游与许可

- [WApaperplane/metol](https://github.com/WApaperplane/metol)：LUT 格式、工具和 M3 经验；RC4 的完整单次即时加载参考固定提交 `b10ec8413ce5f9a03079288cb9880e4d0fac983f`。本项目菜单独立绘制，不将社区基础工作声称为原创。
- [ottokiksmaler/nx500_nx1_modding](https://github.com/ottokiksmaler/nx500_nx1_modding)：NX500／NX1 逆向资料、实际按键定义、SD 脚本和模组接入惯例。

项目派生代码遵循 [GNU AGPL v3](LICENSE)，来源、字体及第三方组件见 [NOTICE.md](NOTICE.md) 和 [STRENGTHS_NOTICE.md](STRENGTHS_NOTICE.md)。该许可不改变第三方 LUT、字体、运行库及原厂资源的归属。完整开发包包括必要运行资源、内置 SRW 初始化资产、历史校准测试图及对应证据；内容清单见 [发布文件说明](docs/RC5_PACKAGE_CONTENTS.md)。完整固件、整卡照片录像和用户备份不随 Git 正文提交。

旧 [2026-11-10 交接 TXT](docs/HANDOFF_20261110.txt)、[PDF](docs/HANDOFF_20261110.pdf) 和 DOCX 保留历史状态；最新接续以 [RC5 交接续篇](docs/HANDOFF_20261119_RC5.md) 为准。
