/* Camera and clock endpoints for the production adapter; one bit field per callback. */
enum dropout_case {
    STABLE, MISSED_EYE, LOST_FACE, EYES_STAY_HIDDEN, MULTIPLE_FACES, FACE_LEAVES,
    REPEATED_GAPS, RETURN_WITHOUT_EYE, FACE_MODE_OFF, TWO_MISSES, FAST_DETECTOR,
    LATE_233_MS, LATE_333_MS, EXPIRES_DURING_PROBE, FACE_MOVES,
    BLIP_18, BLIP_19, BLIP_WIDTH, FLAG_BLIP, ONE_MULTI, JUMP_WITH_EYE,
    BLIP_THEN_HIDDEN, SMOOTH_PAN, SMOOTH_ZOOM, FAST_PAN, NATIVE_EYE_MOVES,
    REPEATED_JUMPS, PRIMARY_BLIP, SIZE_BLIP, STOCK_ERROR, STALE_EYE_COORDS,
    MOTION_AFTER_LOSS, MOTION_WITH_HIDDEN_EYES, NATIVE_ON_JUMP, JUMP_THEN_MISS, OTHER_EYE,
    DENSE_ZEROS, DOUBLE_ZERO, LONG_ABSENCE, GAP_THEN_MULTIPLE, GAP_THEN_OTHER_HIDDEN,
    GAP_MISS_NEAR_SAMPLE, NATIVE_ON_RETURN, RECENT_PROBE_GAP,
    ZERO_AFTER_FRAME_CHANGE, ZERO_AFTER_STOCK_BOX, ZERO_AFTER_BUFFER_CHANGE,
    AAF_EYE_RETURNS, AAF_EYE_RETURNS_LATE, AAF_EYE_STAYS_HIDDEN, AAF_SUBJECT_CHANGES,
    AAF_MULTIPLE_AFTER_EXPIRY, AAF_LOST_AFTER_EXPIRY, AAF_NEVER_SAW_EYE, AAF_REAL_EYE_AT_EXPIRY,
    AAF_PEAK_EYE_RETURNS, AAF_PEAK_EYE_HIDDEN
};
unsigned dropout_frames[90], dropout_counters[5];
unsigned dropout_errors[90], dropout_geometry[90][4], dropout_losses[20];
unsigned dropout_gap_counters[13];
unsigned dropout_relock_counters[2];
static int face_shift, vertical_shift, width_shift, height_shift, eye_offset;
static int invalid_flag, main_face, failed_result, stale_eye, scripted_native, right_eye;
static struct eye_box last_stock_box;
static struct eye_box expected_input_box;
static int check_input_box;

static int scale_fixture(int n, int original, int current)
{
    return (n * current + original / 2) / original;
}

static struct eye_box expected_eye(void)
{
    struct eye_box face = last_stock_box;
    return (struct eye_box){face.x + scale_fixture(found.x - ordinary.x, ordinary.w, face.w) + eye_offset,
                           face.y + scale_fixture(found.y - ordinary.y, ordinary.h, face.h),
                           scale_fixture(found.w, ordinary.w, face.w), scale_fixture(found.h, ordinary.h, face.h)};
}

static int scripted_face(void *self, unsigned char *video, int w, int h, int mode, int orientation)
{
    unsigned char *out = face_result(self);
    if (check_input_box) {
        if (memcmp(out, &expected_input_box, sizeof(expected_input_box)) != 0)
            damaged_history = 1;
        memcpy(out, &ordinary, sizeof(ordinary));
        check_input_box = 0;
    }
    if (!memcmp(out, &last_stock_box, sizeof(last_stock_box)))
        memcpy(out, &ordinary, sizeof(ordinary));
    int rc = camera_face(self, video, w, h, mode, orientation);
    last_stock_box = ordinary;
    last_stock_box.x += face_shift;
    last_stock_box.y += vertical_shift;
    last_stock_box.w += width_shift;
    last_stock_box.h += height_shift;
    memcpy(out, &last_stock_box, sizeof(last_stock_box));
    out[FD_VALID] = !invalid_flag;
    memcpy(out + FD_MAIN, &main_face, sizeof(main_face));
    if (scripted_native) {
        struct eye_box eye = expected_eye();
        memcpy(out + (right_eye ? 0x190 : 0xc8), &eye, sizeof(eye));
        out[0xd8] = !right_eye;
        out[0x1a0] = right_eye;
    }
    return failed_result ? -1 : rc;
}

static int scripted_eye(const unsigned char *video, int w, int h, void *sample)
{
    int rc = camera_eye(video, w, h, sample);
    if (rc > 0 && has_eye) {
        struct eye_box eye = stale_eye ? found : expected_eye();
        unsigned char *result = sample;
        memcpy(result + (right_eye ? 0x190 : 0xc8), &eye, sizeof(eye));
        result[0xd8] = !right_eye;
        result[0x1a0] = right_eye;
    }
    return rc;
}

int study_dropout(unsigned scenario)
{
    unsigned char *result = data;
    memcpy(object + FD_RESULT_PTR, &result, sizeof(result));
    memcpy(data, &ordinary, sizeof(ordinary));
    stock_face = scripted_face;
    stock_eye = scripted_eye;
    ui_get = camera_setting;
    if (scenario >= AAF_EYE_RETURNS)
        af_mode = 2;
    elapsed_us = scenario == FAST_DETECTOR ? 8000 : (scenario >= DENSE_ZEROS ? 25600 : 30400);
    if (scenario == AAF_PEAK_EYE_RETURNS || scenario == AAF_PEAK_EYE_HIDDEN)
        elapsed_us = 88400;
    for (unsigned i = 0; i < 90; ++i) {
        if ((scenario == LATE_233_MS && i > 30 && i < 37) ||
            (scenario == LATE_333_MS && i > 30 && i < 40) ||
            (scenario == EXPIRES_DURING_PROBE && i > 30 && i < 38))
            continue;
        uint64_t tick = 3000000000ULL + (uint64_t)i * 1000000000ULL / 30;
        if (elapsed_us == 88400 && tick < time_ns)
            continue;
        time_ns = tick;
        face_count = 1;
        if ((scenario == LOST_FACE || scenario == RETURN_WITHOUT_EYE) && i == 2)
            face_count = 0;
        if (scenario == REPEATED_GAPS && (i == 2 || i == 6 || i == 36 || i == 40 || i == 66 || i == 70))
            face_count = 0;
        if (scenario == MULTIPLE_FACES && i >= 20 && i < 40)
            face_count = 2;
        if (scenario == FACE_LEAVES && i >= 20)
            face_count = 0;
        face_mode = !(scenario == FACE_MODE_OFF && i == 2);
        has_eye = !((scenario == MISSED_EYE || scenario == LATE_233_MS || scenario == LATE_333_MS) && i == 30);
        if ((scenario == EYES_STAY_HIDDEN || scenario == EXPIRES_DURING_PROBE) && i >= 30)
            has_eye = 0;
        if (scenario == RETURN_WITHOUT_EYE && i >= 2 && i < 44)
            has_eye = 0;
        if (scenario == TWO_MISSES && (i == 30 || i == 33))
            has_eye = 0;
        if (scenario == FACE_MOVES && i >= 12) {
            face_shift = 40;
            has_eye = 0;
        }
        if (scenario == BLIP_18 || scenario == BLIP_19 || scenario == BLIP_THEN_HIDDEN)
            face_shift = i == 12 ? (scenario == BLIP_18 ? 18 : 19) : 0;
        if (scenario == BLIP_WIDTH) width_shift = i == 12 ? 19 : 0;
        if (scenario == BLIP_THEN_HIDDEN && i >= 12) has_eye = 0;
        if (scenario == FLAG_BLIP) invalid_flag = i == 12;
        if (scenario == ONE_MULTI && i == 12) face_count = 2;
        if (scenario == JUMP_WITH_EYE || scenario == NATIVE_ON_JUMP || scenario == JUMP_THEN_MISS)
            face_shift = i >= 12 ? 70 : 0;
        if (scenario == NATIVE_ON_JUMP || scenario == JUMP_THEN_MISS) scripted_native = i == 12;
        if (scenario == JUMP_THEN_MISS && i == 42) has_eye = 0;
        if (scenario == OTHER_EYE) {
            scripted_native = 1;
            right_eye = i >= 25 && i < 60;
            eye_offset = right_eye ? 38 : 0;
        }
        if (scenario == SMOOTH_PAN || scenario == MOTION_WITH_HIDDEN_EYES) {
            int step = i <= 44 ? (int)i : 89 - (int)i;
            face_shift = 3 * step;
            vertical_shift = step / 3;
            if (scenario == MOTION_WITH_HIDDEN_EYES && i >= 15) has_eye = 0;
        }
        if (scenario == SMOOTH_ZOOM) {
            width_shift = i <= 44 ? (int)i : 89 - (int)i;
            height_shift = width_shift;
            face_shift = -width_shift / 2;
            vertical_shift = -height_shift / 2;
        }
        if (scenario == FAST_PAN && i >= 6 && i < 16) {
            int step = (int)(i - 5) * 6;
            face_shift += step < 24 ? step : 24;
        }
        if (scenario == NATIVE_EYE_MOVES) {
            scripted_native = 1;
            eye_offset = (int)(i <= 44 ? i : 89 - i) / 2;
        }
        if (scenario == REPEATED_JUMPS)
            face_shift = ((i >= 12 && i < 20) || (i >= 70 && i < 82)) ? 72 : 0;
        if (scenario == PRIMARY_BLIP) main_face = i == 12;
        if (scenario == SIZE_BLIP) width_shift = i == 12 ? 72 : 0;
        if (scenario == STOCK_ERROR) failed_result = i == 12;
        if (scenario == STALE_EYE_COORDS && i >= 12) { face_shift = 105; stale_eye = 1; }
        if (scenario == MOTION_AFTER_LOSS && i >= 12) {
            face_count = i == 12 ? 0 : 1;
            face_shift = 75;
        }
        if (scenario == DENSE_ZEROS && i >= 6 && i < 80 && i % 4 == 2) face_count = 0;
        if (scenario == DOUBLE_ZERO && (i == 12 || i == 13)) face_count = 0;
        if (scenario == LONG_ABSENCE && i >= 10 && i < 55) face_count = 0;
        if (scenario == GAP_THEN_MULTIPLE) {
            if (i == 12) face_count = 0;
            if (i == 13) face_count = 2;
        }
        if (scenario == GAP_THEN_OTHER_HIDDEN && i >= 12) {
            face_count = i == 12 ? 0 : 1;
            face_shift = 110;
            has_eye = 0;
        }
        if (scenario == GAP_MISS_NEAR_SAMPLE && i >= 27) {
            face_count = i == 27 ? 0 : 1;
            has_eye = 0;
        }
        if (scenario == NATIVE_ON_RETURN) {
            if (i == 12) face_count = 0;
            scripted_native = i == 13;
        }
        if (scenario == RECENT_PROBE_GAP && i == 1) face_count = 0;
        if (scenario >= ZERO_AFTER_FRAME_CHANGE && scenario <= ZERO_AFTER_BUFFER_CHANGE && i == 12) {
            face_count = 0;
            if (scenario == ZERO_AFTER_FRAME_CHANGE) {
                set_count(FD_FRAME, read_value(data, FD_FRAME) + 2);
            } else {
                struct eye_box external = ordinary;
                external.x += 3;
                memcpy(data, &external, sizeof(external));
                if (scenario == ZERO_AFTER_BUFFER_CHANGE)
                    pending.result = data + 64;
            }
            memcpy(&expected_input_box, data, sizeof(expected_input_box));
            check_input_box = 1;
        }
        if (scenario >= AAF_EYE_RETURNS) {
            has_eye = i < 30;
            if ((scenario == AAF_EYE_RETURNS || scenario == AAF_PEAK_EYE_RETURNS) && i >= 43) has_eye = 1;
            if (scenario == AAF_EYE_RETURNS_LATE && i >= 55) has_eye = 1;
            if (scenario == AAF_SUBJECT_CHANGES && i >= 41) face_shift = 76;
            if (scenario == AAF_MULTIPLE_AFTER_EXPIRY && i == 41) face_count = 2;
            if (scenario == AAF_LOST_AFTER_EXPIRY && i == 41) face_count = 0;
            if (scenario == AAF_NEVER_SAW_EYE) has_eye = 0;
            if (scenario == AAF_REAL_EYE_AT_EXPIRY) {
                scripted_native = i == 40;
                if (i >= 40) has_eye = 1;
            }
        }
        unsigned faces_before = single_face_frames, eyes_before = eye_frames;
        unsigned calls_before = detect_calls, expired_before = eye_timeouts;
        unsigned early_before = early_tries;
        frame(0);
        bool eye = eye_frames != eyes_before;
        if (eye != (memcmp(data, &last_stock_box, sizeof(last_stock_box)) != 0))
            return 1;
        if (eye) {
            struct eye_box roi, expected = expected_eye();
            memcpy(&roi, data, sizeof(roi));
            if (roi.x < last_stock_box.x || roi.y < last_stock_box.y ||
                roi.x + roi.w > last_stock_box.x + last_stock_box.w || roi.y + roi.h > last_stock_box.y + last_stock_box.h)
                return 2;
            int x = eye_abs(2 * (roi.x - expected.x) + roi.w - expected.w);
            int y = eye_abs(2 * (roi.y - expected.y) + roi.h - expected.h);
            dropout_errors[i] = (unsigned)((x > y ? x : y) + 1) / 2;
            memcpy(dropout_geometry[i], &roi, sizeof(roi));
        }
        dropout_frames[i] = 1u | ((single_face_frames != faces_before) << 1) | (eye << 2) |
                            ((detect_calls != calls_before) << 3) | ((eye_timeouts != expired_before) << 4) |
                            ((early_tries != early_before) << 5) | ((face_count == 0) << 6);
    }
#ifdef TEST_REACQUISITION_COUNTERS
    dropout_counters[0] = probe_misses;
    dropout_counters[1] = early_tries;
    dropout_counters[2] = early_hits;
    dropout_counters[3] = face_gaps;
    dropout_counters[4] = extended_hold_frames;
#endif
#ifdef TEST_GAP_COUNTERS
    const unsigned gap_events[] = {gap_returns, gap_first_hits, one_zero_returns, longest_zero_run, longest_return_ms,
                                   gap_probes, gap_hits, gap_budget_waits, gap_spacing_waits};
    memcpy(dropout_gap_counters, gap_events, sizeof(gap_events));
    memcpy(dropout_gap_counters + 9, gap_history, sizeof(gap_history));
#endif
#ifdef TEST_MOTION_COUNTERS
    memcpy(dropout_losses, wait_frames, sizeof(wait_frames));
    const unsigned motion_events[] = {jumps, invalid_flags, motion_frames, return_tries, return_hits,
                                      move_tries, move_hits, cooldown_frames, face_returns, gate_releases};
    memcpy(dropout_losses + LOSS_COUNT, motion_events, sizeof(motion_events));
#endif
#ifdef TEST_RELOCK_COUNTERS
    dropout_relock_counters[0] = relock_probes;
    dropout_relock_counters[1] = relock_hits;
#endif
    return damaged_history;
}
