/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_COMMON_H
#define NXL_COMMON_H
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/ioctl.h>
#include "keys.h"
struct nxl_event { long sec, usec; unsigned short type, code; int value; };
static int nxl_supported(void) {
    char ver[32], model[32];
    FILE *f = fopen("/etc/version.info", "r");
    if (!f) return 0;
    int ok = fgets(ver, sizeof ver, f) && fgets(model, sizeof model, f);
    fclose(f);
    if (!ok) return 0;
    ver[strcspn(ver, "\r\n")] = 0;
    model[strcspn(model, "\r\n")] = 0;
    return !strcmp(ver, "1.12") && !strcmp(model, "NX500");
}
static int nxl_input(int index) {
    char path[64];
    snprintf(path, sizeof path, "/dev/input/event%d", index);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        snprintf(path, sizeof path, "/dev/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    }
    return fd;
}
/* EVIOCGKEY is read-only; no input grab or injected key events. */
static int nxl_held(int fd, unsigned code) {
    unsigned long bits[8] = {0};
    if (fd < 0 || code >= sizeof bits * 8 ||
        ioctl(fd, _IOC(_IOC_READ, 'E', 0x18, sizeof bits), bits) < 0) return -1;
    return (int)((bits[code / (8 * sizeof(long))] >> (code % (8 * sizeof(long)))) & 1);
}
static int nxl_lock(const char *path) {
    mkdir("/tmp/nx-lut-lite", 0700);
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB)) { close(fd); return -1; }
    return fd;
}
#endif
