.syntax unified
.arm
.text
.global _start
.type _start,%function
_start:
 mov fp,#0
 mov lr,#0
 pop {r1}
 mov r2,sp
 str r2,[sp,#-4]!
 str r0,[sp,#-4]!
 ldr ip,=nxl_fini
 str ip,[sp,#-4]!
 ldr r0,=main
 ldr r3,=nxl_init
 bl __libc_start_main
 b .
.type nxl_init,%function
nxl_init: bx lr
.type nxl_fini,%function
nxl_fini: bx lr
.section .note.GNU-stack,"",%progbits
