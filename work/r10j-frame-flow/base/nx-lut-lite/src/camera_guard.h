/* SPDX-License-Identifier: AGPL-3.0-only
 * Included after syscall/string helpers in the freestanding memory tools.
 */
static int camera_guard(void) {
    char b[128];
    int fd = sc3(__NR_open, (int)"/etc/version.info", O_RDONLY, 0);
    if (fd < 0) { say("REFUSE: cannot read camera version\n"); return 0; }
    int n = sc3(__NR_read, fd, (int)b, sizeof b - 1);
    sc3(__NR_close, fd, 0, 0);
    if (n <= 0) return 0;
    b[n] = 0;
    char *p = b, *v = p, *model;
    while (*p && *p != '\r' && *p != '\n') ++p;
    if (!*p) return 0;
    *p++ = 0;
    while (*p == '\r' || *p == '\n') ++p;
    model = p;
    while (*p && *p != '\r' && *p != '\n') ++p;
    *p = 0;
    if (equal(v, "1.12") && equal(model, "NX500")) return 1;
    say("REFUSE: requires NX500 firmware 1.12\n");
    return 0;
}
