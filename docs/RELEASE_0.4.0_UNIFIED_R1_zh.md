## 0.4.0 Unified R1 · 合并测试成功

**开发机实机合并测试成功。** 维护者已确认：在同一次普通冷启动中，人眼框与 **EV+AEL 主菜单**均正常。此项为维护者实机反馈；电脑端另通过 30 组用例和 8 组原厂 ARM 服务管理器回放。

本次替换原有完整安装包和 SHA-256 文件，保留 Log117 完整 mod、Eye AF v2.8、原首次安装内容与源码，并加入 `unified-rebuild/` 同代重建入口。旧发布包更名为 `before_Unified_R1`，仅供回退。

- 人眼启用和主菜单按键监听由同一个机内入口顺序启动，不再依赖 SD 挂载回调。旧主监听的自动启动链接会归档。
- 重建先完整备份并逐字节比较本机 150 MiB 旧快照，保留旧状态与配置；制作启动及原厂首次覆盖镜像头前分别核验实际服务依赖。
- SD 准备流程把旧测试包、日志和临时入口整项归档，保留 DCIM、原厂目录、looks 和未知个人文件。归档操作不格式化或修复文件系统。
- 人眼算法、主模块的菜单、LUT／Log 引擎以及原首次安装载荷沿用已验证字节。其他机身仍应独立完成普通冷启动验收。

### 安装入口

下载新的 `NX500_Full_Mod_EyeAF_0.4.0.zip` 及同名 `.sha256`。已安装本代 0.4.0 合并快照的机身，在完整 ZIP 解压目录执行；将盘符和序列号换成自己的 exFAT 卡：

```powershell
python .\stage-postflash.py REBUILD-PLAN --drive X: --serial ABCD-1234
python .\stage-postflash.py REBUILD --drive X: --serial ABCD-1234
```

首次安装仍先使用完整包的 `module/` 和原分步快启安装流程；已有本代状态再使用统一重建入口。R1 重建更新目录本身不包含独立完整主模块。

满电、M 档、English，自动熄屏 5 分钟、省电 10 分钟，关闭无线，关机卸镜头并盖机身盖。看到 **FAST START: WAIT 5 MINUTES / KEEP POWER** 后至少等 5 分钟且卡灯熄灭，再按说明继续。持续黑屏或闪灯时保持供电。每台机身通过原厂流程制作自己的镜像，使用本机备份回退。

[完整安装及恢复说明](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/blob/main/release/0.4.0/README.md) · [统一重建流程](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/blob/main/release/0.4.0/rebuild/README_zh.md) · [灰点数据](https://github.com/NoireSchwarz/NX500_SRW_reprocessing_6.5K-oversampling/blob/main/release/0.4.0/gray-calibration.json)
