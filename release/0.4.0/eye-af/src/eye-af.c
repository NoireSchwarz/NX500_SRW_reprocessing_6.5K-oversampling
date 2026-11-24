/* SPDX-License-Identifier: AGPL-3.0-only */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "eye_policy.h"

/* Layout verified against NX500 1.12 capture-fw and libface. */
enum {
    FD_RESULT_PTR = 0x78,
    FD_RESULT_BYTES = 0x444,
    FD_HISTORY = 0x7c,
    FD_TRACK_COUNT = 0x454,
    FD_METER_Y = 0x488,
    FD_SHARED_BYTES = 0x494,
    FD_MAIN = 0x398,
    FD_COUNT = 0x39c,
    FD_VALID = 0x3a0,
    FD_SELF_SHOT = 0x44a,
    FD_SMILE = 0x44b,
    FD_WINK = 0x44c,
    FD_FRAME = 0x45c,
    FD_WIDTH = 528,
    FD_HEIGHT = 352,
    UI_FACE_DETECTION = 0x1fb,
    UI_CAPTURE_AF = 0xe3,
    UI_FACE_NORMAL = 1,
    UI_AF_MANUAL = 3
};

typedef int (*face_fn)(void *, unsigned char *, int, int, int, int);
typedef int (*eye_fn)(const unsigned char *, int, int, void *);
typedef int (*ui_get_fn)(int);

static const char face_symbol[] = "_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii";
static const char app_path[] = "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app";
static face_fn stock_face;
static eye_fn stock_eye;
static ui_get_fn ui_get;
static struct eye_track track;
static struct face_motion motion;
static void *track_owner;
static struct {
    unsigned char *result;
    uint32_t next_frame;
    struct eye_box original;
    struct eye_box published;
} pending;
static unsigned detect_calls, native_hits, eye_frames, peak_us, single_face_frames, eye_timeouts;
static unsigned probe_misses, early_tries, early_hits, face_gaps, extended_hold_frames;
enum loss_reason { FIRST, GEOMETRY, VALID_FLAG, NO_FACE, MULTIPLE, GATE, BOUNDS, RESET, EXPIRED, NO_EYE, LOSS_COUNT };
static enum loss_reason last_loss;
static unsigned wait_frames[LOSS_COUNT], jumps, invalid_flags, motion_frames;
static unsigned return_tries, return_hits, move_tries, move_hits, cooldown_frames;
static unsigned face_returns, gate_releases;
static bool previous_eye;
static unsigned private_frames, discarded_publications;

/* FaceDetection on firmware 1.12 only uses the result pointer and the face
 * history of this owner. The original FThread, mailbox and public pointer
 * remain at their real addresses throughout the callback. */
struct fd_work_owner {
    unsigned char unused_thread_fields[FD_RESULT_PTR];
    unsigned char *result;
    unsigned char history[FD_RESULT_BYTES];
};
_Static_assert(offsetof(struct fd_work_owner, history) == FD_HISTORY, "FD owner history ABI");
_Static_assert(sizeof(struct fd_work_owner) == 0x4c0, "FD owner size");
static uint64_t next_report;
/* Two additional stock eye calls in any rolling second, across all owners. */
static uint64_t older_early, latest_early;
static struct {
    void *owner;
    uint64_t last_face_at;
    unsigned zeros;
} gap;
static unsigned gap_returns, gap_first_hits, one_zero_returns, longest_zero_run, longest_return_ms;
static unsigned gap_probes, gap_hits, gap_budget_waits, gap_spacing_waits;
enum previous_box { NO_PREVIOUS_BOX, FACE_RESTORED, STOCK_REPLACED_BOX, EYE_WITH_OTHER_FRAME, HISTORY_COUNT };
static unsigned gap_history[HISTORY_COUNT];

static void observe_zero_result(void *owner)
{
    if (track.valid) {
        gap.owner = owner;
        gap.last_face_at = track.last_frame;
        gap.zeros = 1;
    } else if (gap.owner == owner && gap.zeros < UINT32_MAX) {
        gap.zeros++;
    }
}

static bool finish_zero_run(void *owner, uint64_t now)
{
    bool returned = gap.owner == owner && gap.zeros != 0 && now >= gap.last_face_at;
    if (returned) {
        uint64_t ms = (now - gap.last_face_at) / 1000000;
        gap_returns++;
        one_zero_returns += gap.zeros == 1;
        if (gap.zeros > longest_zero_run) longest_zero_run = gap.zeros;
        if (ms > longest_return_ms) longest_return_ms = ms > UINT32_MAX ? UINT32_MAX : (unsigned)ms;
    }
    gap.owner = NULL;
    gap.zeros = 0;
    return returned;
}

static void end_eye_run(bool eligible)
{
    if (previous_eye) {
        if (eligible) face_returns++;
        else gate_releases++;
        previous_eye = false;
    }
}

static void lose_eye(enum loss_reason reason, bool allow_retry)
{
    if (track.valid) {
        jumps += reason == GEOMETRY;
        invalid_flags += reason == VALID_FLAG;
        face_gaps += reason == NO_FACE && allow_retry;
    }
    track.retry_pending = allow_retry && (track.valid || track.retry_pending);
    track.valid = false;
    track.candidate_valid = false;
    if (reason != GEOMETRY)
        motion.previous_at = motion.latest_at = 0;
    last_loss = reason;
}

static uint64_t clock_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000000 + (unsigned)now.tv_nsec;
}

static unsigned read_value(const unsigned char *result, unsigned offset)
{
    unsigned value;
    memcpy(&value, result + offset, sizeof(value));
    return value;
}

static unsigned char *face_result(const void *instance)
{
    unsigned char *result = NULL;
    if (instance != NULL)
        memcpy(&result, (const unsigned char *)instance + FD_RESULT_PTR, sizeof(result));
    return result;
}

static bool from_library(void *symbol, const char *expected)
{
    Dl_info info;
    return symbol && dladdr(symbol, &info) && !strcmp(info.dli_fname, expected);
}

static void report(const char *state, uint64_t now)
{
    char line[1408];
    int length = snprintf(line, sizeof(line),
        "NXL_EYE_AF2 pid=%ld state=%s native=%u extra=%u eye_frames=%u peak_us=%u reduced=%u single_face_frames=%u eye_timeouts=%u"
        " probe_misses=%u early_tries=%u early_hits=%u face_gaps=%u extended_hold_frames=%u"
        " jumps=%u invalid_flags=%u motion_frames=%u return_tries=%u return_hits=%u move_tries=%u move_hits=%u cooldown_frames=%u"
        " wait_first=%u wait_geom=%u wait_valid=%u wait_no_face=%u wait_multi=%u wait_gate=%u wait_bounds=%u"
        " wait_reset=%u wait_expired=%u face_returns=%u gate_releases=%u wait_no_eye=%u"
        " gap_returns=%u gap_first_hits=%u one_zero_returns=%u longest_zero_run=%u longest_return_ms=%u"
        " gap_probes=%u gap_hits=%u gap_budget_waits=%u prior_unknown=%u prior_face=%u prior_changed=%u"
        " prior_eye_seq=%u gap_spacing_waits=%u private_frames=%u discarded_publications=%u schema=28\n",
        (long)getpid(), state, native_hits, detect_calls, eye_frames, peak_us, (unsigned)track.reduced_rate,
        single_face_frames, eye_timeouts, probe_misses, early_tries, early_hits, face_gaps, extended_hold_frames,
        jumps, invalid_flags, motion_frames, return_tries, return_hits, move_tries, move_hits, cooldown_frames,
        wait_frames[FIRST], wait_frames[GEOMETRY], wait_frames[VALID_FLAG], wait_frames[NO_FACE],
        wait_frames[MULTIPLE], wait_frames[GATE], wait_frames[BOUNDS], wait_frames[RESET],
        wait_frames[EXPIRED], face_returns, gate_releases, wait_frames[NO_EYE],
        gap_returns, gap_first_hits, one_zero_returns, longest_zero_run, longest_return_ms,
        gap_probes, gap_hits, gap_budget_waits, gap_history[NO_PREVIOUS_BOX], gap_history[FACE_RESTORED],
        gap_history[STOCK_REPLACED_BOX], gap_history[EYE_WITH_OTHER_FRAME], gap_spacing_waits, private_frames, discarded_publications);
    if (length <= 0 || (unsigned)length >= sizeof(line))
        return;
    int fd = open("/tmp/nx-lut-lite/eye-af.status", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd >= 0) {
        (void)write(fd, line, (size_t)length);
        (void)close(fd);
    }
    next_report = now + 5000000000ULL;
}

static bool resume_ready(void)
{
    static uint64_t next_check;
    uint64_t now = clock_ns();
    if (!now || now < next_check)
        return false;
    next_check = now + 1000000000ULL;
    char value[24], expected[24];
    int fd = open("/tmp/nx-lut-lite/eye-af.resume-ready", O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return false;
    ssize_t length = read(fd, value, sizeof(value));
    close(fd);
    int n = snprintf(expected, sizeof(expected), "%ld\n", (long)getpid());
    return n > 0 && n < (int)sizeof(expected) && length == n && !memcmp(value, expected, (size_t)n);
}

static void bind_after_resume(void)
{
    char path[sizeof(app_path) + 8];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0)
        return;
    path[length] = '\0';
    if (strcmp(path, app_path))
        return;
    if (!from_library((void *)stock_face, "/usr/lib/libcapture-fw-prod.so")) {
        report("BIND_CAPTURE_UNVERIFIED", clock_ns());
        return;
    }
    eye_fn detect = (eye_fn)dlsym(RTLD_DEFAULT, "SDIC_EyeDetection");
    ui_get_fn get = (ui_get_fn)dlsym(RTLD_DEFAULT, "_Z12UI_Get_Value14eUI_VALUE_ITEM");
    if (!from_library((void *)detect, "/usr/lib/libface.so")) {
        report("BIND_EYE_UNVERIFIED", clock_ns());
        return;
    }
    if (!from_library((void *)get, app_path)) {
        report("BIND_UI_UNVERIFIED", clock_ns());
        return;
    }
    stock_eye = detect;
    ui_get = get;
    report("READY", clock_ns());
}

static bool normal_face_af(void)
{
    if (!ui_get || ui_get(UI_FACE_DETECTION) != UI_FACE_NORMAL)
        return false;
    int af_mode = ui_get(UI_CAPTURE_AF);
    return af_mode >= 0 && af_mode < UI_AF_MANUAL;
}

static enum previous_box restore_previous(const unsigned char *published, unsigned char *result)
{
    /* t_Main advances FD_FRAME after publishing. Restore only our own result
     * in the private input before tracking the next frame. */
    enum previous_box history = NO_PREVIOUS_BOX;
    if (result && pending.result == published) {
        if (memcmp(result, &pending.published, sizeof(pending.published)) != 0) {
            history = STOCK_REPLACED_BOX;
        } else if (read_value(result, FD_FRAME) != pending.next_frame) {
            history = EYE_WITH_OTHER_FRAME;
        } else {
            memcpy(result, &pending.original, sizeof(pending.original));
            history = FACE_RESTORED;
        }
    }
    pending.result = NULL;
    return history;
}

static void eye_rectangles(const unsigned char *result, struct eye_box eyes[2])
{
    memset(eyes, 0, 2 * sizeof(*eyes));
    if (result[0xd8] == 1)
        memcpy(&eyes[0], result + 0xc8, sizeof(*eyes));
    if (result[0x1a0] == 1)
        memcpy(&eyes[1], result + 0x190, sizeof(*eyes));
}

int nxl_stock_face(void *instance, unsigned char *image, int width, int height, int tracking, int orientation)
    __asm__("_ZN20CFaceDetection_Linux13FaceDetectionEPhiiii");

struct publication_note {
    uint64_t observed_at;
    bool first_after_gap, moved, extended;
};

static void select_eye(void *instance, unsigned char *published, unsigned char *data,
                       unsigned char *image, int width, int height, bool active, int rc,
                       enum previous_box history, const struct eye_box previous[2],
                       struct publication_note *note)
{
    bool current = active && rc >= 0;
    unsigned faces = current ? read_value(data, FD_COUNT) : 0;
    if (!current || data[FD_VALID] != 1 || faces != 1 || read_value(data, FD_MAIN) != 0) {
        enum loss_reason reason = GATE;
        if (current) {
            if (faces == 0) reason = NO_FACE;
            else if (faces > 1) reason = MULTIPLE;
            else if (read_value(data, FD_MAIN) == 0) reason = VALID_FLAG;
        }
        if (reason == NO_FACE && track_owner == instance) {
            if (track.valid) gap_history[history]++;
            observe_zero_result(instance);
        } else {
            gap.owner = NULL;
        }
        lose_eye(reason, track_owner == instance && (reason == NO_FACE || reason == VALID_FLAG));
        end_eye_run(false);
        return;
    }

    uint64_t now = clock_ns();
    if (!now) {
        gap.owner = NULL;
        end_eye_run(false);
        return;
    }
    if (instance != track_owner) {
        last_loss = track_owner ? RESET : FIRST;
        motion.previous_at = motion.latest_at = 0;
        memset(&track, 0, sizeof(track));
        track_owner = instance;
    } else if (track.last_frame && now - track.last_frame > 1200000000ULL) {
        lose_eye(RESET, false);
    }
    track.last_frame = now;
    struct eye_box face;
    memcpy(&face, data, sizeof(face));
    if (!eye_box_in_frame(face, width, height) || face.w < 32 || face.h < 32 ||
        face.w > 160 || face.h > 160) {
        gap.owner = NULL;
        lose_eye(BOUNDS, false);
        end_eye_run(false);
        return;
    }

    single_face_frames++;
    bool first_after_gap = finish_zero_run(instance, now);
    struct eye_box eyes[2];
    eye_rectangles(data, eyes);
    bool same_position = eye_continuous(&motion, track.last_face, face, now);
    if (track.valid && !same_position)
        lose_eye(GEOMETRY, now < track.expires);
    if (track.retry_pending && now >= track.expires)
        track.retry_pending = false;
    /* A single discontinuity can be a false face. Two consecutive, consistent
     * single-face results at the new location may request a fresh eye there. */
    bool new_position = track.retry_pending && !same_position && track.candidate_valid &&
                        eye_continuous(&motion, track.candidate_face, face, now);
    motion.previous = motion.latest;
    motion.previous_at = motion.latest_at;
    motion.latest = face;
    motion.latest_at = now;
    track.candidate_valid = track.retry_pending && !same_position;
    if (track.candidate_valid)
        track.candidate_face = face;
    bool lost_position = !track.valid && (last_loss == GEOMETRY || last_loss == VALID_FLAG || last_loss == NO_FACE);
    bool can_recheck = track.retry_pending && (same_position || new_position) && (track.reduced_rate || lost_position) &&
                       now < track.next_sample;
    bool gap_return = can_recheck && !track.valid && last_loss == NO_FACE && same_position;
    bool spaced = now >= track.last_probe + EYE_RETRY_DELAY_NS &&
                  (!latest_early || now >= latest_early + EYE_RETRY_DELAY_NS);
    bool budget_open = !older_early || now >= older_early + EYE_SLOW_SAMPLE_NS;
    bool retry_window = can_recheck && spaced;
    bool retry = retry_window && budget_open && (gap_return || now >= track.retry_allowed_after);
    /* A longer hold must not push the next ordinary eye measurement back. */
    uint64_t refresh_delay = track.reduced_rate ? EYE_SLOW_SAMPLE_NS : EYE_HOLD_NS - EYE_REFRESH_LEAD_NS;
    bool due = now >= track.next_sample && (!track.valid || now >= track.confirmed_at + refresh_delay);
    uint64_t publish_time = now;
    if (memcmp(eyes, previous, sizeof(eyes)) && eye_choose(&track, face, eyes, now)) {
        native_hits++;
        track.retry_pending = false;
    } else if (retry || due) {
        /* Keep the original FD object and metering statistics owned by stock.
         * SDIC_EyeDetection receives a one-face copy and the existing luma. */
        _Alignas(unsigned) unsigned char sample[FD_RESULT_BYTES];
        memcpy(sample, data, sizeof(sample));
        sample[0xd8] = sample[0x1a0] = 0;
        int result = stock_eye(image, width, height, sample);
        uint64_t end = clock_ns();
        uint64_t elapsed = end >= now ? end - now : 0;
        publish_time = now + elapsed;
        track.last_probe = now;
        unsigned us = (unsigned)(elapsed / 1000u);
        if (us > peak_us)
            peak_us = us;
        detect_calls++;
        if (elapsed > EYE_SLOW_CALL_NS)
            track.reduced_rate = true;
        eye_rectangles(sample, eyes);
        bool found = result > 0 && eye_choose(&track, face, eyes, now);
        if (!found) {
            probe_misses++;
            if (!track.valid) last_loss = NO_EYE;
        }
        if (retry) {
            early_tries++;
            older_early = latest_early;
            latest_early = now;
            if (gap_return) {
                gap_probes++;
                gap_hits += found;
            }
            if (found)
                early_hits++;
            if (new_position) {
                move_tries++;
                move_hits += found;
            } else if (lost_position) {
                return_tries++;
                return_hits += found;
            }
            track.retry_allowed_after = now + EYE_RETRY_COOLDOWN_NS;
        }
        /* Keep the old expiry after a miss. Avoid a second call immediately after a failed gap probe. */
        if (!retry || found)
            track.next_sample = now + (track.reduced_rate ? EYE_SLOW_SAMPLE_NS : EYE_SAMPLE_NS);
        else if (gap_return && track.next_sample < now + EYE_RETRY_DELAY_NS)
            track.next_sample = now + EYE_RETRY_DELAY_NS;
        track.retry_pending = !found && !retry && track.reduced_rate && track.valid &&
                              now < track.expires && same_position;
    }

    struct eye_box target;
    if (track.valid && publish_time >= track.expires) {
        eye_timeouts++;
        lose_eye(EXPIRED, false);
    }
    if (!normal_face_af())
        lose_eye(GATE, false);
    if (eye_cached_target(&track, face, publish_time, &target)) {
        pending.result = published;
        pending.next_frame = read_value(data, FD_FRAME) + 1;
        pending.original = face;
        pending.published = target;
        /* The stock FD thread publishes this primary box to the RTOS after
         * the call. Its AF region conversion, motor control and timing are native. */
        memcpy(data, &target, sizeof(target));
        note->first_after_gap = first_after_gap;
        note->moved = !eye_same_face(track.face, face);
        note->extended = track.reduced_rate &&
                         publish_time - track.confirmed_at >= EYE_SLOW_SAMPLE_NS + EYE_REFRESH_LEAD_NS;
    } else {
        end_eye_run(true);
        wait_frames[last_loss]++;
        if (retry_window && !retry)
            cooldown_frames++;
        if (gap_return && !spaced) gap_spacing_waits++;
        else if (gap_return && !budget_open) gap_budget_waits++;
    }
    note->observed_at = now;
}

static bool selection_enabled(const unsigned char *public_result)
{
    return !public_result[FD_SELF_SHOT] && !public_result[FD_SMILE] && !public_result[FD_WINK];
}

static bool same_frame(void *instance, unsigned char *published, unsigned frame)
{
    return face_result(instance) == published && read_value(published, FD_FRAME) == frame;
}

static void forget_publication(void)
{
    pending.result = NULL;
    gap.owner = NULL;
    lose_eye(RESET, false);
    end_eye_run(false);
}

int nxl_stock_face(void *instance, unsigned char *image, int width, int height, int tracking, int orientation)
{
    if (!stock_face)
        stock_face = (face_fn)dlsym(RTLD_NEXT, face_symbol);
    if (!stock_face)
        return -1;
    if (!stock_eye && resume_ready())
        bind_after_resume();
    if (!stock_eye)
        return stock_face(instance, image, width, height, tracking, orientation);

    unsigned char *published = face_result(instance);
    if (!published) {
        forget_publication();
        return stock_face(instance, image, width, height, tracking, orientation);
    }
    bool active = image && width == FD_WIDTH && height == FD_HEIGHT &&
                  normal_face_af() && selection_enabled(published);
    if (!active && !pending.result) {
        gap.owner = NULL;
        lose_eye(GATE, false);
        end_eye_run(false);
        return stock_face(instance, image, width, height, tracking, orientation);
    }

    _Alignas(unsigned) unsigned char work[FD_SHARED_BYTES];
    struct fd_work_owner owner;
    unsigned char *real_history = (unsigned char *)instance + FD_HISTORY;
    const unsigned frame = read_value(published, FD_FRAME);
    memcpy(work, published, sizeof(work));
    memcpy(owner.history, real_history, sizeof(owner.history));
    owner.result = work;
    enum previous_box history = restore_previous(published, work);
    struct eye_box previous[2] = {{0}};
    if (active)
        eye_rectangles(work, previous);
    int rc = stock_face(&owner, image, width, height, tracking, orientation);
    if (!same_frame(instance, published, frame)) {
        discarded_publications++;
        forget_publication();
        return rc;
    }

    struct publication_note note = {0};
    select_eye(instance, published, work, image, width, height,
               active && selection_enabled(published), rc, history, previous, &note);
    if (!same_frame(instance, published, frame)) {
        discarded_publications++;
        forget_publication();
        return rc;
    }
    if (pending.result && !selection_enabled(published)) {
        memcpy(work, &pending.original, sizeof(pending.original));
        forget_publication();
    }

    /* Keep producer history full-face. Copy only the stock-owned payload,
     * tracking counter and Y/Cb/Cr. Mode flags and the producer's sequence
     * are independently owned. The public primary changes only to its final
     * value, before the native worker advances the frame sequence. */
    memcpy(real_history, owner.history, sizeof(owner.history));
    const size_t primary = sizeof(struct eye_box);
    memcpy(published + primary, work + primary, FD_RESULT_BYTES - primary);
    memcpy(published + FD_TRACK_COUNT, work + FD_TRACK_COUNT, sizeof(unsigned));
    memcpy(published + FD_METER_Y, work + FD_METER_Y, 3 * sizeof(unsigned));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (memcmp(published, work, primary))
        memcpy(published, work, primary);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    private_frames++;
    if (pending.result) {
        eye_frames++;
        previous_eye = true;
        gap_first_hits += note.first_after_gap;
        motion_frames += note.moved;
        extended_hold_frames += note.extended;
    }
    if (note.observed_at && note.observed_at >= next_report)
        report(eye_frames ? "ACTIVE" : "FACE_ONLY", note.observed_at);
    return rc;
}
