#ifndef NXL_EYE_POLICY_H
#define NXL_EYE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef NXL_EYE_YUNET
#define NXL_EYE_POINT_BACKEND
#endif

/* Unit is a pixel in the native face detector's image. */
struct eye_box {
    int x, y, w, h;
};

struct eye_point {
    int x, y;
    bool valid;
};

struct eye_track {
    /* face and target stay in the coordinates of the last real eye measurement. */
    struct eye_box face;
    struct eye_box target;
    struct eye_box last_face;
    struct eye_box candidate_face;
    uint64_t next_sample;
    uint64_t last_probe;
    uint64_t confirmed_at;
    uint64_t expires;
    uint64_t last_frame;
    bool valid;
    bool reduced_rate;
    bool retry_pending;
    bool candidate_valid;
    uint64_t retry_allowed_after;
    unsigned side;
#ifdef NXL_EYE_YUNET
    unsigned main_face;
    unsigned rate_streak;
    unsigned published_side;
    bool upright_eye;
    bool published_eye;
#endif
};

enum {
    EYE_SAMPLE_NS = 333000000,
    EYE_HOLD_NS = 580000000,
    EYE_SLOW_SAMPLE_NS = 1000000000,
    EYE_REFRESH_LEAD_NS = 160000000,
    EYE_SLOW_GRACE_NS = 280000000,
    EYE_RETRY_DELAY_NS = 100000000,
    EYE_RETRY_COOLDOWN_NS = 2000000000,
#ifdef NXL_EYE_YUNET
    EYE_SLOW_CALL_NS = 20000000,
    EYE_FAST_CALL_NS = 12000000,
    EYE_RATE_STREAK = 3
#else
    EYE_SLOW_CALL_NS = 16000000
#endif
};

static inline bool eye_box_in_frame(struct eye_box box, int width, int height)
{
    return box.x >= 0 && box.y >= 0 && box.w > 0 && box.h > 0 &&
           box.w <= width && box.h <= height &&
           box.x <= width - box.w && box.y <= height - box.h;
}

static inline bool eye_in_face(struct eye_box eye, struct eye_box face)
{
    return eye.w >= 3 && eye.h >= 3 && eye.x >= face.x && eye.y >= face.y &&
           eye.x - face.x <= face.w - eye.w && eye.y - face.y <= face.h - eye.h &&
           eye.y - face.y + eye.h / 2 < face.h * 2 / 3;
}

static inline int eye_abs(int x)
{
    return x < 0 ? -x : x;
}

static inline bool eye_same_face(struct eye_box first, struct eye_box second)
{
    return eye_abs(first.x - second.x) <= first.w / 5 &&
           eye_abs(first.y - second.y) <= first.h / 5 &&
           eye_abs(first.w - second.w) <= first.w / 5 &&
           eye_abs(first.h - second.h) <= first.h / 5;
}

struct face_motion {
    struct eye_box previous, latest;
    uint64_t previous_at, latest_at;
};

/* Allow a larger single step only when two actual, recent face movements
 * agree in speed and direction. Never extrapolate a face which is absent. */
static inline bool eye_continuous(const struct face_motion *m, struct eye_box from,
                                  struct eye_box to, uint64_t now)
{
    if (eye_same_face(from, to))
        return true;
    if (!m->previous_at || m->latest_at <= m->previous_at || now <= m->latest_at ||
        m->latest_at - m->previous_at > 150000000 || now - m->latest_at > 150000000 ||
        from.x != m->latest.x || from.y != m->latest.y || from.w != m->latest.w || from.h != m->latest.h)
        return false;
    int dx = to.x - from.x, dy = to.y - from.y;
    int px = from.x - m->previous.x, py = from.y - m->previous.y;
    if (eye_abs(dx) > from.w / 2 || eye_abs(dy) > from.h / 2 ||
        eye_abs(px) > from.w / 2 || eye_abs(py) > from.h / 2 ||
        eye_abs(to.w - from.w) > from.w / 5 || eye_abs(to.h - from.h) > from.h / 5 ||
        eye_abs(from.w - m->previous.w) > from.w / 5 || eye_abs(from.h - m->previous.h) > from.h / 5)
        return false;
    int before = (int)((m->latest_at - m->previous_at) / 1000000);
    int after = (int)((now - m->latest_at) / 1000000);
    return before > 0 && after > 0 && after <= 2 * before && before <= 2 * after &&
           eye_abs(dx * before - px * after) <= (from.w / 12) * before &&
           eye_abs(dy * before - py * after) <= (from.h / 12) * before;
}

static inline int eye_limit(int value, int low, int high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

#ifdef NXL_EYE_YUNET
static inline void eye_align_box(struct eye_box face, unsigned side, struct eye_box *target)
{
    /* Calibrated against upright eye-centre annotations, in face coordinates. */
    const int outward = (face.w + 16) / 32;
    const int upward = (face.h + 25) / 50;
    int x = target->x + (side == 0 ? -outward : outward);
    target->x = eye_limit(x, face.x, face.x + face.w - target->w);
    target->y = eye_limit(target->y - upward, face.y, face.y + face.h - target->h);
}

static inline void eye_minimum_focus_box(struct eye_box face, struct eye_box *target)
{
    /* The 720 x 480 main preview draws at least 24 pixels on either axis.
     * In the 528 x 352 AF grid, 18 is the first side that projects to 24
     * with the stock renderer's even-pixel rounding. */
    enum { MIN_SIDE = 18, SMALLEST_FACE = 32, FACE_STEP = 8 };
    /* Increase the floor with the face so mid-size faces do not all draw
     * the same 24-pixel window. The caller admits face sides of 32 to 160. */
    const int width = eye_limit(MIN_SIDE + (face.w - SMALLEST_FACE) / FACE_STEP, target->w, face.w);
    const int height = eye_limit(MIN_SIDE + (face.h - SMALLEST_FACE) / FACE_STEP, target->h, face.h);
    target->x = eye_limit(target->x - (width - target->w) / 2, face.x, face.x + face.w - width);
    target->y = eye_limit(target->y - (height - target->h) / 2, face.y, face.y + face.h - height);
    target->w = width;
    target->h = height;
}

static inline bool eye_filter_box_jitter(struct eye_box face, struct eye_box previous, struct eye_box *target)
{
    enum { DEAD_BAND = 2 };
    const int dw = target->w - previous.w, dh = target->h - previous.h;
    const int dx = target->x - previous.x, dy = target->y - previous.y;
    if (!(dx || dy || dw || dh) || eye_abs(dx + dw / 2) > DEAD_BAND ||
        eye_abs(dy + dh / 2) > DEAD_BAND || eye_abs(dw) > DEAD_BAND ||
        eye_abs(dh) > DEAD_BAND || !eye_in_face(previous, face))
        return false;
    *target = previous;
    return true;
}
#endif

static inline void eye_confirm(struct eye_track *state, struct eye_box face,
                               struct eye_box target, unsigned side, uint64_t now)
{
    state->target = target;
    state->face = face;
    state->last_face = face;
    state->candidate_valid = false;
    state->side = side;
    /* Keep a confirmed eye through the next budgeted sample on the same face. */
    state->confirmed_at = now;
    state->expires = now + (state->reduced_rate ? EYE_SLOW_SAMPLE_NS + EYE_SLOW_GRACE_NS : EYE_HOLD_NS);
    state->valid = true;
}

static inline bool eye_choose(struct eye_track *state, struct eye_box face,
                              const struct eye_box eyes[2], uint64_t now)
{
    const bool valid[2] = {eye_in_face(eyes[0], face), eye_in_face(eyes[1], face)};
    if (!valid[0] && !valid[1])
        return false;

    /* Retain the previously selected eye while its detection is valid. */
    unsigned side = state->side;
    if (!state->valid || !valid[side])
        side = valid[0] ? 0 : 1;
    const struct eye_box eye = eyes[side];
    const int w = eye_limit(eye.w * 2, face.w / 4, face.w / 2);
    const int h = eye_limit(eye.h * 2, face.h / 5, face.h / 2);
    const struct eye_box target = {
        eye_limit(eye.x + (eye.w - w) / 2, face.x, face.x + face.w - w),
        eye_limit(eye.y + (eye.h - h) / 2, face.y, face.y + face.h - h),
        w, h
    };
    eye_confirm(state, face, target, side, now);
    return true;
}

static inline bool eye_box_at_point(struct eye_box face, int x, int y, struct eye_box *box)
{
    if (x < face.x || x >= face.x + face.w || y < face.y || y >= face.y + face.h)
        return false;
    /* Odd sides put the measured landmark exactly at the centre. */
#ifdef NXL_EYE_YUNET
    enum { RADIUS_DIVISOR = 10 };
#else
    enum { RADIUS_DIVISOR = 16 };
#endif
    int rx = eye_limit(face.w / RADIUS_DIVISOR, 0, x - face.x);
    int ry = eye_limit(face.h / RADIUS_DIVISOR, 0, y - face.y);
    rx = eye_limit(rx, 0, face.x + face.w - 1 - x);
    ry = eye_limit(ry, 0, face.y + face.h - 1 - y);
    if (rx < 2 || ry < 2)
        return false;
    *box = (struct eye_box){x - rx, y - ry, 2 * rx + 1, 2 * ry + 1};
    return true;
}

static inline bool eye_choose_points(struct eye_track *state, struct eye_box face,
                                     const struct eye_point eyes[2], uint64_t now)
{
    /* Point zero is the left eye in the upright image. Try it on every detection. */
    for (unsigned side = 0; side < 2; ++side) {
        struct eye_box target;
        if (!eyes[side].valid || !eye_box_at_point(face, eyes[side].x, eyes[side].y, &target))
            continue;
        eye_confirm(state, face, target, side, now);
        return true;
    }
    return false;
}

static inline int eye_scale(int value, int original_size, int current_size)
{
    return (value * current_size + original_size / 2) / original_size;
}

#ifdef NXL_EYE_POINT_BACKEND
static inline bool eye_cached_target(struct eye_track *state, struct eye_box face,
                                     uint64_t now, struct eye_box *target)
{
    if (!state->valid || now >= state->expires) {
        state->valid = false;
        return false;
    }
    const struct eye_box anchor = state->face, eye = state->target;
    const int x = face.x + eye_scale(eye.x + eye.w / 2 - anchor.x, anchor.w, face.w);
    const int y = face.y + eye_scale(eye.y + eye.h / 2 - anchor.y, anchor.h, face.h);
    state->last_face = face;
    return eye_box_at_point(face, x, y, target);
}
#else
static inline bool eye_cached_target(struct eye_track *state, struct eye_box face,
                                     uint64_t now, struct eye_box *target)
{
    /* The caller has already discarded discontinuities before trying a fresh eye. */
    if (!state->valid || now >= state->expires) {
        state->valid = false;
        return false;
    }
    const struct eye_box anchor = state->face, eye = state->target;
    target->w = eye_limit(eye_scale(eye.w, anchor.w, face.w), face.w / 4, face.w / 2);
    target->h = eye_limit(eye_scale(eye.h, anchor.h, face.h), face.h / 5, face.h / 2);
    target->x = face.x + eye_limit(eye_scale(eye.x - anchor.x, anchor.w, face.w), 0, face.w - target->w);
    target->y = face.y + eye_limit(eye_scale(eye.y - anchor.y, anchor.h, face.h), 0, face.h - target->h);
    state->last_face = face;
    return true;
}
#endif

#endif
