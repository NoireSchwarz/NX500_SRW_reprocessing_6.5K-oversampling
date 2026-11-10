# 来源与改动

本项目为 metol 的精简派生作品，不声称 LUT 研究、格式或写入流程为原创。

来源：WApaperplane/metol，提交 cb82f4886c1174261cbba8127000c7b696944ff4（2026-10-25）。

| 本包 | 上游 | 改动 |
| --- | --- | --- |
| src/cube2lut.c | test_server/lutpipe/cube2lut.c | 保留转换实现，增加独立编译所需的除法辅助代码 |
| src/nx_slotwrite.c | test_server/lutpipe/nx_slotwrite.c | 增加 NX500 1.12 固件门控，修复地址范围整数溢出检查 |
| src/nx_regtabwrite.c | test_server/lutpipe/nx_regtabwrite.c | 删除裸硬件 reload；增加只读快照；按本次启动保存的 View4 原值还原；备份迁入本机临时目录；增加固件门控与未知动作拒绝 |
| lut.sh | scripts/lut.sh | 改独立路径，去外置 BusyBox 依赖；串行事务、写前备份、失败回退、输入校验、OLED 跨启动恢复 |
| looks/*.bin | looks/ 同名文件 | 未修改；三张示例原生表 |
| tests/arm_vm.py | test_server/lutpipe/arm_converter_emulator.py | 延伸 ELF/系统调用模拟思路，增加相机内存与持久文件模型 |
| src/menu.c | 新实现 | Xlib 菜单；焦点保存/归还及物理半按思路参考 metol NXUI，未复制 Nuklear 与字体包 |
| src/keys.c、keys.h | 新实现 | 按键码与双 evdev 设备惯例来自 ottokiksmaler/nx500_nx1_modding 的 keyscan.c 和 Working-with-key-events.md |
| 安装与接入脚本 | 新实现 | SD 触发与 systemd 服务参考社区现有模组部署方式；未复制旧蓝牙替换链 |

metol 的 LICENSE 为 GNU Affero GPL v3，本派生版本按 AGPL-3.0 分发并提供对应源码。新增文件的 SPDX 标记不改变上游的权利和归属。vendor/X11 为构建用 X.Org 头文件，保留其各自 MIT/X11 许可和版权文本；不安装到相机。

未包含三星固件、rootfs、libc、X11 共享库，亦未包含 NX-KS、keyscan、mod_gui 的旧预编译程序。

0.1.3 的蓝牙包装入口参考社区 Running_scripts_without_factory_mode_BT.md 和 metol scripts/init.sh 的启动惯例；只加入本模组启动与日志，随后执行原厂程序并保留参数。未包含任何三星 bluetoothd 文件，原始程序只在用户相机本地改名保留供还原。

0.1.2 的大字菜单使用本包 src/font.h 的简单位图字形，不再依赖机身核心字体。安装收尾的 dfmsd 退出与绿点说明参考社区 Running-shell-scripts-from-SD-card.md；未引入完整旧 mod。三个 LUT 工具在此版本未改动。

0.1.4 将默认调用路径改为仅成片 LUT，参考 metol LUT_RETARGET_LIVE_2026-10-18.md 对独立 View/Still 登记表的实机记录。此派生版本的对焦改善尚未通过实机验收。

0.1.5 的 src/nxl-lutobserve.c 派生自 metol test_server/lutpipe/lutobserve.c（同一固定提交，blob 7d73d058e988ce535cce350b58d447a6e7eccbf5）：移除 pools 模式，加入固件门控，将 watch 改为 200×100ms、30 秒定时器及 RAM 缓冲输出。只读记录不代表 AF 修复已完成。

0.1.6 按用户要求恢复 View4 预览，增加实际写入源副本与只读比对，不再仅依赖 ACTIVE 标签。LUT 工具与只读记录器未改；恢复预览后的 AF 表现未通过本派生版本实机验收。

0.1.7 菜单布局参考用户提供的三星 NX 菜单照片，绘制和交互独立实现，不调用或复制三星原生菜单资源。data/looks-info.json 的短简介为本包撰写的风格提示，不是胶片厂商的官方标定说明。

data/ui-glyphs.json 与生成的 src/ui_font.h 当前为 335 字形的 NXL UI Sans 位图子集，源自 Noto Sans CJK SC Medium 2.004（© 2014–2021 Adobe），依据 SIL Open Font License 1.1 分发；完整许可见 vendor/NotoSans-OFL.txt。原 OTF 的来源、SHA256 和栅格化参数见 UI_FONT.json；可用 tools/make_sans_glyphs.py 重建。原始 OTF、三星字体和系统库不分发。此前 ISAS/DejaVu 的许可文件保留供历史来源与旧工具说明。字形部分继续遵循 OFL，新增非字体代码按 AGPL-3.0 分发。

0.1.8 对菜单的 Trace 生命周期作独立修改：采集在后台任务中等待，释放前台菜单锁，专用锁防重复；LUT 写入器和记录器未改。VIDEO_TRACE_NOTES_zh.md 只包含用户本次记录的必要地址/时序分析，不附带完整原始日志。

0.1.9-video-trial 增加动态 24 路指针重定向。位置与原值来自三星官网公开的 NX500 1.12 原厂固件的离线核对，并通过原 ARM 选择函数逐分支仿真。DYNAMIC_MAPPING.json 记录源散列、代码指纹和 24 个原值；tools/verify_dynamic_mapping.py 可对开发者自行取得的同版 P7 镜像独立复核。包中不附带固件、反汇编全文或相机运行库；仅新增必要的事实性地址及校验常量。该修改尚未通过实际录像文件验收。

0.2.0 的 EV+Wi-Fi 编码依据社区 Working-with-key-events.md：MOBILE 的 X11 码为 233，evdev 码减去偏移 8 即 225。组合键释放、混按抑制、视频 CLI 与提示条保留图像为本包独立实现。SamsungSans/Breeze Sans 与当前 Noto 位图不是同一个字体，本包不作三星原生字体声明。机身原厂字体文件线索 /usr/share/fonts/SDIC_GP_US_20120720.ttf 来自 metol 的 NXUI 回退代码，未在此版调用或从机身复制。

0.2.1 更正上一段对 Wi-Fi 键码的错误推导：社区真实 keyscan.c（Git blob 8ff348b9b04fa0939466c7d0a7b5afd57dcbb735）定义 NXKEY_MOBILE 215，且 nxkeyname[215] = MOBILE。0.2.0 曾将旧 Working-with-key-events.md 的 X11 MOBILE=233 减 8 得到 225，未与实际 evdev 源码交叉核对。本版不沿用该错误推导；事实来源见 KEYMAP_SOURCE.json。

菜单 0.2.2 的新增 7 张简介依据本次对话已经制作的人像/5219 包说明撰写，仅描述该包的创作配色和使用方向。apx 文件为近似配方，不宣称是 EJAY 5207/Gold200 原版或柯达官方标定。ejay_skin_native 是先前已转换的 EJAY 肤色表；本菜单补丁仅增加文字映射，不包含或修改这 7 张 LUT 的数据。

0.3.0 新增 src/browser.h 的收藏/分类/光标持久化与强度选择，src/nxl-tablecheck.c 为独立只读文件比较/完整性工具，不访问相机内存设备。tools/make_strengths.py 的混合算法作用于本次对话已有的原生 LUT，使用 tools/native_codec.py 的 BT.601-full 转换；后者从此前人像包保留的 metol 固定提交原样复制，来源 blob 与散列见该文件说明和原 UPSTREAM。强度数据的原作者、近似配方与 EJAY 区别见 STRENGTHS_NOTICE.md。
