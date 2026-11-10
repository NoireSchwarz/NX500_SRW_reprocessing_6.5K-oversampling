P49 CC 提交集成接口（无硬件执行）

当前默认是固定原厂CC fallback：case.cc=0。主流程完成正常nativeconfig后调用`p49_pd_record_factory()`，然后仍按原CC审计流程`cc_prepare_fixed(f+P48_F_CC1)`与`cc_apply()`提交/精确恢复固定template；不调用下面experimental arm/commit，不注册event67、不新增reload。

factory审计区域同为CAP+bc00/256B：phase=SKIPPED_FACTORY_CC(7)，profile_mode word48=2，nonce绑定；其余word4..63除48全部为0，含sourceCCenabled49、reloadcalled50、IRQ/等待/loader计数。host必须同时确认handoff `cc_source_reloaded=0`并通过`p49_pd_factory_audit_valid()`。factory记录与experimental成功记录互斥，不能混装。

原stop ACK没有被证明会排空独立参数DMA通道。event67不携带bank/代次；更早旧DMA可能使两代计数提前满足。`P49_PD_INITIAL_DRAIN_PROVEN=0`明确阻止experimental入口，返回INITIAL_DRAIN_UNPROVEN(14)，不产生新的注册或装载。即使构造出结构完整的两个callback记录，`p49_pd_audit_valid()`在此门控未闭合时仍不接受。`p49_pd_completion_record_valid()`只验证记录结构，不能冒充可激活结论。

以下保留experimental源码的未来接线说明，当前不可激活：

主流程按以下顺序调用，接口不依赖帧数：

1. 原stop合同已验证、输入/输出buffer cache完成后，`cc_dma_arm_config()`。
2. 原`native(0x801d9f84,...)`成功。
3. `cc_dma_wait_config()`；未观察到原厂event67新完成就失败并保留busy。
4. `cc_prepare_fixed(f+P48_F_CC1)`。
5. `cc_commit()`：内部调用原index1 setter、核Normal-NC映射，调用原`8021b314(owner)`，等待第二代event67完成，核source/size/bank和DDR目标，再注销临时handler。
6. WB最终提交及frame start。

`cc_commit()`前不要额外调用`cc_apply()`。整bank重装可能覆盖WB硬件，因此WB最终提交必须放在commit之后。

失败清理：当`cc()[71]==0`且无其它frame busy时，先尝试`cc_dma_cleanup()`，再进行CC恢复和native时钟清理。若参数DMA等待超时或收到不合合同IRQ，inflight/busy保持1；不能恢复DDR、关钟或释放ownedCAP。已有“在途失败保留CAP至重启”的策略应继续生效。正常commit内部已经cleanup。

审计区域：CAP+`0xbc00`，256B，建议导出`p49_param_dma_audit.bin`。`p49_pd_audit_valid(record,nonce)`是成功验收；当前error必须为0、phase为CLOSED、两个generation分别完成一次、总IRQ恰好2次、无foreign、slot恢复0，且新source/size/bank为bb71e000/1700/1。

完成合同：原厂同步调用者`801dd028`明确注册event67、装载后等待“Sram Update Done”。event67映射pending`20810418 bit3`，原ISR ACK`20810440=8→0`后调用slot`813f09c0`。专用callback保留直接原ISR边界：LR=`80036c28`、entrySP[0]=3、entrySP[1]&8。它不借用共享`81540f40`事件对象。

监听必须在config之前注册。event67不含bank/代次；如果config之后才注册，其迟到完成可能被误认作重装完成。代码先接受config generation1，再允许修改CC/发起generation2。注册前要求原stop证明、idle7、slot0、enablebit3关闭、pendingbit3清、commandbit4清；后续配置状态2、owner bank1和roots固定。调用期间应沿用现有相机应用暂停/单任务控制，不能并发启动其它原厂parameter DMA。

DDR可见性：原mapper回放证明BB7为`bb701402`，Normal-NC。runtime双读PTE`81002edc`稳定并用原TRE/PRRR/NMRR合同分类，失败则VISIBILITY_UNPROVEN。未扩展未知mode3 cache ABI。DSB只排序，完成仍由IRQ独立判定。

布局：`.text.param`放`0x3100..0x3fff`，ctx为`0x3000..0x3043`，result始于`0x4000`。强制全部API可达的ARM fixture当前`.text.param`约2.9KiB，`.text.cc`约7.1KiB；helper和callback仍在.cc。此测试fixture不进入相机二进制。

本分支新增的原厂API/ISR/mapper须加入正式固件字节guards；原完整RTOS SHA固定为f8e417cb5b55768851ba7e2b8017d3a98dc0b0c8444cc0a5061265cc08dbbf7f。

验证：`test_param_dma_contract.c`通过，2437份stale/failed/mixed/corrupt审计被拒绝，含cacheable/错物理页/访问属性错误、错IRQ bit/LR/对齐、factory伪填任意完成/调用字段等。原字节IRQ/同步wrapper回放在`purple-evidence-review/cc-active/completion/`，mapper回放在`purple-evidence-review/cc-active/cache/`。初始drain阻塞证据是completion/stop-drain-boundary.json。虚拟MMIO不等于相机采用或像素正确。
