/* Preserve the original by-value ARM EABI frame arguments. */
.syntax unified
.arm
.section .text.hg_entry,"ax",%progbits
.global hg_entry
.type hg_entry,%function
hg_entry:
 push {r0-r12,lr}
 mov r0,sp
 add r1,sp,#56
 adr r2,hg_entry
 sub r2,r2,#0x60000
 bl hg_decide
 cmp r0,#0
 pop {r0-r12,lr}
 beq 1f
 mov r0,#0
 bx lr
1: b hg_original
.size hg_entry,.-hg_entry
.section .note.GNU-stack,"",%progbits
