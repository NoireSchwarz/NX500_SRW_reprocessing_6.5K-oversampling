/* R10j: same eight-slot /256-byte native count lease; no new RTOS RAM.
 * Record hardware commit, SRAM-load IRQ, CS0-start IRQ, final IRQ, callback.
 * IRQ observations never select/enable/ACK an event. OEM trigger STR once.
 */
.syntax unified
.arm
.section .donor_a,"ax"
.global print_disabled,ipc_marker,hw_commit,irq_entry
print_disabled: bx lr
ipc_marker:
 push {r0-r7,ip,lr}
 mrs r7,cpsr
 mov r6,#0
 cmp r0,#4
 beq request_caller
 cmp r0,#5
 bne return_fast
 ldr r1,callback_caller
 cmp lr,r1
 bne return_fast
 mov r6,#1
 b lock_common
request_caller:
 ldr r1,start_caller
 cmp lr,r1
 bne return_fast
lock_common:
 cpsid if
 b common
hw_commit:
 push {r0-r7,ip,lr}
 mrs r7,cpsr
 cpsid if
 str r2,[r3,#0x304]
 dsb sy
 mov r6,#2
 b common
irq_entry:
 push {r0-r7,ip,lr}
 mrs r7,cpsr
 mov r6,#3
 movw r1,#0x418
 movt r1,#0x2081
 ldr r5,[r1]
 tst r5,#0x4c
 beq return_fast
 cpsid if
common:
 ldr r3,state
 ldr r1,[r3]
 ldr ip,magic
 cmp r1,ip
 bne unlock_fast
 cmp r6,#3
 movne r5,#0
 cmp r6,#0
 beq request
 ldr r2,[r3,#12]
 tst r2,#1
 beq unlock_fast
 cmp r6,#2
 beq hardware
 cmp r6,#3
 beq interrupt
 bl read_tick
 orr r2,r2,#8
 str r2,[r3,#12]
 bl publish_frame
 mov r0,#0
 str r0,[r3,#12]
 b unlock_clean
request:
 bl qualify_mode
 bne wrong_mode
 ldr r2,[r3,#12]
 tst r2,#1
 movne r4,#0
 orrne r2,r2,#16
 strne r2,[r3,#12]
 blne publish_frame
 mov r0,#0
 str r0,[r3,#16]
 str r0,[r3,#20]
 str r0,[r3,#24]
 str r0,[r3,#28]
 ldr r0,[sp,#16]
 and r0,r0,#3
 mov r1,#1
 orr r1,r1,r0,lsl #24
 str r1,[r3,#12]
 b unlock_clean
hardware:
 ldr r0,[sp]
 ubfx r1,r2,#24,#2
 cmp r0,r1
 bne unlock_fast
 bl read_tick
 tst r2,#2
 streq r4,[r3,#20]
 ubfx r1,r2,#16,#8
 cmp r1,#255
 addlo r2,r2,#0x10000
 orrhs r2,r2,#0x80000000
 orr r2,r2,#2
 str r2,[r3,#12]
 b unlock_clean
interrupt:
 bl read_tick
 bl extra_events
 tst r5,#4
 beq event_store
 tst r2,#4
 streq r4,[r3,#24]
 ubfx r1,r2,#8,#8
 cmp r1,#255
 addlo r2,r2,#0x100
 orrhs r2,r2,#0x40000000
 orr r2,r2,#4
 tst r5,#0x40
 orrne r2,r2,#128
event_store:
 str r2,[r3,#12]
 mov r5,#0
 b unlock_clean
wrong_mode:
 mov r0,#0
 str r0,[r3,#12]
unlock_clean:
 msr cpsr_c,r7
 bl clean_snapshot
 b return_fast
unlock_fast:
 msr cpsr_c,r7
return_fast:
 cmp r6,#3
 beq return_irq
 msr cpsr_f,r7
 pop {r0-r7,ip,pc}
return_irq:
 msr cpsr_f,r7
 pop {r0-r7,ip,lr}
 push {r4,r5,r6,r7,lr}
 ldr pc,irq_resume
.org 0x1ec
 bx lr
extra_events:
 tst r5,#8
 beq start_event
 tst r2,#0x30000000
 streq r4,[r3,#28]
 ubfx r1,r2,#28,#2
 cmp r1,#3
 addlo r2,r2,#0x10000000
start_event:
 tst r5,#0x40
 bxeq lr
 tst r2,#0x0c000000
 streq r4,[r3,#16]
 ubfx r1,r2,#26,#2
 cmp r1,#3
 addlo r2,r2,#0x04000000
 bx lr
clean_snapshot:
 cmp r5,#0
 mcrne p15,0,r5,c7,c10,1
 addne ip,r5,#24
 mcrne p15,0,ip,c7,c10,1
 mcr p15,0,r3,c7,c10,1
 dsb sy
 bx lr
.ltorg
tick_address: .word 0x06002060
start_caller: .word 0x8017d884
callback_caller: .word 0x801cdf78
state: .word 0x817dbbe0
magic: .word 0x33544c56
irq_resume: .word 0x80036aec
.org 0x260

.section .donor_b,"ax"
 bx lr
read_tick:
 ldr r1,tick_address
 ldr ip,[r1]
 ldr r4,[r1,#4]
 bx lr
.org 0x14
 bx lr
qualify_mode:
 movw r1,#0xe438
 movt r1,#0x8146
 ldrh r1,[r1]
 movw ip,#0x1717
 sub r1,r1,ip
 and ip,r1,#255
 cmp ip,r1,lsr #8
 bxne lr
 cmp ip,#2
 bxhi lr
 movw r1,#0xbf60
 movt r1,#0x813e
 ldr r1,[r1]
 bic r1,r1,#1
 movw ip,#0xe004
 cmp r1,ip
 bx lr
.org 0x5c
 bx lr
publish_frame:
 push {ip,lr}
 ldr r0,[r3,#8]
 add r0,r0,#1
 cmp r0,#0
 moveq r0,#1
 and r0,r0,#7
 rsb r0,r0,r0,lsl #3
 add r0,r3,r0,lsl #2
 add r0,r0,#32
 mov r1,#0
 str r1,[r0]
 ldr r1,[r3,#20]
 str r1,[r0,#4]
 ldr r1,[r3,#28]
 str r1,[r0,#8]
 ldr r1,[r3,#16]
 str r1,[r0,#12]
 ldr r1,[r3,#24]
 str r1,[r0,#16]
 str r4,[r0,#20]
 ldr r2,[r3,#12]
 str r2,[r0,#24]
 dmb sy
 ldr r1,[r3,#8]
 add r1,r1,#1
 cmp r1,#0
 moveq r1,#1
 str r1,[r0]
 str r1,[r3,#8]
 mov r5,r0
 pop {ip,pc}
.org 0xec
