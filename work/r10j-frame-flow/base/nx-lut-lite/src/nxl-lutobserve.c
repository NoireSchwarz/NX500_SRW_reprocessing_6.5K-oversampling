/* SPDX-License-Identifier: AGPL-3.0-only
 * Derived from metol test_server/lutpipe/lutobserve.c.
 * Bounded read-only capture of the five known EP 3DLUT registers.
 * No ioctl, no private d5 device, no writable memory mapping, no register store.
 * Usage: nxl-lutobserve.arm [snapshot|watch]
 * watch: 200 samples, 100ms apart, 30-second process timer.
 * Samples are buffered in RAM; the CSV is written after sampling finishes.
 * No image buffers, AF registers, LUT slots or registry tables are mapped.
 * Build: ARM EABI, -nostdlib -static -fno-builtin -fno-stack-protector
 *        -fno-pic -no-pie -fomit-frame-pointer -marm -mfloat-abi=soft.
 */
#include <asm/unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

static int sc3(int n, int a, int b, int c)
{
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory", "cc");
    return r0;
}

static int sc6(int n, int a, int b, int c, int d, int e, int f)
{
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    register int r3 __asm__("r3") = d;
    register int r4 __asm__("r4") = e;
    register int r5 __asm__("r5") = f;
    register int r7 __asm__("r7") = n;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4),
                     "r"(r5), "r"(r7) : "memory", "cc");
    return r0;
}

static int write_all(int fd, const void *buffer, unsigned size)
{
    const char *p = (const char *)buffer;
    while (size) {
        int n = sc3(__NR_write, fd, (int)p, (int)size);
        if (n == -4) continue;
        if (n <= 0) return -1;
        p += n;
        size -= (unsigned)n;
    }
    return 0;
}

static unsigned length(const char *s)
{
    unsigned n = 0;
    while (s[n]) ++n;
    return n;
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int fail(const char *message)
{
    write_all(2, message, length(message));
    return 1;
}

static char *hex(char *p, unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    *p++ = '0'; *p++ = 'x';
    for (shift = 28; shift >= 0; shift -= 4) *p++ = digits[(value >> shift) & 15];
    return p;
}

struct nx_timespec { int seconds; int nanoseconds; };

static int monotonic(unsigned *ms)
{
    struct nx_timespec t;
    if (sc3(__NR_clock_gettime, 1, (int)&t, 0) < 0) return -1;
    *ms = (unsigned)t.seconds * 1000u + (unsigned)t.nanoseconds / 1000000u;
    return 0;
}

static void say(const char *s) { write_all(2, s, length(s)); }
#include "camera_guard.h"
static unsigned captured[200][6];

int nx_main(unsigned *initial_stack)
{
    unsigned argc = initial_stack[0];
    char **argv = (char **)(initial_stack + 1);
    unsigned address, map_size, pointer, sample, count, began, now;
    int fd, rc = 0;
    volatile unsigned *registers;

    if (!camera_guard()) return 1;
    count = 1;
    if (argc == 2 && equal(argv[1], "watch")) count = 200;
    else if (argc > 2 || (argc == 2 && !equal(argv[1], "snapshot")))
        return fail("usage: nxl-lutobserve.arm [snapshot|watch]\n");
    {
        const int timer[4] = {0, 0, 30, 0};
        if (sc3(__NR_setitimer, 0, (int)timer, 0) < 0)
            return fail("process timer setup failed\n");
    }
    address = 0x2082B000u;
    map_size = 4096u;
    fd = sc3(__NR_open, (int)"/dev/mem", O_RDONLY | O_SYNC, 0);
    if (fd < 0) return fail("open /dev/mem O_RDONLY|O_SYNC failed\n");
    pointer = (unsigned)sc6(__NR_mmap2, 0, (int)map_size, PROT_READ,
                            MAP_SHARED, fd, (int)(address >> 12));
    if (pointer >= 0xFFFFF000u) {
        sc3(__NR_close, fd, 0, 0);
        return fail("mmap PROT_READ failed\n");
    }
    {
        const char *header = "monotonic_ms,onoff,cfg,pulse,load_addr,save_addr\n";
        unsigned done = 0;
        registers = (volatile unsigned *)pointer;
        if (monotonic(&began) < 0) rc = -1;
        for (sample = 0; sample < count && rc == 0; ++sample) {
            unsigned i;
            struct nx_timespec delay = {0, 100000000};
            if (monotonic(&now) < 0 || now - began > 25000u) { rc = -1; break; }
            captured[sample][0] = now;
            for (i = 0; i < 5; ++i) captured[sample][i+1] = registers[i];
            ++done;
            if (sample + 1 < count && sc3(__NR_nanosleep, (int)&delay, 0, 0) < 0) rc = -1;
        }
        /* Keep SD writes outside the observation interval. */
        if (write_all(1, header, length(header)) < 0) rc = -1;
        for (sample = 0; sample < done; ++sample) {
            char line[80], *out = line;
            unsigned i;
            for (i = 0; i < 6; ++i) {
                if (i) *out++ = ',';
                out = hex(out, captured[sample][i]);
            }
            *out++ = '\n';
            if (write_all(1, line, (unsigned)(out - line)) < 0) { rc = -1; break; }
        }
    }
    sc3(__NR_munmap, (int)pointer, (int)map_size, 0);
    sc3(__NR_close, fd, 0, 0);
    return rc == 0 ? 0 : fail("capture incomplete\n");
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #1\n"
        "svc 0\n");

