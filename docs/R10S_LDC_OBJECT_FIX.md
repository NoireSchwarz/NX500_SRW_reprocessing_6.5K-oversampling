# R10s：修正活动 LDC 策略对象读址

R10r实测：内部安装/采样保存正常；稳定录像87/87条各一次event10，无owner/回调变化。类型快照全部拒绝。录像周期43.562ms、HW到最终IRQ43.067ms，普通帧周期42.589ms，性能问题未解决。

原厂0x80115ab0分配84字节标量参数区到0x8142efc8+0x220=0x8142f1e8；0x80115a18按字节/标量初始化。该字段不是多态策略。既有R10R_LDC_OBSERVER.md及模拟对象图将其误认为操作对象的描述，以本版校正为准。历史文件保留，不改写实测证据。

原厂0x80114500将父对象0x8142e018的+0xFA4（View）复制到+0xFA0（当前策略），故只读入口为0x8142efb8。View工厂0x80116134返回0x8142f434；原厂构造器写主/次类型0x80582d10/0x80582d70。0x80114ec8用策略this+12注册event10，原厂EnableInterrupt将owner写入0x8153cb28。相机实测owner0x8142f440与View+12吻合，但R10r未采到活动字段/owner虚表，不能追认其类型关联有效。

R10s仅修改Linux只读快照根和版本：route3仍为800字节/6条，记录ABI及采样文件大小不变，双读策略指针、类型、owner、回调、timer与mode/source，不执行对象方法，不写目标内存或MMIO。仍检查精确主/次类型对和owner=策略+12。CBackend_Ldc_CS仍不关联。RTOS producer9和0x804bb640/event10入口、ACK/enable/mask、100ms读取、三文件保存和HEVC停用保持；全宽helper与RTOS二进制要求与R10r逐字节相同。

分析器兼容旧route2以保留原始记录，新增raw_object_fields_match与operation_pointer_source，但route2不再产生有效对象关联或LDC分段统计；只有route3可用于同一稳定mode/source区间的关联。稀疏对象关联仍不等于逐缓冲身份或完整生命周期证明，不把近提交的6微秒事件当作LDC实际处理时长。

验证包括：原厂ARM参数分配/构造/策略选择/注册路径生成真实布局后交给生产ARM读取器；参数区伪对象、对象变更/空指针/坏类型/越界及旧格式拒绝；真实ARM采集器和shell启停/导出；R10r实测负例与历史文件回归；包身份绑定、解包与SD读回。OS、缓存、内核和MMIO仍有模型边界，实机性能修复未宣称。
