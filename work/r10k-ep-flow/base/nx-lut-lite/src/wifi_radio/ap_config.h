/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../wifi_channel.h"

/* The agent has just generated this file for the owned uap0. Copy it with
 * the same SSID, credentials and control socket. Accept the pinned template
 * once and reject an ambiguous or already overridden key. */
static size_t render_ap_config(const char *input, size_t length, char *output, size_t capacity)
{
    static const struct { const char *key, *original, *replacement; } fields[] = {
        {"interface", "uap0", NULL}, {"driver", "nl80211", NULL}, {"hw_mode", "a", NULL},
        {"ieee80211n", "1", NULL}, {"ieee80211ac", "1", "0"},
        {"channel", "36", NXL_AP_PRIMARY}, {"vht_oper_chwidth", "1", "0"},
        {"vht_oper_centr_freq_seg0_idx", "42", NXL_AP_CENTER},
        {"wmm_enabled", "1", NULL},
        {"ht_capab", NULL, NULL}, {"vht_oper_centr_freq_seg1_idx", NULL, NULL},
    };
    const unsigned required = (1u << 8) - 1;
    unsigned seen = 0;
    size_t written = 0;
    if (!length || input[length - 1] != '\n' || memchr(input, 0, length) || memchr(input, '\r', length))
        return 0;
    for (const char *line = input, *end = input + length; line < end;) {
        const char *newline = memchr(line, '\n', (size_t)(end - line));
        const char *next = newline + 1;
        const char *eq = memchr(line, '=', (size_t)(next - line - 1));
        const char *value = NULL;
        if (eq) {
            for (size_t i = 0; i < sizeof fields / sizeof *fields; ++i) {
                size_t key = strlen(fields[i].key);
                if ((size_t)(eq - line) != key || memcmp(line, fields[i].key, key))
                    continue;
                unsigned bit = 1u << i;
                const char *old = fields[i].original;
                if ((seen & bit) || !old || strlen(old) != (size_t)(next - eq - 2) ||
                    memcmp(eq + 1, old, strlen(old)))
                    return 0;
                seen |= bit;
                value = fields[i].replacement;
                break;
            }
        }
        size_t line_size = value ? (size_t)(eq + 1 - line) + strlen(value) + 1 : (size_t)(next - line);
        if (line_size > capacity - written)
            return 0;
        if (value) {
            size_t prefix = (size_t)(eq + 1 - line);
            memcpy(output + written, line, prefix);
            memcpy(output + written + prefix, value, strlen(value));
            output[written + line_size - 1] = '\n';
        } else {
            memcpy(output + written, line, line_size);
        }
        written += line_size;
        line = next;
    }
    if ((seen & required) != required)
        return 0;
    const char *extension = (seen & (1u << 8)) ? "ht_capab=[HT40-]\n" : "ht_capab=[HT40-]\nwmm_enabled=1\n";
    if (strlen(extension) > capacity - written)
        return 0;
    memcpy(output + written, extension, strlen(extension));
    return written + strlen(extension);
}
