#include "eye_replay.c"
#include "../src/eye_yunet.c"

uint8_t preview_pixels[FD_WIDTH * FD_HEIGHT * 3 / 2];
struct eye_point found_points[2];
int8_t head_bytes[21 * (1 + 1 + 4 + 10)];

int verify_points(int angle)
{
    return eye_yunet_landmarks(preview_pixels, FD_WIDTH, FD_HEIGHT, ordinary, angle, found_points) ? 0 : 1;
}

int verify_heads(void)
{
    const int8_t *out[12];
    yunet_forward(workspace.patch, workspace.arena, out);
    int8_t *p = head_bytes;
    for (int head = 0; head < 12; ++head) {
        const int grid = 4 >> (head % 3);
        const int channels[] = {1, 1, 4, 10};
        const size_t bytes = (size_t)grid * grid * channels[head / 3];
        memcpy(p, out[head], bytes);
        p += bytes;
    }
    return 0;
}

static void advance(unsigned ms)
{
    replay_wall_reads = 0;
    time_ns += ms * 1000000ULL;
    nxl_stock_face(object, preview_pixels, FD_WIDTH, FD_HEIGHT, 0, 0);
    set_count(FD_FRAME, read_value(data, FD_FRAME) + 1);
}

int verify_yunet(void)
{
    if (verify_points(0) || !found_points[0].valid)
        return 1;
    unsigned char *result = data;
    memcpy(object + FD_RESULT_PTR, &result, sizeof(result));
    memcpy(data, &ordinary, sizeof(ordinary));
    advance(1000);
    if (ui_get || !original_face() || detect_calls)
        return 2;
    resume_token = 1;
    advance(1000);
    if (ui_get || detect_calls || !original_face())
        return 3;
    resume_token = 2;
    af_mode = 2;
    native_eye = 1;
    advance(1000);
    if (!ui_get || stock_eye_queries || detect_calls != 1 || native_hits || !track.valid || track.side != 0)
        return 4;
    struct eye_box expected;
    if (!eye_box_at_point(ordinary, found_points[0].x, found_points[0].y, &expected) ||
        memcmp(&track.target, &expected, sizeof(expected)))
        return 5;
    eye_align_box(ordinary, 0, &expected);
    eye_minimum_focus_box(ordinary, &expected);
    if (memcmp(data, &expected, sizeof(expected)))
        return 11;
    next_report = 0;
    for (int i = 0; i < 15; ++i)
        advance(33);
    if (damaged_history || !track.valid || detect_calls != 2 || face_returns)
        return 6;
    if (last_publication.side != 1 || memcmp(&last_publication.face, &ordinary, sizeof(ordinary)) ||
        memcmp(&last_publication.target, &expected, sizeof(expected)) || left_measurements != 2 || right_measurements)
        return 9;
    next_report = 0;
    face_count = 0;
    advance(33);
    if (!original_face() || track.valid || no_face_frames != 1 || last_publication.side || last_publication.target.w)
        return 7;
    next_report = 0;
    face_count = 1;
    memset(preview_pixels, 128, sizeof(preview_pixels));
    advance(1201);
    if (stock_eye_queries || track.valid || !original_face() || native_hits || !probe_misses)
        return 8;
    if (last_publication.side || last_publication.target.w ||
        memcmp(&last_publication.face, &ordinary, sizeof(ordinary)))
        return 10;
    return 0;
}

static void enable_fixture(void)
{
    unsigned char *result = data;
    memcpy(object + FD_RESULT_PTR, &result, sizeof(result));
    resume_token = 2;
    af_mode = 2;
    advance(1000);
}

/* Use a positive crop from the paired-camera fixture study. */
int verify_multiface(void)
{
    struct eye_box expected;
    if (verify_points(0) || !found_points[0].valid ||
        !eye_box_at_point(ordinary, found_points[0].x, found_points[0].y, &expected))
        return 1;
    eye_align_box(ordinary, 0, &expected);
    eye_minimum_focus_box(ordinary, &expected);

    /* The stock primary is slot 1, and only its angle is usable. */
    secondary = ordinary;
    ordinary.x += ordinary.x >= FD_WIDTH / 2 ? -144 : 144;
    ordinary_angle = 181;
    face_count = 2;
    stock_main_face = 1;
    enable_fixture();
    if (!ui_get || stock_eye_queries || detect_calls != 1 || !track.valid ||
        !original_face() || memcmp(data + FD_FACE_STRIDE, &expected, sizeof(expected)) ||
        read_value(data, FD_MAIN) != 1 || read_value(data, FD_COUNT) != 2 ||
        read_value(data, FD_FACE_STRIDE + sizeof(secondary)) != stock_face_word)
        return 2;
    if (memcmp(object + FD_HISTORY + FD_FACE_STRIDE, &secondary, sizeof(secondary)) ||
        memcmp(data + FD_METER_Y, stock_meters, sizeof(stock_meters)))
        return 3;

    next_report = 0;
    advance(33);
    if (damaged_history || !original_face() || detect_calls != 1 || multi_face_frames != 2 ||
        multi_eye_frames != 2 || last_publication.main_face != 1 || last_publication.faces != 2 ||
        memcmp(data + FD_FACE_STRIDE, &expected, sizeof(expected)))
        return 4;

    /* Even overlapping boxes must not pass a cached eye between slots. */
    ordinary = secondary;
    ordinary.x++;
    ordinary_angle = 0;
    stock_main_face = 0;
    advance(33);
    if (damaged_history || track.valid || !original_face() ||
        memcmp(data + FD_FACE_STRIDE, &secondary, sizeof(secondary)) ||
        detect_calls != 1 || main_changes != 1)
        return 5;
    advance(267);
    if (!track.valid || original_face() || detect_calls != 2 ||
        memcmp(data + FD_FACE_STRIDE, &secondary, sizeof(secondary)))
        return 6;

    stock_main_face = 2;
    advance(200);
    if (damaged_history || track.valid || !original_face() || detect_calls != 2 || pending.result)
        return 7;
    stock_main_face = 0;
    face_count = FD_FACE_LIMIT + 1;
    advance(200);
    if (!original_face() || detect_calls != 2 || pending.result)
        return 8;
    return 0;
}

int verify_rate(void)
{
    enable_fixture();
    if (detect_calls != 1 || track.reduced_rate)
        return 1;
    replay_wall_us = 18800;
    advance(420);
    if (detect_calls != 2 || track.reduced_rate || peak_us != 18800)
        return 2;

    replay_wall_us = 21000;
    advance(420);
    if (detect_calls != 3 || track.reduced_rate)
        return 3;
    replay_wall_us = 20000;
    advance(420);
    replay_wall_us = 21000;
    advance(420);
    advance(420);
    if (detect_calls != 6 || track.reduced_rate)
        return 4;
    advance(420);
    if (detect_calls != 7 || !track.reduced_rate || reduced_entries != 1)
        return 5;

    replay_wall_us = 6000;
    advance(900);
    if (detect_calls != 7)
        return 6;
    advance(100);
    advance(1000);
    if (detect_calls != 9 || !track.reduced_rate)
        return 7;
    advance(1000);
    if (detect_calls != 10 || track.reduced_rate || reduced_exits != 1)
        return 8;
    advance(420);
    if (detect_calls != 11 || damaged_history)
        return 9;
    return 0;
}

int verify_priority(void)
{
    const struct eye_box face = {100, 100, 96, 108};
    struct eye_track state = {0};
    struct eye_point points[2] = {{128, 128, false}, {166, 130, true}};
    if (!eye_choose_points(&state, face, points, 1) || state.side != 1)
        return 1;
    points[0].valid = true;
    if (!eye_choose_points(&state, face, points, 2) || state.side != 0 ||
        state.target.w != 19 || state.target.h != 21 ||
        state.target.x + state.target.w / 2 != 128 || state.target.y + state.target.h / 2 != 128)
        return 2;
    points[0].x = 104;
    if (!eye_choose_points(&state, face, points, 3) || state.side != 0 || state.target.w != 9)
        return 3;
    points[0].x = face.x;
    if (!eye_choose_points(&state, face, points, 4) || state.side != 1)
        return 4;
    struct eye_box expected;
    const struct eye_box moved = {104, 106, 108, 120};
    if (!eye_cached_target(&state, moved, 5, &expected) || expected.w != 21 || expected.h != 25)
        return 5;
    return 0;
}

int verify_focus_geometry(void)
{
    const struct eye_box face = {100, 100, 96, 108};
    const struct eye_box raw = {120, 130, 17, 19};
    struct eye_box tracked = raw;
    eye_align_box(face, 0, &tracked);
    if (tracked.x != 117 || tracked.y != 128 || tracked.w != raw.w || tracked.h != raw.h)
        return 1;
    tracked = raw;
    eye_align_box(face, 1, &tracked);
    if (tracked.x != 123 || tracked.y != 128)
        return 2;
    tracked = (struct eye_box){101, 101, 17, 19};
    eye_align_box(face, 0, &tracked);
    if (tracked.x != 100 || tracked.y != 100)
        return 3;

    for (int i = -2; i <= 2; ++i) {
        if (!i)
            continue;
        tracked = (struct eye_box){raw.x + i, raw.y - i, raw.w, raw.h};
        if (!eye_filter_box_jitter(face, raw, &tracked) || memcmp(&tracked, &raw, sizeof(raw)))
            return 4;
    }
    tracked = (struct eye_box){raw.x - 1, raw.y - 1, raw.w + 2, raw.h + 2};
    if (!eye_filter_box_jitter(face, raw, &tracked) || memcmp(&tracked, &raw, sizeof(raw)))
        return 5;
    tracked = (struct eye_box){raw.x + 3, raw.y, raw.w, raw.h};
    if (eye_filter_box_jitter(face, raw, &tracked) || tracked.x != raw.x + 3)
        return 6;
    struct eye_box moved = face;
    moved.x = raw.x + 1;
    tracked = (struct eye_box){raw.x + 1, raw.y, raw.w, raw.h};
    if (eye_filter_box_jitter(moved, raw, &tracked))
        return 7;

    const struct eye_box small = {272, 110, 54, 54};
    const struct eye_box small_eye = {283, 122, 9, 9};
    const struct eye_box expanded = {278, 117, 20, 20};
    tracked = small_eye;
    eye_minimum_focus_box(small, &tracked);
    if (memcmp(&tracked, &expanded, sizeof(tracked)))
        return 8;
    tracked = (struct eye_box){small.x, small.y + small.h - 7, 9, 7};
    eye_minimum_focus_box(small, &tracked);
    if (tracked.x != small.x || tracked.y != small.y + small.h - 20 || tracked.w != 20 || tracked.h != 20)
        return 9;
    const struct eye_box larger = {120, 130, 30, 32};
    tracked = larger;
    eye_minimum_focus_box(face, &tracked);
    if (memcmp(&tracked, &larger, sizeof(tracked)))
        return 10;
    tracked = (struct eye_box){expanded.x + 1, expanded.y + 2, 20, 20};
    if (!eye_filter_box_jitter(small, expanded, &tracked) || memcmp(&tracked, &expanded, sizeof(tracked)))
        return 11;
    const int face_sizes[] = {32, 54, 90, 120, 144, 160};
    const int expected[] = {18, 20, 25, 29, 32, 34};
    for (unsigned i = 0; i < sizeof(face_sizes) / sizeof(face_sizes[0]); ++i) {
        struct eye_box zoomed = {100, 100, face_sizes[i], face_sizes[i]};
        tracked = (struct eye_box){108, 109, 7, 9};
        eye_minimum_focus_box(zoomed, &tracked);
        if (tracked.w != expected[i] || tracked.h != expected[i] || !eye_in_face(tracked, zoomed))
            return 12;
    }
    return 0;
}

int verify_focus_publication(void)
{
    if (verify_points(0) || !found_points[0].valid)
        return 1;
    enable_fixture();
    if (!track.valid || !pending.result)
        return 2;
    const struct eye_box first = pending.published;
    ordinary.x++;
    advance(33);
    ordinary.x++;
    advance(33);
    if (memcmp(data, &first, sizeof(first)) || roi_jitter_frames != 2 || damaged_history)
        return 3;
    ordinary.x++;
    advance(33);
    if (pending.published.x != first.x + 3 || roi_jitter_frames != 2 || damaged_history)
        return 4;
    face_count = 0;
    advance(33);
    if (pending.result || track.published_eye || !original_face())
        return 5;
    return 0;
}
