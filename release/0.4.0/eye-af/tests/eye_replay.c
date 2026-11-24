#include "../src/eye-af.c"

/* The real ARM adapter runs here with deterministic camera and clock endpoints. */
static _Alignas(unsigned) unsigned char data[0x494];
static _Alignas(unsigned) unsigned char object[0x4c0];
static unsigned char image;
static uint64_t time_ns;
static int face_mode = 1, af_mode = 0, face_count = 1, has_eye = 1, native_eye = 0;
static unsigned elapsed_us = 2300;
static int damaged_history;
struct eye_box published_result;
static const struct eye_box ordinary = {120, 94, 92, 102};
static const struct eye_box found = {140, 120, 16, 12};

void *memcpy(void *out, const void *in, size_t bytes)
{
    unsigned char *to = out;
    const unsigned char *from = in;
    typedef unsigned word __attribute__((may_alias));
    if ((((uintptr_t)to | (uintptr_t)from) & 3u) == 0) {
        while (bytes >= 16) {
            for (unsigned i = 0; i < 4; ++i)
                ((word *)to)[i] = ((const word *)from)[i];
            bytes -= 16;
            to += 16;
            from += 16;
        }
    }
    for (size_t i = 0; i < bytes; i++)
        to[i] = from[i];
    return out;
}

void *memset(void *out, int c, size_t bytes)
{
    unsigned char *to = out;
    for (size_t i = 0; i < bytes; i++)
        to[i] = c;
    return out;
}

int memcmp(const void *a, const void *b, size_t bytes)
{
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < bytes; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

int clock_gettime(clockid_t id, struct timespec *time)
{
    (void)id;
    time->tv_sec = time_ns / 1000000000ULL;
    time->tv_nsec = time_ns % 1000000000ULL;
    return 0;
}

static int resume_token, ui_calls;
static int camera_face(void *, unsigned char *, int, int, int, int);
static int camera_eye(const unsigned char *, int, int, void *);
static int camera_setting(int);

void *dlsym(void *handle, const char *name)
{
    if (handle == RTLD_NEXT && !strcmp(name, face_symbol)) return (void *)camera_face;
    if (!resume_token) return NULL;
    if (!strcmp(name, "SDIC_EyeDetection")) return (void *)camera_eye;
    if (!strcmp(name, "_Z12UI_Get_Value14eUI_VALUE_ITEM")) return (void *)camera_setting;
    return NULL;
}
int dladdr(const void *addr, Dl_info *info)
{
    if (addr == (void *)camera_face) info->dli_fname = "/usr/lib/libcapture-fw-prod.so";
    else if (addr == (void *)camera_eye) info->dli_fname = "/usr/lib/libface.so";
    else if (addr == (void *)camera_setting) info->dli_fname = app_path;
    else return 0;
    return 1;
}
ssize_t readlink(const char *path, char *out, size_t n)
{
    if (strcmp(path, "/proc/self/exe") || n < sizeof(app_path) - 1) return -1;
    memcpy(out, app_path, sizeof(app_path) - 1);
    return sizeof(app_path) - 1;
}
int snprintf(char *out, size_t n, const char *fmt, ...)
{
    if (strcmp(fmt, "%ld\n") || n < 4) return 0;
    memcpy(out, "37\n", 4);
    return 3;
}
int open(const char *path, int flags, ...)
{
    (void)flags;
    return resume_token && !strcmp(path, "/tmp/nx-lut-lite/eye-af.resume-ready") ? 10 : -1;
}
ssize_t read(int fd, void *out, size_t n)
{
    if (fd != 10 || n < 3) return -1;
    memcpy(out, resume_token == 1 ? "38\n" : "37\n", 3);
    return 3;
}
ssize_t write(int fd, const void *buf, size_t n) { (void)fd; (void)buf; (void)n; return -1; }
int close(int fd) { (void)fd; return 0; }
pid_t getpid(void) { return 37; }

static void set_count(unsigned offset, unsigned value)
{
    memcpy(data + offset, &value, sizeof(value));
}

static int camera_face(void *self, unsigned char *video, int w, int h, int mode, int orientation)
{
    unsigned char *out = face_result(self); (void)video; (void)w; (void)h; (void)mode; (void)orientation;
    if (memcmp(out, &ordinary, sizeof(ordinary)))
        damaged_history = 1;
    memcpy(out, &ordinary, sizeof(ordinary));
    memcpy(out + FD_COUNT, &face_count, sizeof(face_count));
    out[FD_VALID] = 1;
    if (native_eye) {
        memcpy(out + 0xc8, &found, sizeof(found));
        out[0xd8] = 1;
    }
    return 0;
}

static int camera_eye(const unsigned char *video, int w, int h, void *sample)
{
    (void)video;
    unsigned char *res = sample;
    if (res == data || read_value(res, FD_COUNT) != 1 || w != 528 || h != 352)
        return -1;
    time_ns += elapsed_us * 1000ULL;
    if (has_eye) {
        memcpy(res + 0xc8, &found, sizeof(found));
        res[0xd8] = 1;
    }
    return 1;
}

static int camera_setting(int item)
{
    ui_calls++;
    return item == UI_FACE_DETECTION ? face_mode : af_mode;
}

static void frame(unsigned ms)
{
    time_ns += ms * 1000000ULL;
    nxl_stock_face(object, &image, 528, 352, 0, 0);
    set_count(FD_FRAME, read_value(data, FD_FRAME) + 1);
}

static int original_face(void)
{
    return !memcmp(data, &ordinary, sizeof(ordinary));
}

int verify(void)
{
    unsigned char *result = data;
    memcpy(object + FD_RESULT_PTR, &result, sizeof(result));
    memcpy(data, &ordinary, sizeof(ordinary));
    frame(1000);
    if (stock_face != camera_face || stock_eye || ui_calls || detect_calls || !original_face()) return 11;
    resume_token = 1;
    frame(1000);
    if (stock_eye || ui_calls || detect_calls || !original_face()) return 12;
    resume_token = 2;
    face_mode = 0;
    frame(1000);
    if (stock_eye != camera_eye || !ui_calls || detect_calls || !original_face()) return 13;

    face_mode = 0;
    frame(1000);
    if (detect_calls || !original_face()) return 1;
    face_mode = 1;
    af_mode = 3;
    frame(20);
    if (detect_calls || !original_face()) return 2;
    af_mode = 0;
    data[FD_WINK] = 1;
    frame(20);
    if (detect_calls || !original_face()) return 3;
    data[FD_WINK] = 0;
    face_count = 2;
    frame(20);
    if (detect_calls || !original_face()) return 4;
    face_count = 1;
    native_eye = 1;
    frame(20);
    if (detect_calls || native_hits != 1 || original_face()) return 5;
    memcpy(&published_result, data, sizeof(published_result));
    for (int i = 0; i < 6; i++)
        frame(25);
    if (detect_calls || original_face() || damaged_history) return 6;
    native_eye = 0;
    elapsed_us = 19000;
    frame(470);
    if (detect_calls != 1 || !track.reduced_rate || peak_us != elapsed_us || original_face()) return 7;
    has_eye = 0;
    frame(620);
    if (detect_calls != 1 || original_face()) return 8;
    frame(420);
    if (detect_calls != 2 || original_face()) return 9;
    frame(440);
    if (detect_calls != 2 || !original_face() || eye_timeouts != 1) return 14;
    face_mode = 0;
    frame(1020);
    if (detect_calls != 2 || !original_face() || damaged_history) return 10;
    face_mode = 1;
    has_eye = 1;
    elapsed_us = 16399;
    unsigned starting_calls = detect_calls, starting_eyes = eye_frames, starting_faces = single_face_frames;
    /* Model 3 seconds of fixed-cadence single-face callbacks at the measured cost. */
    uint64_t base = time_ns + 2000000000ULL;
    for (unsigned i = 0; i < 90; ++i) {
        time_ns = base + (uint64_t)i * 1000000000ULL / 30;
        frame(0);
        if (original_face()) return 15;
    }
    if (detect_calls - starting_calls != 3 || eye_frames - starting_eyes != 90 ||
        single_face_frames - starting_faces != 90 || damaged_history) return 16;
    face_count = 2;
    frame(33);
    if (track.valid || !original_face()) return 17;
    face_count = 1;
    has_eye = 0;
    frame(33);
    if (!original_face()) return 18;
    return 0;
}

#define TEST_REACQUISITION_COUNTERS 1
#define TEST_MOTION_COUNTERS 1
#define TEST_GAP_COUNTERS 1
#include "dropout-replay.inc"
