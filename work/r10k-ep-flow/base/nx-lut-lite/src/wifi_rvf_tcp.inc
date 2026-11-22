/* SPDX-License-Identifier: AGPL-3.0-only */

#define RVF_TCP_LOG "/mnt/mmc/NXDEMO51/logs/wifi-rvf-tcp-rc6148.log"
#define RVF_TCP_WINDOW_MS 500u
#define RVF_TCP_ROWS 512u

struct rvf_tcp_window {
    unsigned long long connection, from_ms, to_ms;
    unsigned long long pending_ms, max_pending_ms;
    uint32_t first_id, last_id, pending_id;
    uint32_t queued, skipped, max_skip, max_skip_at_id;
    unsigned no_pollout, eagain, partial;
    uint32_t tcp_retrans_total, tcp_retrans, tcp_unacked, tcp_rtt_us;
    int tcp_info, retrans_valid, outq_bytes, error;
    const char *end;
};

/* One browser push socket is admitted at a time. Keep the recent windows
 * in RAM and append them only after RVF and radio restoration. */
static struct {
    struct rvf_tcp_window window, rows[RVF_TCP_ROWS];
    unsigned long long stored, pending_start, pending_accounted;
    uint32_t previous_retrans, pending_id;
    int previous_valid;
} rvf_tcp;

static void rvf_tcp_open(int fd, unsigned long long connection, unsigned long long now)
{
    struct tcp_info info;
    rvf_tcp.previous_valid = wifi_tcp_info(fd, &info);
    rvf_tcp.previous_retrans = info.tcpi_total_retrans;
    rvf_tcp.pending_id = 0;
    rvf_tcp.window = (struct rvf_tcp_window){.connection = connection, .from_ms = now};
}

static void rvf_tcp_select(unsigned long long now, uint32_t id, uint32_t skipped)
{
    struct rvf_tcp_window *row = &rvf_tcp.window;
    if (!row->first_id)
        row->first_id = id;
    row->last_id = id;
    row->skipped += skipped;
    if (skipped > row->max_skip) {
        row->max_skip = skipped;
        row->max_skip_at_id = id;
    }
    rvf_tcp.pending_id = id;
    rvf_tcp.pending_start = rvf_tcp.pending_accounted = now;
}

static void rvf_tcp_account_pending(unsigned long long now)
{
    if (!rvf_tcp.pending_id || now < rvf_tcp.pending_accounted)
        return;
    struct rvf_tcp_window *row = &rvf_tcp.window;
    row->pending_ms += now - rvf_tcp.pending_accounted;
    unsigned long long age = now - rvf_tcp.pending_start;
    if (age > row->max_pending_ms)
        row->max_pending_ms = age;
    rvf_tcp.pending_accounted = now;
}

static void rvf_tcp_queued(unsigned long long now)
{
    rvf_tcp_account_pending(now);
    rvf_tcp.pending_id = 0;
    rvf_tcp.window.queued++;
}

static void rvf_tcp_capture(int fd, unsigned long long now, const char *end, int error)
{
    struct rvf_tcp_window *row = &rvf_tcp.window;
    if (now < row->from_ms)
        now = row->from_ms;
    rvf_tcp_account_pending(now);
    row->to_ms = now;
    row->pending_id = rvf_tcp.pending_id;
    row->end = end;
    row->error = error;

    struct tcp_info info;
    row->tcp_info = wifi_tcp_info(fd, &info);
    if (row->tcp_info) {
        row->tcp_retrans_total = info.tcpi_total_retrans;
        row->tcp_unacked = info.tcpi_unacked;
        row->tcp_rtt_us = info.tcpi_rtt;
        if (rvf_tcp.previous_valid && info.tcpi_total_retrans >= rvf_tcp.previous_retrans) {
            row->retrans_valid = 1;
            row->tcp_retrans = info.tcpi_total_retrans - rvf_tcp.previous_retrans;
        }
        rvf_tcp.previous_retrans = info.tcpi_total_retrans;
    }
    rvf_tcp.previous_valid = row->tcp_info;
    if (ioctl(fd, TIOCOUTQ, &row->outq_bytes))
        row->outq_bytes = -1;

    rvf_tcp.rows[rvf_tcp.stored++ % RVF_TCP_ROWS] = *row;
    *row = (struct rvf_tcp_window){.connection = end ? 0 : row->connection, .from_ms = now};
}

static void rvf_tcp_tick(int fd, unsigned long long now)
{
    unsigned long long from = rvf_tcp.window.from_ms;
    if (now >= from && now - from >= RVF_TCP_WINDOW_MS)
        rvf_tcp_capture(fd, now, NULL, 0);
}

static int rvf_tcp_dump(int fd, long pid, unsigned long long tick, int mhz)
{
    unsigned long long count = rvf_tcp.stored < RVF_TCP_ROWS ? rvf_tcp.stored : RVF_TCP_ROWS;
    unsigned long long first = rvf_tcp.stored - count;
    if (dprintf(fd, "NXL_RVF_TCP_V1 version=" WIFI_VERSION " session=%ld_%llu mhz=%d "
               "primary=%u width_mhz=%u window_ms=%u rows=%llu overwritten=%llu\n",
               pid, tick, mhz,
               mhz == 2484 ? 14u : wifi_frequency_is_24ghz(mhz) ? (unsigned)(mhz - 2407) / 5u : 0u,
               0u, RVF_TCP_WINDOW_MS, count, first) < 0)
        return 0;
    for (unsigned long long i = first; i < rvf_tcp.stored; ++i) {
        if ((i - first) % 16 == 0 && physical_usb() != 0)
            return 0;
        const struct rvf_tcp_window *row = &rvf_tcp.rows[i % RVF_TCP_ROWS];
        if (dprintf(fd, "RVF_TCP conn=%llu from_ms=%llu to_ms=%llu first_id=%u last_id=%u "
                   "frames_queued=%u skipped=%u max_skip=%u max_skip_at_id=%u pending_id=%u "
                   "send_wait_ms=%llu max_frame_wait_ms=%llu no_pollout=%u eagain=%u partial=%u "
                   "tcp_info=%d retrans_valid=%d retrans=%u retrans_total=%u unacked=%u "
                   "rtt_us=%u outq_bytes=%d end=%s errno=%d\n",
                   row->connection, row->from_ms, row->to_ms, row->first_id, row->last_id,
                   row->queued, row->skipped, row->max_skip, row->max_skip_at_id, row->pending_id,
                   row->pending_ms, row->max_pending_ms, row->no_pollout, row->eagain, row->partial,
                   row->tcp_info, row->retrans_valid, row->tcp_retrans, row->tcp_retrans_total,
                   row->tcp_unacked, row->tcp_rtt_us, row->outq_bytes,
                   row->end ? row->end : "WINDOW", row->error) < 0)
            return 0;
    }
    return physical_usb() == 0 &&
        dprintf(fd, "NXL_RVF_TCP_END1 session=%ld_%llu rows=%llu\n", pid, tick, count) >= 0;
}

static void rvf_tcp_save(long pid, unsigned long long tick, int mhz)
{
    if (!rvf_tcp.stored || physical_usb() != 0)
        return;
    int fd = open(RVF_TCP_LOG, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) {
        fprintf(stderr, "RVF_TCP_LOG_OPEN_FAILED errno=%d\n", errno);
        return;
    }
    struct stat st;
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && physical_usb() == 0 && rvf_tcp_dump(fd, pid, tick, mhz);
    if (close(fd))
        ok = 0;
    if (!ok)
        fputs("RVF_TCP_LOG_INCOMPLETE\n", stderr);
}
