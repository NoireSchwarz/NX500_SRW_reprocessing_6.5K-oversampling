/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_DOWNLOAD_H
#define NXL_WIFI_DOWNLOAD_H

#define FILE_CHUNK 32768u

struct wifi_download {
    char name[81];
    unsigned long long expected, queued, started, first_body, max_read_ms;
    unsigned read_retries;
};

#endif
