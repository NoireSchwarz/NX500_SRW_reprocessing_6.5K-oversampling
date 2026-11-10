/* SPDX-License-Identifier: AGPL-3.0-only */
/* ARM EABI/glibc legacy startup, no dependence on a modern host crt1.o. */
.syntax unified
.arm
.text
.global _start
.type _start,%function
_start:
    mov fp, #0
    mov lr, #0
    mov r4, r0
    ldr r1, [sp], #4
    mov r2, sp
    push {r2}
    push {r4}
    mov r0, #0
    push {r0}
    mov r3, #0
    ldr r0, =main
    bl __libc_start_main
    bl abort
.section .note.GNU-stack,"",%progbits
