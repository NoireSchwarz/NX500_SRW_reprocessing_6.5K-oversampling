/* SPDX-License-Identifier: AGPL-3.0-only */
#include <sys/mount.h>
#include "ap_config.h"

/* A bind over the launch path exists only during the mod's AP start. The
 * original executable stays open and unmodified, even while hidden there. */
static int original_ap_fd = -1;

static int ap_path_matches(const char *path, uint64_t device, uint64_t inode)
{
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode) &&
        (uint64_t)st.st_dev == device && (uint64_t)st.st_ino == inode;
}

static int ap_hold_launch(void)
{
    struct stat stock, relay;
    int fd = open(NXL_AP_DAEMON, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return failed("AP_EXECUTABLE_UNAVAILABLE");
    if (fstat(fd, &stock) || !S_ISREG(stock.st_mode) || stock.st_uid ||
        lstat(RADIO_INSTALLED, &relay) || !S_ISREG(relay.st_mode) || relay.st_uid ||
        !ap_path_matches(NXL_AP_DAEMON, stock.st_dev, stock.st_ino) ||
        !hot_fingerprint(NXL_AP_DAEMON, AP_STOCK_BYTES, AP_STOCK_FNV)) {
        close(fd);
        return failed("AP_EXECUTABLE_IDENTITY_MISMATCH");
    }
    original_ap_fd = fd;
    trial.ap_device = stock.st_dev;
    trial.ap_inode = stock.st_ino;
    trial.relay_device = relay.st_dev;
    trial.relay_inode = relay.st_ino;
    trial.ap_fd = fd;
    trial.ap_pending = 1;
    if (!state_save())
        return failed("AP_LAUNCH_NOT_JOURNALED");
    if (mount(RADIO_INSTALLED, NXL_AP_DAEMON, NULL, MS_BIND, NULL) ||
        !ap_path_matches(NXL_AP_DAEMON, trial.relay_device, trial.relay_inode))
        return failed("AP_LAUNCH_BIND_FAILED");
    note("AP_LAUNCH_BOUND");
    return 1;
}

static int ap_release_launch(void)
{
    if (!trial.ap_pending)
        return 1;
    if (ap_path_matches(NXL_AP_DAEMON, trial.relay_device, trial.relay_inode) &&
        umount2(NXL_AP_DAEMON, MNT_DETACH))
        return failed("AP_LAUNCH_UNBIND_FAILED");
    if (!ap_path_matches(NXL_AP_DAEMON, trial.ap_device, trial.ap_inode))
        return failed("AP_ORIGINAL_EXECUTABLE_NOT_RESTORED");
    if (original_ap_fd >= 0) {
        close(original_ap_fd);
        original_ap_fd = -1;
    }
    trial.ap_pending = 0;
    if (!state_save()) {
        trial.ap_pending = 1;
        return failed("AP_LAUNCH_RESTORE_NOT_JOURNALED");
    }
    note("AP_ORIGINAL_EXECUTABLE_RESTORED");
    return 1;
}

static void ap_config_path(char path[160])
{
    snprintf(path, 160, TRIAL_TMP "/ap-%ld-%llu.conf", (long)trial.owner, trial.owner_tick);
}

static int ap_remove_config(void)
{
    if (!trial.ap_inode)
        return 1;
    char path[160];
    struct stat st;
    ap_config_path(path);
    if (lstat(path, &st))
        return errno == ENOENT;
    return S_ISREG(st.st_mode) && st.st_uid == 0 && !unlink(path);
}

static int ap_owned_launch(void)
{
    if (trial.phase != P_READY)
        return 0;
    if (trial.rvf_pid) {
        struct rvf_owner owner;
        if (!radio_rvf_owner(&owner) || owner.pid != trial.rvf_pid || owner.tick != trial.rvf_tick ||
            !wire_process(owner.pid, owner.tick, RVF_EXE))
            return -1;
        return owner.phase == RVF_ENTERING && owner.ap_up >= 0;
    }
    struct hot_state hot;
    if (!hot_same_epoch(&hot) || hot.phase < 0)
        return -1;
    return hot.owned && hot.phase == 1;
}

static int ap_stock_arguments(int argc, char *const *argv)
{
    return argc == 7 && !strcmp(argv[1], "-e") && !strcmp(argv[2], "/opt/var/lib/misc/hostapd.bin") &&
        !strcmp(argv[3], NXL_AP_STOCK_CONF) && !strcmp(argv[4], "-f") &&
        !strcmp(argv[5], "/var/log/hostapd.log") && !strcmp(argv[6], "-d");
}

static int ap_write_config(const char *path)
{
    char input[8192], output[sizeof input + 64];
    int fd = open(NXL_AP_STOCK_CONF, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return 0;
    struct stat st;
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 &&
        st.st_size > 0 && st.st_size < (off_t)sizeof input;
    ssize_t count = ok ? read(fd, input, sizeof input) : -1;
    close(fd);
    if (!ok || count != st.st_size)
        return 0;
    size_t length = render_ap_config(input, (size_t)count, output, sizeof output);
    if (!length)
        return 0;
    fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return 0;
    ok = write(fd, output, length) == (ssize_t)length && !fsync(fd);
    if (close(fd))
        ok = 0;
    return ok;
}

static int ap_launch_error(const char *reason)
{
    int fd = open(TRIAL_TMP "/controller.log", O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
    if (fd >= 0) {
        dprintf(fd, "AP_LAUNCH_FAILED reason=%s\n", reason);
        close(fd);
    }
    return 1;
}

static int ap_relay(int argc, char **argv)
{
    if (!owned_state_read() || !trial.ap_pending || !live_identity(trial.owner, trial.owner_tick))
        return ap_launch_error("AP_LEASE_LOST");
    char fd_path[96], config[160];
    snprintf(fd_path, sizeof fd_path, "/proc/%ld/fd/%d", (long)trial.owner, trial.ap_fd);
    int fd = open(fd_path, O_RDONLY | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || (uint64_t)st.st_dev != trial.ap_device ||
        (uint64_t)st.st_ino != trial.ap_inode || !live_identity(trial.owner, trial.owner_tick)) {
        if (fd >= 0)
            close(fd);
        return ap_launch_error("AP_ORIGINAL_FD_MISMATCH");
    }
    int owned = ap_owned_launch();
    if (owned != 0) {
        pid_t parent = getppid();
        pid_t supervisor = trial.owner;
        unsigned long long tick = trial.owner_tick;
        if (owned < 0 || !ap_stock_arguments(argc, argv) || physical_usb() != 0 ||
            !wire_process(parent, proc_ticks(parent), "/usr/bin/mobileap-agent")) {
            close(fd);
            return ap_launch_error("AP_OWNER_OR_ARGUMENTS_MISMATCH");
        }
        ap_config_path(config);
        if (!ap_write_config(config)) {
            close(fd);
            return ap_launch_error("AP_TEMPLATE_OR_CONFIG_WRITE_FAILED");
        }
        if (physical_usb() != 0 || !owned_state_read() || !trial.ap_pending ||
            trial.owner != supervisor || trial.owner_tick != tick ||
            !live_identity(supervisor, tick) || ap_owned_launch() != 1) {
            close(fd);
            return ap_launch_error("AP_OWNER_CHANGED_BEFORE_EXEC");
        }
        argv[3] = config;
    }
    /* The camera's 3.5 kernel has procfs and predates execveat. The open FD
     * resolves to the original executable, including after the bind ends. */
    snprintf(fd_path, sizeof fd_path, "/proc/self/fd/%d", fd);
    execv(fd_path, argv);
    close(fd);
    return ap_launch_error("AP_ORIGINAL_EXEC_FAILED");
}
