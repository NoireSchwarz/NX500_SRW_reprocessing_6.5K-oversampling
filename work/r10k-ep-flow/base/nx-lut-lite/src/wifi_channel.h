/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_CHANNEL_H
#define NXL_WIFI_CHANNEL_H

#define NXL_AP_PRIMARY "48"
#define NXL_AP_SECONDARY "44"
#define NXL_AP_CENTER "46"
#define NXL_AP_WIDTH "40"
#define NXL_AP_MHZ 5240
#define NXL_AP_MHZ_TEXT "5240"
#define NXL_AP_DAEMON "/usr/sbin/hostapd"
#define NXL_AP_STOCK_CONF "/opt/var/lib/misc/hostapd.conf"
#define NXL_AP_DESCRIPTION "frequency_mhz=" NXL_AP_MHZ_TEXT " channel=" NXL_AP_PRIMARY \
    " secondary=" NXL_AP_SECONDARY " center=" NXL_AP_CENTER " requested_width_mhz=" NXL_AP_WIDTH

static inline int wifi_frequency_is_24ghz(int mhz)
{
    return (mhz >= 2412 && mhz <= 2472 && (mhz - 2412) % 5 == 0) || mhz == 2484;
}

#endif
