/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_RVF_AP_H
#define NXL_WIFI_RVF_AP_H

#define RVF_AP_PROFILE WIFI_TMP "/wifi-radio/rvf-ap-profile"
#define RVF_AP_MAGIC 0x41503643u

struct rvf_ap_profile {
    uint32_t magic, checksum;
    int32_t owner, camera;
    uint64_t owner_tick, camera_tick;
    unsigned ifindex;
    char ip[32], ssid[33];
};

static int rvf_ap_link(const struct rvf_owner *owner, char ip[32], unsigned *ifindex)
{
    uint32_t mode;
    unsigned index = if_nametoindex("uap0");
    if (!index || !interface_ip("uap0", ip) ||
        !rvf_native_state(owner->camera, owner->camera_tick, &mode) || mode != 2 ||
        if_nametoindex("uap0") != index)
        return 0;
    *ifindex = index;
    return 1;
}

static int rvf_profile_credentials(const struct rvf_owner *owner, struct hot_state *ap)
{
    struct rvf_ap_profile profile;
    unsigned index;
    char ip[32];
    if (!wire_read(RVF_AP_PROFILE, &profile, sizeof profile, RVF_AP_MAGIC) ||
        profile.owner != owner->pid || profile.owner_tick != owner->tick ||
        profile.camera != owner->camera || profile.camera_tick != owner->camera_tick ||
        !hot_ascii(profile.ssid, sizeof profile.ssid, 1) || profile.ip[31] ||
        !wire_process(owner->pid, owner->tick, RVF_EXE) ||
        !rvf_ap_link(owner, ip, &index) || index != profile.ifindex || strcmp(ip, profile.ip))
        return 0;

    *ap = (struct hot_state){.pid=owner->camera, .tick=owner->camera_tick,
                            .state=2, .ready=1, .security=0};
    strcpy(ap->ip, ip);
    strcpy(ap->ssid, profile.ssid);
    return 1;
}

#endif
