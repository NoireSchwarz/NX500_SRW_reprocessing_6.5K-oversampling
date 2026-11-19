# 当前状态：2026-11-19，NX LUT Lite 0.4.0 RC5

当前包为 **RC5 预发布**。最近完成的用户认可来自 RC4（反馈“效果很好”）；RC5 已通过电脑验证并部署到 SD，尚未收到 RC5 独立相机安装和短测回执。

| 范围 | 当前状态 |
| --- | --- |
| 菜单／LUT | 繁体中文、半屏详细页、四项参数、M3 A/B；OK 才装载并保持菜单，半按回取景 |
| 快捷键 | EV+AEL 菜单；EV+WiFi 开关 V‑Log；双击 EV 打开当前已应用 LUT／Log LUT |
| 视频 LUT | 普通 LUT 非零提交默认开启，仍可手动关闭；冷启动不自动套用 |
| LUT 目录 | 25 普通＋7 V‑Log；Log LUT 仅 VID 专用入口，普通 A/B 和 SRW 不可选 |
| 即时重载 | RC4 完整 EOF／DMA 确认流程保留；SRW 自己的清理不打开新硬件加载器 |
| 视频规格 | 原厂命令重新提交，保留全宽 UHD24、UHD转2.5K、原厂UHD 三路线 |
| 全宽帧率 | 优先原生24p可用路线；真实稳定30p未实现，RSZ2／WDMA2／HEVC／DDR因果拆分仍未完成 |
| Log | 8bit V‑Log／V‑Gamut兼容路线；不宣称官方认证、Main10或独立物理灰卡标定 |
| SRW | 保持已恢复原生流程；正式菜单无测试Gamma及AUTO+COMPARE；先还原Log、关闭影片LUT再进入 |

MP4 原厂颜色标签没有自动重写；原色视频监看与 Log 写卡未实现独立分流。源原色、白点及部分颜色残差仍有证据边界。不要重启已控制的事务104、OLED、WB或重复码率主线。

完整包：`NX_LUT_Lite_0.4.0_RC5_full.zip`，85,921,118 字节，SHA256：

```text
74615b39bd1644ebc251b0d845de942acd0ee42435d5adc7e7dbb99761449be3
```

[完整包与附件](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/releases/tag/v0.4.0-rc5) · [更新说明](RELEASE_0.4.0_RC5_zh.md) · [交接续篇](HANDOFF_20261119_RC5.md) · [合并短测](RC5_TEST_STEPS_zh.txt)

旧 R10k 状态及当时的计时／供帧结论已保存在 [历史状态](history/CURRENT_STATUS_20261110_R10k.md)。旧结论不反向改写，后续接手先读本页。
