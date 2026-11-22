/* SPDX-License-Identifier: AGPL-3.0-only */

static int ap_rvf_has_frame(const struct rvf_owner *owner)
{
    struct rvf_record stream;
    return rvf_record_read(&stream) && stream.state == RVF_STREAMING && stream.frames &&
        stream.server == owner->pid && stream.server_tick == owner->tick &&
        stream.camera == owner->camera && stream.camera_tick == owner->camera_tick &&
        stream.updated + 3000 >= monotime() && wire_process(stream.pid, stream.tick, RVF_EXE);
}

/* Read the private config passed to the original daemon by ap_relay. Treat
 * only the open, visible, single-BSS profile as authoritative for QR input. */
static int ap_open_ssid(char *config, size_t bytes, char ssid[33])
{
    if (!bytes || config[bytes - 1] != '\n' || memchr(config, '\0', bytes) ||
        memchr(config, '\r', bytes))
        return 0;

    unsigned seen = 0;
    for (char *line = config, *end = config + bytes; line < end;) {
        char *next = strchr(line, '\n');
        *next++ = '\0';
        char *key = line;
        line = next;
        if (!*key || *key == '#')
            continue;
        char *value = strchr(key, '=');
        if (!value || strcspn(key, " \t=") != (size_t)(value - key))
            return 0;
        *value++ = '\0';

        unsigned field;
        if (!strcmp(key, "interface")) {
            if (strcmp(value, "uap0")) return 0;
            field = 1;
        } else if (!strcmp(key, "ssid")) {
            if (!hot_ascii(value, 33, 1)) return 0;
            strcpy(ssid, value);
            field = 2;
        } else if (!strcmp(key, "wpa")) {
            if (strcmp(value, "0")) return 0;
            field = 4;
        } else if (!strcmp(key, "ieee8021x")) {
            if (strcmp(value, "0")) return 0;
            field = 8;
        } else if (!strcmp(key, "auth_algs")) {
            if (strcmp(value, "1") && strcmp(value, "3")) return 0;
            field = 16;
        } else if (!strcmp(key, "ignore_broadcast_ssid")) {
            if (strcmp(value, "0")) return 0;
            field = 32;
        } else if (!strcmp(key, "osen")) {
            if (strcmp(value, "0")) return 0;
            field = 64;
        } else {
            if (!strcmp(key, "bss") || !strcmp(key, "ssid2") ||
                !strncmp(key, "wep_", 4) || !strncmp(key, "owe_", 4))
                return 0;
            continue;
        }
        if (seen & field)
            return 0;
        seen |= field;
    }
    return (seen & 3) == 3;
}

static void ap_publish_rvf_profile(const struct rvf_owner *owner, const char *ip, unsigned index)
{
    char path[160], config[8256];
    ap_config_path(path);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        note("RVF_QR_PROFILE_CONFIG_UNAVAILABLE");
        return;
    }
    struct stat st;
    ssize_t n = -1;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && !st.st_uid && !(st.st_mode & 077) &&
        st.st_size > 0 && st.st_size <= (off_t)sizeof config)
        n = read(fd, config, sizeof config);
    close(fd);

    struct rvf_ap_profile profile = {.owner=owner->pid, .owner_tick=owner->tick,
                                    .camera=owner->camera, .camera_tick=owner->camera_tick};
    if (n <= 0 || n != st.st_size || !ap_open_ssid(config, (size_t)n, profile.ssid)) {
        note("RVF_QR_PROFILE_OPEN_UNCONFIRMED");
        return;
    }
    if (!rvf_ap_link(owner, profile.ip, &profile.ifindex) ||
        profile.ifindex != index || strcmp(profile.ip, ip) ||
        !wire_process(owner->pid, owner->tick, RVF_EXE)) {
        note("RVF_QR_PROFILE_LINK_CHANGED");
        return;
    }
    if (!wire_write(RVF_AP_PROFILE, &profile, sizeof profile, RVF_AP_MAGIC)) {
        note("RVF_QR_PROFILE_PUBLISH_FAILED");
        return;
    }
    note("RVF_QR_PROFILE_OPEN_VERIFIED");
}
