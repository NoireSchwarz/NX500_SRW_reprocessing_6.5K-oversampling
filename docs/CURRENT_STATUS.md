# 当前状态 · 2026-11-24 · NX LUT Lite 0.4.0

当前交付为 Log117（菜单内部版本 RC6.14.12）、Eye AF v2.8 和原厂快启恢复后的菜单监听。合并镜像使用机身本身的休眠生成流程。下一次验收应在同一次开机核对原厂 MENU、EV+AEL、取景眼框、`resume/runtime.log` 与 `resume/full-menu.txt`。

开发机的 26 mm 实拍验证了 117 工作灰点。旧 Metol 引擎来自 RC5，原目标 132。灰点结果与色域、物理反射率需分别验证。最后一次关闭 Log 的原厂 Gamma 请求已提交，后续 LUT 重载有一次未确认。请保留该错误回执供定位。

[版本说明、风险、恢复与短测](../release/0.4.0/README.md) · [灰点数据](../release/0.4.0/gray-calibration.json) · [上游人眼 PR](https://github.com/WApaperplane/NX500-metol/pull/2)

## 历史状态 · RC6.14.10

RC6.14.10 以 RC6.14.9 实机测试版为基线。电脑端验证已通过，实机验收待完成。

| 范围 | 当前状态 |
| --- | --- |
| Wi-Fi | 5 GHz 相簿、2.4 GHz 相簿、2.4 GHz 原厂图传。双码、全屏和 Wi-Fi 停止 |
| 菜单／LUT | 繁体中文、半屏详细页、四项参数、M3 A/B；OK 才装载并保持菜单，半按回取景 |
| 快捷键 | EV+AEL 菜单；EV+WiFi 开关 V‑Log；双击 EV 打开当前已应用 LUT／Log LUT |
| 视频 LUT | 普通 LUT 非零提交默认开启，仍可手动关闭；冷启动不自动套用 |
| LUT 目录 | Log LUT 仅 VID 专用入口，普通 A/B 和 SRW 不可选 |
| 即时重载 | RC4 完整 EOF／DMA 确认流程保留；SRW 自己的清理不打开新硬件加载器 |
| 视频规格 | 原厂命令重新提交，保留全宽 UHD24、UHD转2.5K、原厂UHD 三路线 |
| 全宽帧率 | 优先原生24p可用路线；真实稳定30p未实现，RSZ2／WDMA2／HEVC／DDR因果拆分仍未完成 |
| Log | 8bit V‑Log／V‑Gamut兼容路线；不宣称官方认证、Main10或独立物理灰卡标定 |
| SRW | 保持已恢复原生流程；正式菜单无测试Gamma及AUTO+COMPARE；先还原Log、关闭影片LUT再进入 |

MP4 原厂颜色标签没有自动重写；原色视频监看与 Log 写卡未实现独立分流。源原色、白点及部分颜色残差仍有证据边界。不要重启已控制的事务104、OLED、WB或重复码率主线。

[完整安装包](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/releases/tag/v0.4.0-rc6.14.10) · [更新说明](RELEASE_0.4.0_RC6.14.10_zh.md) · [交接](HANDOFF_20261122_RC61410.md) · [短测](RC61410_WIFI_TEST_STEPS_zh.md)

[历史状态](history/CURRENT_STATUS_20261110_R10k.md) · [RC5 交接](HANDOFF_20261119_RC5.md)
