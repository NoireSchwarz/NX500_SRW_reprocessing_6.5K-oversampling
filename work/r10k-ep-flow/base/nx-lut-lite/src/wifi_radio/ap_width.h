/* SPDX-License-Identifier: AGPL-3.0-only */

static unsigned ap_u16(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

/* SD8887 UAP BSS_CONFIG, action GET, TLV 0x012a. The legacy driver writes
 * 0x31 for HT40 below with no explicit width bits. The firmware can also
 * report 0x39 with an explicit 40 MHz width. Never accept VHT80 (0x3d). */
static int ap_width_response(const unsigned char *p, size_t bytes)
{
    return bytes >= 16 && ap_u16(p) == 0x80b0 && ap_u16(p + 2) == 16 &&
        !ap_u16(p + 6) && !ap_u16(p + 8) && ap_u16(p + 10) == 0x012a &&
        ap_u16(p + 12) == 2 && p[15] == 48 && (p[14] == 0x31 || p[14] == 0x39);
}

static int ap_read_width(void)
{
    unsigned char buffer[2048] = "MRVL_CMDhostcmd";
    static const unsigned char request[] = {
        0xb0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0x2a, 0x01, 2, 0, 0, 0,
    };
    uint32_t size = sizeof request;
    memcpy(buffer + 15, &size, sizeof size);
    memcpy(buffer + 19, request, sizeof request);
    struct { void *data; int used, total; } command = {buffer, 0, sizeof buffer};
    struct ifreq call = {0};
    strcpy(call.ifr_name, "uap0");
    call.ifr_data = (void *)&command;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return 0;
    int ok = !ioctl(fd, 0x89fe, &call);
    int saved_error = errno;
    close(fd);
    const unsigned char *reply = buffer + 19;
    printf("AP_WIDTH_READ ok=%d errno=%d primary=%u bandcfg=0x%02x width_code=%u "
           "secondary_offset=%u command=0x%04x bytes=%u result=%u\n",
           ok, ok ? 0 : saved_error, reply[15], reply[14], (reply[14] >> 2) & 3u,
           (reply[14] >> 4) & 3u, ap_u16(reply), ap_u16(reply + 2), ap_u16(reply + 6));
    return ok && ap_width_response(reply, sizeof buffer - 19);
}

static int ap_confirm_active(const char *mode)
{
    char config[160];
    struct stat st;
    ap_config_path(config);
    if (!trial.ap_pending || lstat(config, &st) || !S_ISREG(st.st_mode) || st.st_uid || st.st_size <= 0)
        return failed("AP_PROFILE_CONFIG_UNCONFIRMED");
    const char *const read_width[] = {RADIO_INSTALLED, "--read-width", NULL};
    if (!command(read_width, 2200))
        return failed("AP_PROFILE_WIDTH_UNCONFIRMED");
    if (!ap_release_launch() || !phase_save(P_ACTIVE))
        return 0;
    char line[180];
    snprintf(line, sizeof line, "RADIO_ACTIVE " NXL_AP_DESCRIPTION " mode=%s", mode);
    note(line);
    return 1;
}
