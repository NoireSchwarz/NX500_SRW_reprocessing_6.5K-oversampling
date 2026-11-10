/* SPDX-License-Identifier: AGPL-3.0-only */
/* Clang -O0 may emit this EABI helper for constant unsigned division. */
unsigned __aeabi_uidiv(unsigned n, unsigned d) {
    unsigned q = 0, bit = 1;
    if (!d) return 0;
    while (!(d & 0x80000000u) && d < n) { d <<= 1; bit <<= 1; }
    while (bit) { if (n >= d) { n -= d; q |= bit; } d >>= 1; bit >>= 1; }
    return q;
}
int __aeabi_idiv(int n, int d) {
    unsigned a = n < 0 ? 0u - (unsigned)n : (unsigned)n;
    unsigned b = d < 0 ? 0u - (unsigned)d : (unsigned)d;
    unsigned q = __aeabi_uidiv(a, b);
    return (int)((n < 0) != (d < 0) ? 0u - q : q);
}
#if defined(__arm__)
__asm__(".global __aeabi_idivmod\n"
        "__aeabi_idivmod:\n"
        "push {r4, r5, r6, lr}\n"
        "mov r4, r0\n"
        "mov r5, r1\n"
        "bl __aeabi_idiv\n"
        "mul r1, r0, r5\n"
        "sub r1, r4, r1\n"
        "pop {r4, r5, r6, pc}\n");
#endif
