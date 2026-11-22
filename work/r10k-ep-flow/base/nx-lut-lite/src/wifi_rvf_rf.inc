/* SPDX-License-Identifier: AGPL-3.0-only */

static unsigned rf_le16(const unsigned char *p)
{
    return p[0] | ((unsigned)p[1] << 8);
}

/* The two fixed requests have action GET. Query only an active, owned AP.
 * Keep the raw firmware fields distinct from measured radiated power. */
static void rvf_read_power(void)
{
    static const unsigned char ap_get[] = {
        0xb0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 0x2f, 0x01, 1, 0, 0,
    };
    static const unsigned char rf_get[] = {0x1e, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    int mhz = wifi_frequency_mhz("uap0");
    if (physical_usb() != 0 || !wifi_frequency_is_24ghz(mhz))
        return;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        rvf_stage("POWER_QUERY_SOCKET", errno, mhz, 0);
        return;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (physical_usb() != 0)
            break;

        unsigned char buffer[2048] = "MRVL_CMDhostcmd";
        uint32_t size = i ? sizeof rf_get : sizeof ap_get;
        memcpy(buffer + 15, &size, sizeof size);
        memcpy(buffer + 19, i ? rf_get : ap_get, size);
        struct { void *data; int used, total; } command = {buffer, 0, sizeof buffer};
        struct ifreq call = {0};
        strcpy(call.ifr_name, "uap0");
        call.ifr_data = (void *)&command;
        int error = ioctl(fd, 0x89fe, &call) ? errno : 0;
        const unsigned char *p = buffer + 19;
        unsigned code = rf_le16(p);
        unsigned result = rf_le16(p + 6);
        int ok = !error && !result && rf_le16(p + 2) == size && rf_le16(p + 8) == 0;
        if (!i && ok && code == 0x80b0 && rf_le16(p + 10) == 0x012f && rf_le16(p + 12) == 1)
            rvf_stage("AP_POWER_BSS_CONFIG", p[14], mhz, 0);
        else if (i && ok && code == 0x801e)
            rvf_stage("RF_POWER_CURRENT_MIN_MAX", rf_le16(p + 10), p[13], p[12]);
        else
            rvf_stage(i ? "RF_POWER_UNAVAILABLE" : "AP_POWER_UNAVAILABLE", error, code, result);
    }
    close(fd);
}
