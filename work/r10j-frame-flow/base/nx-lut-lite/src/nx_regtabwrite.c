/* nx_regtabwrite.c — guarded NX500 1.12 LUT pointer operations.
 * Derived from metol; see NOTICE.md and bundled corresponding source.
 * View4, Still6 and BV12 retain their existing independent backups.
 * The opt-in DYN24 trial validates the stock selector instructions and all
 * 24 literal pointers before redirecting them to A18; it never copies over
 * the 24 factory LUT buffers or writes the hardware reload registers.
 * Writes are read back and checked against the surrounding 16 KiB window.
 * RAM-only changes must be restored explicitly before upgrading/downgrading;
 * an ordinary camera power cycle may resume a hibernation image.
 * Build: ARM EABI, -nostdlib -static -O0 -marm -mfloat-abi=soft.
 */
#include <asm/unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define TAB_ADDR    0x80380000u
#define TAB_SIZE    0x4000u
#define VIEW_AT     0x37F0u
#define VIEW0_OFF   0x37F0u   /* View4[0] -- AdobeRGB + screen colour on (A12) */
#define VIEW1_OFF   0x37F4u   /* View4[1] -- sRGB + screen colour on (A13) */
#define VIEW2_OFF   0x37F8u   /* View4[2] -- AdobeRGB + screen colour off (A14) */
#define VIEW3_OFF   0x37FCu   /* View4[3] -- sRGB + screen colour off (A17) */
#define STILL_AT    0x2C5Cu
#define STILL_N     6u
#define STILL_BAK   "/tmp/nx-lut-lite/session/still6_orig.bin"
/* Still (JPEG) has TWO mutually exclusive registry branches in 0x80382B30:
 *   +0x21 (the "screen colour / OLED" bit) == 1 -> BV12  (this table, 12 entries)
 *   otherwise                                   -> Still6 (6 entries, above)
 * Retargeting only Still6 leaves Still on the factory table whenever OLED is
 * on, which shows up as "viewfinder filtered, photo not filtered". */
#define BV_AT       0x2B00u
#define BV_N        12u
#define BV_BAK      "/tmp/nx-lut-lite/session/bv12_orig.bin"
#define REG_ADDR    0x2082B000u
#define REG_SIZE    0x1000u
#define VAL_A17     0x81115200u
#define VAL_A18     0x81119F00u
#define VAL_A19     0x8111EC00u
#define VAL_A12     0x810FD100u
#define VAL_A13     0x81101E00u
#define VAL_A14     0x81106B00u
#include "dynamic_table.h"

static unsigned char snap[TAB_SIZE];

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

static unsigned length(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }
static void say(const char *s) { write_all(1, s, length(s)); }
static int equal(const char *a, const char *b)
{ while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

static char *hex(char *p, unsigned int value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    *p++ = '0'; *p++ = 'x';
    for (shift = 28; shift >= 0; shift -= 4) *p++ = digits[(value >> shift) & 15];
    return p;
}

static void line_registry(const char *label, const volatile unsigned int *words, unsigned count)
{
    char out[512], *p = out;
    const char *s;
    unsigned i;
    for (s = label; *s; ++s) *p++ = *s;
    *p++ = ':';
    for (i = 0; i < count; ++i) { *p++ = ' '; p = hex(p, words[i]); }
    *p++ = '\n';
    write_all(1, out, (unsigned)(p - out));
}

/* DSB via coprocessor encoding: assembles under the arm5te default, takes
 * effect on the Cortex-A9 target. Needed so the store reaches the device
 * before the readback below. */
static void barrier(void)
{ __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory"); }

static int read_file(const char *path, unsigned char *dst, unsigned want)
{
    int fd = sc3(__NR_open, (int)path, O_RDONLY, 0);
    unsigned done = 0;
    if (fd < 0) { say("FAIL open backup for read\n"); return -1; }
    while (done < want) {
        int n = sc3(__NR_read, fd, (int)(dst + done), (int)(want - done));
        if (n == -4) continue;
        if (n <= 0) { sc3(__NR_close, fd, 0, 0); say("FAIL short backup\n"); return -1; }
        done += (unsigned)n;
    }
    {
        unsigned char extra;int n;
        do {n=sc3(__NR_read,fd,(int)&extra,1);} while(n==-4);
        if(n!=0) {sc3(__NR_close,fd,0,0);say("FAIL backup length\n");return -1;}
    }
    sc3(__NR_close, fd, 0, 0);
    return 0;
}

static int save_file(const char *path, const unsigned char *src, unsigned n)
{
    int fd = sc3(__NR_open, (int)path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int rc;
    if (fd < 0) { say("FAIL create backup\n"); return -1; }
    rc = write_all(fd, src, n);
    sc3(__NR_close, fd, 0, 0);
    return rc;
}

/* Only these slot addresses may ever appear in the View4 entries under our hand. */
static int known_slot(unsigned int v)
{ return v == VAL_A17 || v == VAL_A18 || v == VAL_A19 || v == VAL_A12 || v == VAL_A13; }

/* Count bytes changed outside the target region (must stay zero), and
 * report how many bytes inside it actually differ. A word rewrite can change
 * as few as one byte (0x81115200 -> 0x81119F00 differs only at 0x52). */
static unsigned window_delta(const volatile unsigned char *live,
                             unsigned off, unsigned len, unsigned *inside)
{
    unsigned i, stray = 0;
    *inside = 0;
    for (i = 0; i < TAB_SIZE; ++i) {
        if (live[i] == snap[i]) continue;
        if (i >= off && i < off + len) ++(*inside);
        else ++stray;
    }
    return stray;
}

static void putu(const char *label, unsigned v)
{
    char b[96], *p = b;   /* labels here run to ~42 chars; keep ample slack */
    unsigned d[10], k = 0, j;
    const char *s;
    for (s = label; *s; ++s) *p++ = *s;
    if (!v) d[k++] = 0;
    while (v) { d[k++] = v % 10u; v /= 10u; }
    for (j = k; j > 0; --j) *p++ = (char)('0' + d[j - 1]);
    *p++ = '\n';
    write_all(1, b, (unsigned)(p - b));
}

static int map(void **addr, unsigned pa, unsigned size, int writable, int *fd)
{
    int flags = writable ? (O_RDWR | O_SYNC) : (O_RDONLY | O_SYNC);
    int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
    unsigned got;
    *fd = sc3(__NR_open, (int)"/dev/mem", flags, 0);
    if (*fd < 0) { say("FAIL open /dev/mem\n"); return -1; }
    got = (unsigned)sc6(__NR_mmap2, 0, (int)size, prot, MAP_SHARED, *fd, (int)(pa >> 12));
    if (got >= 0xFFFFF000u) {
        say("FAIL mmap (writable=");
        say(writable ? "yes" : "no");
        say(")\n");
        sc3(__NR_close, *fd, 0, 0);
        return -1;
    }
    *addr = (void *)got;
    return 0;
}

/* Read the five EP 3DLUT registers once, for an objective Load-source check. */
static void show_registers(void)
{
    void *base = 0; int fd = 0;
    if (map(&base, REG_ADDR, REG_SIZE, 0, &fd) < 0) { say("REGS: unavailable\n"); return; }
    {
        volatile unsigned int *r = (volatile unsigned int *)base;
        char out[160], *p = out;
        *p++ = 'R'; *p++ = 'E'; *p++ = 'G'; *p++ = 'S'; *p++ = ':';
        *p++ = ' ';
        p = hex(p, r[0]); *p++ = ' ';
        p = hex(p, r[1]); *p++ = ' ';
        p = hex(p, r[2]); *p++ = ' ';
        p = hex(p, r[3]); *p++ = ' ';
        p = hex(p, r[4]); *p++ = '\n';
        write_all(1, out, (unsigned)(p - out));
        say("REGS: onoff cfg pulse load_addr save_addr\n");
    }
    sc3(__NR_munmap, (int)base, (int)REG_SIZE, 0);
    sc3(__NR_close, fd, 0, 0);
}

static int do_write(const char *mode, unsigned int slot)
{
    void *base = 0;
    int fd = 0, rc = 1;
    volatile unsigned int *tab;
    volatile unsigned char *live;
    unsigned int expect, target, before, after;
    unsigned off = VIEW3_OFF;
    unsigned i;

    if (equal(mode, "probe"))        { expect = 0xFFFFFFFFu; target = 0xFFFFFFFFu; }
    else if (equal(mode, "patch"))   { expect = 0xFFFFFFFFu; target = slot; }
    else if (equal(mode, "patch1"))  { expect = 0xFFFFFFFFu; target = slot; off = VIEW1_OFF; }
    else if (equal(mode, "patch12")) { expect = 0xFFFFFFFFu; target = VAL_A12; }
    else if (equal(mode, "restore")) { expect = 0xFFFFFFFFu; target = VAL_A17; }
    else { say("unknown mode\n"); return 1; }

    if (map(&base, TAB_ADDR, TAB_SIZE, 1, &fd) < 0) return 1;
    tab = (volatile unsigned int *)base;
    live = (volatile unsigned char *)base;

    for (i = 0; i < TAB_SIZE; ++i) snap[i] = live[i];
    before = tab[off >> 2];
    line_registry("VIEW4 before", tab + (VIEW_AT >> 2), 4);
    say("MODE "); say(mode); say(" writable-mapping ok\n");

    if (!known_slot(before)) {
        say("REFUSE: View4[3] holds an unknown slot, no write performed\n");
        goto out;
    }
    if (expect != 0xFFFFFFFFu && before != expect) {
        say("REFUSE: View4[3] is not the expected value, no write performed\n");
        goto out;
    }
    if (equal(mode, "probe")) target = before;
    if (!equal(mode, "probe") && before == target) {
        say("NOTE: already at target, nothing to write\n");
        rc = 0;
        goto out;
    }

    say("WRITE "); { char b[16], *p = b; p = hex(p, target); *p++ = '\n'; write_all(1, b, (unsigned)(p - b)); }
    tab[off >> 2] = target;
    barrier();

    after = tab[off >> 2];
    line_registry("VIEW4 after ", tab + (VIEW_AT >> 2), 4);
    say("READBACK "); { char b[16], *p = b; p = hex(p, after); *p++ = '\n'; write_all(1, b, (unsigned)(p - b)); }

    if (after != target) { say("FAIL readback does not match written value\n"); goto out; }

    /* Only the target word may differ; everything else must be untouched. */
    {
        unsigned in_word = 0;
        unsigned stray = window_delta(live, off, 4, &in_word);
        putu("WINDOW bytes changed inside target word = ", in_word);
        putu("WINDOW stray deltas outside target word = ", stray);
        if (stray != 0) { say("FAIL collateral change in window\n"); goto out; }
        if (equal(mode, "probe") ? in_word != 0 : in_word == 0) {
            say("FAIL unexpected delta pattern\n");
            goto out;
        }
    }
    rc = 0;

out:
    sc3(__NR_munmap, (int)base, (int)TAB_SIZE, 0);
    sc3(__NR_close, fd, 0, 0);
    return rc;
}

/* View4 is one four-entry table for all output profiles: P7 picks the entry
 * from the OrderSheet value (colour space + screen colour), so covering only
 * [1]/[3] leaves AdobeRGB looking at the factory table.
 *   [0] A12  AdobeRGB + screen colour on      [1] A13  sRGB + on
 *   [2] A14  AdobeRGB + screen colour off     [3] A17  sRGB + off
 * This writes or restores all four in one mapping, with the same "only the
 * target region may change" check as the single-word path. Restoring writes
 * the factory words unconditionally, so it is idempotent. */
/* View4 originals are saved in this boot, alongside the two still tables. */

static int do_view_all(const char *action, unsigned int slot)
{
    void *base = 0;
    int fd = 0, rc = 1;
    volatile unsigned int *tab;
    volatile unsigned char *live;
    unsigned int vals[4];
    unsigned i, inside = 0, stray;
    int readonly = equal(action, "status") || equal(action, "save");

    if (map(&base, TAB_ADDR, TAB_SIZE, readonly ? 0 : 1, &fd) < 0) return 1;
    tab = (volatile unsigned int *)base;
    live = (volatile unsigned char *)base;
    for (i = 0; i < TAB_SIZE; ++i) snap[i] = live[i];
    line_registry("VIEW4 before", tab + (VIEW0_OFF >> 2), 4);
    if (equal(action, "save")) { rc = save_file("/tmp/nx-lut-lite/session/view4_orig.bin", snap + VIEW0_OFF, 16); goto out; }
    if (readonly) { rc = 0; goto out; }
    if (!(equal(action, "set") || equal(action, "restore"))) goto out;
    if (equal(action, "restore") && read_file("/tmp/nx-lut-lite/session/view4_orig.bin", (unsigned char *)vals, 16) < 0) goto out;

    for (i = 0; i < 4; ++i) {
        if (equal(action, "set")) vals[i] = slot;
        tab[(VIEW0_OFF >> 2) + i] = vals[i];
    }
    barrier();
    line_registry("VIEW4 after ", tab + (VIEW0_OFF >> 2), 4);

    for (i = 0; i < 4; ++i) {
        if (tab[(VIEW0_OFF >> 2) + i] != vals[i]) { say("FAIL readback mismatch\n"); goto out; }
    }
    say("READBACK ok\n");

    stray = window_delta(live, VIEW0_OFF, 16, &inside);
    putu("VIEW4 bytes changed = ", inside);
    putu("stray deltas outside View4 = ", stray);
    if (stray != 0) { say("FAIL collateral change in window\n"); goto out; }
    rc = 0;

out:
    sc3(__NR_munmap, (int)base, (int)TAB_SIZE, 0);
    sc3(__NR_close, fd, 0, 0);
    return rc;
}

/* A registry table is a run of u32 slot addresses. TWO of them feed Still:
 * Still6 (6 entries) and BV12 (12 entries), selected in 0x80382B30 by the
 * +0x21 "screen colour" bit. Pointing every entry of a table at one slot
 * makes any branch of that table read the same content, so one capture
 * answers "does Still consume it" without guessing which index is live.
 * The original words are backed up before the first write. */
static void cat(char *dst, const char *a, const char *b)
{ while (*a) *dst++ = *a++; while (*b) *dst++ = *b++; *dst = 0; }

static int do_table(const char *action, unsigned off, unsigned n,
                    const char *bak, const char *name, unsigned int slot)
{
    void *base = 0;
    int fd = 0, rc = 1;
    volatile unsigned int *tab;
    volatile unsigned char *live;
    unsigned int vals[BV_N], before[BV_N];
    unsigned i, inside = 0, stray;
    int writing = equal(action, "set") || equal(action, "write");
    int backup = equal(action, "set");
    int readonly = equal(action, "status") || equal(action, "save");
    char lab[48];

    if (map(&base, TAB_ADDR, TAB_SIZE, readonly ? 0 : 1, &fd) < 0) return 1;
    tab = (volatile unsigned int *)base;
    live = (volatile unsigned char *)base;
    for (i = 0; i < TAB_SIZE; ++i) snap[i] = live[i];
    for (i = 0; i < n; ++i) before[i] = tab[(off >> 2) + i];

    cat(lab, name, " before"); line_registry(lab, tab + (off >> 2), n);
    if (equal(action, "save")) { rc = save_file(bak, (const unsigned char *)before, n * 4); goto out; }
    if (readonly) { rc = 0; goto out; }
    if (!(writing || equal(action, "restore"))) goto out;

    if (writing) {
        if (backup) {
            if (save_file(bak, (const unsigned char *)before, n * 4) < 0) goto out;
            say("BACKUP written\n");
        }
        for (i = 0; i < n; ++i) tab[(off >> 2) + i] = slot;
        for (i = 0; i < n; ++i) vals[i] = slot;
    } else {
        if (read_file(bak, (unsigned char *)vals, n * 4) < 0) goto out;
        for (i = 0; i < n; ++i) tab[(off >> 2) + i] = vals[i];
    }
    barrier();
    cat(lab, name, " after "); line_registry(lab, tab + (off >> 2), n);

    for (i = 0; i < n; ++i) {
        if (tab[(off >> 2) + i] != vals[i]) { say("FAIL readback mismatch\n"); goto out; }
    }
    say("READBACK ok\n");

    stray = window_delta(live, off, n * 4, &inside);
    cat(lab, name, " bytes changed = "); putu(lab, inside);
    putu("stray deltas outside table = ", stray);
    if (stray != 0) { say("FAIL collateral change in window\n"); goto out; }
    rc = 0;

out:
    sc3(__NR_munmap, (int)base, (int)TAB_SIZE, 0);
    sc3(__NR_close, fd, 0, 0);
    return rc;
}

static int do_still(const char *action, unsigned int slot)
{ return do_table(action, STILL_AT, STILL_N, STILL_BAK, "STILL6", slot); }

static int do_bv(const char *action, unsigned int slot)
{ return do_table(action, BV_AT, BV_N, BV_BAK, "BV12", slot); }

/* The dynamic selector's instructions stay intact. Only its 24 literal data
 * words may change. Layout and every original value were checked against the
 * stock NX500 1.12 image. Any different code or foreign pointer fails closed. */
static int dyn_guard(const volatile unsigned char *live, const volatile unsigned int *tab)
{
    unsigned hash=2166136261u;
    for(unsigned i=0;i<DYN_CODE_LEN;++i)hash=(hash^live[DYN_CODE_OFF+i])*16777619u;
    if(hash!=DYN_CODE_FNV) {say("REFUSE: DYN24 selector code signature mismatch\n");return 0;}
    for(unsigned i=0;i<DYN_N;++i) {
        unsigned v=tab[(DYN_AT>>2)+i];
        if(v!=dyn_factory[i] && v!=VAL_A18) {
            say("REFUSE: DYN24 contains an unrecognized pointer\n");return 0;
        }
    }
    return 1;
}
static int do_dynamic(const char *action,unsigned slot)
{
    int readonly=equal(action,"status") || equal(action,"check") || equal(action,"save");
    int writing=equal(action,"write"),restoring=equal(action,"restore");
    if(!(readonly || writing || restoring)) {say("REFUSE: unknown dyn action\n");return 1;}
    if(writing && slot!=VAL_A18) {say("REFUSE: video trial target must be A18\n");return 1;}
    void *base=0;int fd=0,rc=1;
    if(map(&base,TAB_ADDR,TAB_SIZE,!readonly,&fd)<0)return 1;
    volatile unsigned int *tab=(volatile unsigned int *)base;
    volatile unsigned char *live=(volatile unsigned char *)base;
    unsigned before[DYN_N],backup[DYN_N],wanted[DYN_N],inside=0,stray;
    for(unsigned i=0;i<TAB_SIZE;++i)snap[i]=live[i];
    for(unsigned i=0;i<DYN_N;++i)before[i]=tab[(DYN_AT>>2)+i];
    line_registry("DYN24 before",before,DYN_N);
    if(!dyn_guard(live,tab))goto out;
    if(equal(action,"status")) {say("DYN24_SIGNATURE_MATCH\n");rc=0;goto out;}
    if(equal(action,"check")) {
        for(unsigned i=0;i<DYN_N;++i)if(before[i]!=VAL_A18) {
            say("FAIL DYN24 is not fully redirected\n");goto out;
        }
        say("DYN24_ALL_A18\n");rc=0;goto out;
    }
    if(equal(action,"save")) {
        for(unsigned i=0;i<DYN_N;++i)if(before[i]!=dyn_factory[i]) {
            say("REFUSE: DYN24 is not factory; refusing to replace original backup\n");goto out;
        }
        rc=save_file(DYN_BAK,(const unsigned char *)before,sizeof before)<0;
        goto out;
    }
    if(read_file(DYN_BAK,(unsigned char *)backup,sizeof backup)<0)goto out;
    for(unsigned i=0;i<DYN_N;++i)if(backup[i]!=dyn_factory[i]) {
        say("REFUSE: DYN24 backup does not match stock pointers\n");goto out;
    }
    for(unsigned i=0;i<DYN_N;++i)wanted[i]=writing?VAL_A18:backup[i];
    /* Recheck after reading the backup and immediately before the first store. */
    if(!dyn_guard(live,tab))goto out;
    for(unsigned i=0;i<DYN_N;++i)if(tab[(DYN_AT>>2)+i]!=before[i]) {
        say("REFUSE: DYN24 changed during validation\n");goto out;
    }
    for(unsigned i=0;i<DYN_N;++i)tab[(DYN_AT>>2)+i]=wanted[i];
    barrier();
    for(unsigned i=0;i<DYN_N;++i)if(tab[(DYN_AT>>2)+i]!=wanted[i]) {
        say("FAIL DYN24 readback\n");goto undo;
    }
    stray=window_delta(live,DYN_AT,DYN_N*4,&inside);
    putu("DYN24 bytes changed = ",inside);putu("stray deltas outside DYN24 = ",stray);
    if(stray) {say("FAIL DYN24 collateral change\n");goto undo;}
    line_registry("DYN24 after",tab+(DYN_AT>>2),DYN_N);
    say("READBACK ok\n");rc=0;goto out;
undo:
    for(unsigned i=0;i<DYN_N;++i)tab[(DYN_AT>>2)+i]=before[i];
    barrier();say("DYN24 rollback attempted\n");
out:
    sc3(__NR_munmap,(int)base,TAB_SIZE,0);sc3(__NR_close,fd,0,0);return rc;
}

/* "still-set" / "bv-restore" ... -> action after the dash, plus which table. */
static int prefix(const char *s, const char *p)
{ while (*p) { if (*s++ != *p++) return 0; } return 1; }

static int table_mode(const char *cmd, unsigned int slot)
{
    const char *p = cmd;
    while (*p && *p != '-') ++p;
    if (*p != '-') { say("unknown mode\n"); return 1; }
    if (prefix(cmd, "still-")) return do_still(p + 1, slot);
    if (prefix(cmd, "bv-"))    return do_bv(p + 1, slot);
    say("unknown mode\n");
    return 1;
}

/* Raw hardware reload deliberately removed; use physical half-shutter. */
static unsigned int parse_hex(const char *s)
{
    unsigned v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++;
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A') + 10u;
        else break;
        v = (v << 4) | d;
    }
    return v;
}

#include "camera_guard.h"

int nx_main(unsigned *initial_stack)
{
    unsigned argc = initial_stack[0];
    char **argv = (char **)(initial_stack + 1);
    unsigned int slot = VAL_A18;   /* A18 unless the caller names A17/A19 */

    if (!camera_guard()) return 1;
    say("=== regtabwrite (registry entries, two Still tables) ===\n");
    if (argc < 2) {
        say("usage: nx_regtabwrite.arm status|probe|patch|patch1|patch12|restore\n");
        say("       patch  -> View4[3] (0x803837FC)\n");
        say("       patch1 -> View4[1] (0x803837F4), the entry P7 loads today\n");
        say("       still-status|still-set|still-write|still-restore <hexslot>\n");
        say("       bv-status|bv-set|bv-write|bv-restore <hexslot>\n");
        say("       view-set|view-restore|view-status [hexslot]  (all four View4)\n");
        say("       dyn-status|dyn-save|dyn-write|dyn-restore (guarded video trial)\n");
        say("       still-* = Still6 (6 entries), bv-* = BV12 (12 entries)\n");
        return 1;
    }
    if (argc >= 3) {
        slot = parse_hex(argv[2]);
        if (!known_slot(slot)) {
            say("REFUSE: target slot is not A17/A18/A19\n");
            return 1;
        }
    }
    if (equal(argv[1], "status")) {
        void *base = 0; int fd = 0;
        if (map(&base, TAB_ADDR, TAB_SIZE, 0, &fd) < 0) return 1;
        line_registry("VIEW4 ", (volatile unsigned int *)base + (VIEW_AT >> 2), 4);
        line_registry("STILL6", (volatile unsigned int *)base + (STILL_AT >> 2), STILL_N);
        line_registry("BV12  ", (volatile unsigned int *)base + (BV_AT >> 2), BV_N);
        sc3(__NR_munmap, (int)base, (int)TAB_SIZE, 0);
        sc3(__NR_close, fd, 0, 0);
        show_registers();
        say("=== done ===\n");
        return 0;
    }
    if (prefix(argv[1], "view-")) {
        int rc = do_view_all(argv[1] + 5, slot);
        say(rc == 0 ? "=== done ===\n" : "=== FAILED ===\n");
        return rc;
    }
    if(prefix(argv[1],"dyn-"))return do_dynamic(argv[1]+4,slot);
    if (prefix(argv[1], "still-") || prefix(argv[1], "bv-")) {
        int rc = table_mode(argv[1], slot);
        say(rc == 0 ? "=== done ===\n" : "=== FAILED ===\n");
        return rc;
    }
    {
        int rc = do_write(argv[1], slot);
        show_registers();
        say(rc == 0 ? "=== done ===\n" : "=== FAILED ===\n");
        return rc;
    }
}

__asm__(".global _start\n"
        "_start:\n"
        "mov r0, sp\n"
        "bl nx_main\n"
        "mov r7, #1\n"
        "svc 0\n");

