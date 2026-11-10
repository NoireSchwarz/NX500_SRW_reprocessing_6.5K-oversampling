# Demo51 Recovery R7 源码

releasecode：NX-LUT-Lite-0.4.0-demo51-recovery-r7-patch。

基于已冻结 R6，修改独立 OEM 恢复器的物理读取、受限故障处理和诊断；增加独立只读资源 probe、专用 session rearm lease 及菜单入口；安装器增加精确旧 Recovery 锁的控制面更新许可，锁文件始终保留。辅助脚本与渲染器更新身份和报告路径。没有新增 ISP/JPEG 提交或 CAP 释放入口，资源验证只允许回到 fresh-SRW 的初始化阶段。

core/tools/build_oem_restore.py 构建恢复器；core/tests 保留读宽、信号边界及原始恢复合同回归。tools/validate_recovery_r7_release.py 与 package_recovery_r7_patch.py 固定当前源码、受测 ARM、组件身份和发布归档。EVIDENCE 中 R6 数据属于历史基线，R7 验证单独标识。

工具链、原厂专有库及 assets 不随补丁分发。SOURCE 为本次实际代码；旧 R5/R6 发布脚本只是历史文件，不能据旧 helper hash 构建 R7。
