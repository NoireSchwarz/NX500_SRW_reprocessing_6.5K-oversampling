/* SPDX-License-Identifier: AGPL-3.0-only */
#include "eye_yunet.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

enum { SIDE = 32, XR = 256, PATCH_BYTES = SIDE * SIDE * 3 };

struct quant_conv {
    const int8_t *weights;
    const int32_t *bias, *gain;
    int input_zero, output_zero, minimum;
};

struct head_scale {
    float step;
    int zero;
};

static int8_t quantize(int32_t value, int i, const struct quant_conv *p)
{
    const int q = (int)(((int64_t)value * p->gain[i] + (1LL << 30)) >> 31);
    return (int8_t)eye_limit(q + p->output_zero, p->minimum, 127);
}

#if defined(__ARM_NEON)
static inline int32x4_t dot16(int32x4_t sum, int8x16_t src, int8x16_t weight)
{
    /* Two int8 products fit in an int16, including the signed activation range. */
    int16x8_t part = vmull_s8(vget_low_s8(src), vget_low_s8(weight));
    part = vmlal_s8(part, vget_high_s8(src), vget_high_s8(weight));
    return vpadalq_s16(sum, part);
}

static inline int32x4_t requant4(int32x4_t v, int i, const struct quant_conv *p)
{
    v = vqrdmulhq_s32(v, vld1q_s32(p->gain + i));
    v = vaddq_s32(v, vdupq_n_s32(p->output_zero));
    return vmaxq_s32(v, vdupq_n_s32(p->minimum));
}

static inline int32x4_t dot_four(const int8_t *src, const int8_t *w, int ci)
{
    int32x4_t a = vdupq_n_s32(0), b = a, c = a, d = a;
    for (int j = 0; j < ci; j += 16) {
        const int8x16_t pixels = vld1q_s8(src + j);
        a = dot16(a, pixels, vld1q_s8(w + j));
        b = dot16(b, pixels, vld1q_s8(w + ci + j));
        c = dot16(c, pixels, vld1q_s8(w + 2 * ci + j));
        d = dot16(d, pixels, vld1q_s8(w + 3 * ci + j));
    }
    const int32x2_t a2 = vadd_s32(vget_low_s32(a), vget_high_s32(a));
    const int32x2_t b2 = vadd_s32(vget_low_s32(b), vget_high_s32(b));
    const int32x2_t c2 = vadd_s32(vget_low_s32(c), vget_high_s32(c));
    const int32x2_t d2 = vadd_s32(vget_low_s32(d), vget_high_s32(d));
    return vcombine_s32(vpadd_s32(a2, b2), vpadd_s32(c2, d2));
}
#endif

static void project(const int8_t *src, int8_t *dst, int ci, int co, const struct quant_conv *p)
{
    int channel = 0;
#if defined(__ARM_NEON)
    for (; channel + 4 <= co; channel += 4) {
        int32x4_t v = dot_four(src, p->weights + channel * ci, ci);
        v = vaddq_s32(v, vld1q_s32(p->bias + channel));
        const int16x4_t words = vqmovn_s32(requant4(v, channel, p));
        const int8x8_t pack = vqmovn_s16(vcombine_s16(words, words));
        const uint32_t bytes = vget_lane_u32(vreinterpret_u32_s8(pack), 0);
        __builtin_memcpy(dst + channel, &bytes, sizeof(bytes));
    }
#endif
    for (; channel < co; ++channel) {
        const int8_t *w = p->weights + channel * ci;
        int32_t dot = p->bias[channel];
#if defined(__ARM_NEON)
        int32x4_t acc = vdupq_n_s32(0);
        for (int j = 0; j < ci; j += 16)
            acc = dot16(acc, vld1q_s8(src + j), vld1q_s8(w + j));
        const int32x2_t pair = vadd_s32(vget_low_s32(acc), vget_high_s32(acc));
        dot += vget_lane_s32(vpadd_s32(pair, pair), 0);
#else
        for (int j = 0; j < ci; ++j)
            dot += src[j] * w[j];
#endif
        dst[channel] = quantize(dot, channel, p);
    }
}

static void pointwise(const int8_t *src, int8_t *dst, int pixels, int ci, int co,
                      const struct quant_conv *p)
{
    for (int i = 0; i < pixels; ++i)
        project(src + i * ci, dst + i * co, ci, co, p);
}

static void stem(const int8_t *src, int8_t *dst, const struct quant_conv *p)
{
    for (int row = 0; row < SIDE / 2; ++row) {
        for (int col = 0; col < SIDE / 2; ++col) {
            _Alignas(16) int8_t patch[32] = {0};
            for (int k = 0; k < 9; ++k) {
                const int r = 2 * row + k / 3 - 1, c = 2 * col + k % 3 - 1;
                if ((unsigned)r < SIDE && (unsigned)c < SIDE) {
                    const int8_t *pixel = src + (r * SIDE + c) * 3;
                    patch[3 * k] = pixel[0];
                    patch[3 * k + 1] = pixel[1];
                    patch[3 * k + 2] = pixel[2];
                } else {
                    patch[3 * k] = patch[3 * k + 1] = patch[3 * k + 2] = (int8_t)p->input_zero;
                }
            }
            project(patch, dst, 32, 16, p);
            dst += 16;
        }
    }
}

static void depthwise(const int8_t *src, int8_t *dst, int width, int channels,
                      const struct quant_conv *p)
{
    int8_t zero[64];
    for (int i = 0; i < channels; ++i)
        zero[i] = (int8_t)p->input_zero;
    for (int y = 0; y < width; ++y) {
        for (int x = 0; x < width; ++x) {
            const int8_t *tap[9];
            for (int t = 0; t < 9; ++t) {
                int yy = y + t / 3 - 1, xx = x + t % 3 - 1;
                tap[t] = (unsigned)yy < (unsigned)width && (unsigned)xx < (unsigned)width ?
                         src + (yy * width + xx) * channels : zero;
            }
            int c = 0;
#if defined(__ARM_NEON)
            for (; c + 8 <= channels; c += 8) {
                int32x4_t lo = vld1q_s32(p->bias + c), hi = vld1q_s32(p->bias + c + 4);
                for (int t = 0; t < 8; t += 2) {
                    int16x8_t product = vmull_s8(vld1_s8(tap[t] + c), vld1_s8(p->weights + t * channels + c));
                    product = vmlal_s8(product, vld1_s8(tap[t + 1] + c), vld1_s8(p->weights + (t + 1) * channels + c));
                    lo = vaddw_s16(lo, vget_low_s16(product));
                    hi = vaddw_s16(hi, vget_high_s16(product));
                }
                const int16x8_t last = vmull_s8(vld1_s8(tap[8] + c), vld1_s8(p->weights + 8 * channels + c));
                lo = vaddw_s16(lo, vget_low_s16(last));
                hi = vaddw_s16(hi, vget_high_s16(last));
                const int16x8_t scaled = vcombine_s16(vqmovn_s32(requant4(lo, c, p)),
                                                      vqmovn_s32(requant4(hi, c + 4, p)));
                vst1_s8(dst + c, vqmovn_s16(scaled));
            }
#endif
            for (; c < channels; ++c) {
                int32_t dot = p->bias[c];
                for (int t = 0; t < 9; ++t)
                    dot += tap[t][c] * p->weights[t * channels + c];
                dst[c] = quantize(dot, c, p);
            }
            dst += channels;
        }
    }
}

static void maxpool(const int8_t *src, int8_t *dst, int width, int ch)
{
    for (int y = 0; y < width; y += 2) {
        for (int x = 0; x < width; x += 2) {
            const int8_t *top = src + (y * width + x) * ch;
            const int8_t *bottom = top + width * ch;
            for (int c = 0; c < ch; ++c) {
                int a = top[c] > top[c + ch] ? top[c] : top[c + ch];
                int b = bottom[c] > bottom[c + ch] ? bottom[c] : bottom[c + ch];
                dst[c] = (int8_t)(a > b ? a : b);
            }
            dst += ch;
        }
    }
}

static void add_nearest(const int8_t *fine, const int8_t *coarse, int8_t *dst, int width, int ch,
                        int fine_gain, int coarse_gain, int offset)
{
    for (int y = 0; y < width; ++y) {
        for (int x = 0; x < width; ++x) {
            const int8_t *b = coarse + (y / 2 * (width / 2) + x / 2) * ch;
            for (int c = 0; c < ch; ++c) {
                const int q = (fine[c] * fine_gain + b[c] * coarse_gain + offset + 16384) >> 15;
                dst[c] = (int8_t)eye_limit(q, -128, 127);
            }
            fine += ch;
            dst += ch;
        }
    }
}

#include "yunet_parameters.h"

static struct {
    _Alignas(16) int8_t patch[PATCH_BYTES], arena[YUNET_ARENA_BYTES];
} workspace;
static bool workspace_busy;

struct crop {
    int cx, cy, side, cosine, sine;
};

struct bgr {
    int channel[3];
};

static struct bgr read_pixel(const uint8_t *yuv, int width, int height, int x, int y)
{
    const unsigned at_uv = width * height + (y / 2) * width + (x & ~1);
    const int luma = yuv[y * width + x] << 14;
    const int cb = yuv[at_uv] - 128, cr = yuv[at_uv + 1] - 128;
    return (struct bgr){{
        eye_limit((luma + 29032 * cb + 8192) >> 14, 0, 255),
        eye_limit((luma - 5638 * cb - 11700 * cr + 8192) >> 14, 0, 255),
        eye_limit((luma + 22970 * cr + 8192) >> 14, 0, 255)
    }};
}

static void sample_bgr(const uint8_t *yuv, int width, int height, int x, int y, int8_t *dst)
{
    x = eye_limit(x, 0, (width - 1) * XR);
    y = eye_limit(y, 0, (height - 1) * XR);
    const int col = x / XR, row = y / XR;
    const int right = col + (col < width - 1), bottom = row + (row < height - 1);
    const int fx = x % XR, fy = y % XR;
    const struct bgr a = read_pixel(yuv, width, height, col, row);
    const struct bgr b = read_pixel(yuv, width, height, right, row);
    const struct bgr c = read_pixel(yuv, width, height, col, bottom);
    const struct bgr d = read_pixel(yuv, width, height, right, bottom);
    for (int i = 0; i < 3; ++i) {
        const int top = a.channel[i] * (XR - fx) + b.channel[i] * fx;
        const int low = c.channel[i] * (XR - fx) + d.channel[i] * fx;
        dst[i] = (int8_t)(((top * (XR - fy) + low * fy + 32768) >> 16) - 128);
    }
}

static struct crop face_crop(struct eye_box face, int angle)
{
    /* Stock reports the rotation that makes the face upright. */
    static const int sine[24] = {0, 66, 128, 181, 222, 247, 256, 247, 222, 181, 128, 66,
                                0, -66, -128, -181, -222, -247, -256, -247, -222, -181, -128, -66};
    const int direction = ((360 - angle + 7) / 15) % 24;
    return (struct crop){face.x * XR + face.w * (XR / 2), face.y * XR + face.h * (XR / 2),
                         face.w >= face.h ? face.w : face.h, sine[(direction + 6) % 24], sine[direction]};
}

static void project_pixel(struct crop crop, int x, int y, int *sx, int *sy)
{
    *sx = crop.cx + (crop.cosine * x - crop.sine * y) / XR;
    *sy = crop.cy + (crop.sine * x + crop.cosine * y) / XR;
}

static void fill_patch(const uint8_t *yuv, int width, int height, struct crop crop, int8_t *patch)
{
    for (int row = 0; row < SIDE; ++row) {
        const int y = (2 * row + 1 - SIDE) * crop.side * (XR / 2 / SIDE);
        for (int col = 0; col < SIDE; ++col) {
            const int x = (2 * col + 1 - SIDE) * crop.side * (XR / 2 / SIDE);
            int sx, sy;
            project_pixel(crop, x, y, &sx, &sy);
            sample_bgr(yuv, width, height, sx - XR / 2, sy - XR / 2, patch);
            patch += 3;
        }
    }
}

struct rectf { float x, y, w, h; };

static float head_value(int index, int8_t q)
{
    return (q - head_scales[index].zero) * head_scales[index].step;
}

static bool face_overlap(struct rectf a, struct rectf b)
{
    const float left = a.x > b.x ? a.x : b.x, top = a.y > b.y ? a.y : b.y;
    const float ar = a.x + a.w, br = b.x + b.w, ab = a.y + a.h, bb = b.y + b.h;
    const float right = ar < br ? ar : br, bottom = ab < bb ? ab : bb;
    if (right <= left || bottom <= top)
        return false;
    const float intersection = (right - left) * (bottom - top);
    return intersection * 1.35f >= (a.w * a.h + b.w * b.h) * 0.35f;
}

static bool decode(const int8_t *out[12], struct crop crop, struct eye_box face, struct eye_point eyes[2])
{
    const int c = eye_abs(crop.cosine), s = eye_abs(crop.sine), span = crop.side * XR;
    const float fw = (float)(eye_limit(face.w * c + face.h * s, 0, span) * SIDE) / span;
    const float fh = (float)(eye_limit(face.h * c + face.w * s, 0, span) * SIDE) / span;
    const struct rectf roi = {(SIDE - fw) / 2, (SIDE - fh) / 2, fw, fh};
    uint32_t best = 0;
    float px[2] = {0}, py[2] = {0};
    for (int i = 0; i < 3; ++i) {
        const int stride = 8 << i, grid = SIDE / stride;
        for (int k = 0; k < grid * grid; ++k) {
            const uint32_t confidence = (uint32_t)scores[i][out[i][k] + 128] *
                                        scores[3 + i][out[3 + i][k] + 128];
            if (confidence < UINT32_C(1546141041) || confidence <= best)
                continue;
            const int r = k / grid, c = k % grid;
            const int8_t *b = out[6 + i] + 4 * k, *p = out[9 + i] + 10 * k;
            const float w = extents[i][b[2] + 128] * stride, h = extents[i][b[3] + 128] * stride;
            const struct rectf box = {(c + head_value(6 + i, b[0])) * stride - w / 2,
                                      (r + head_value(6 + i, b[1])) * stride - h / 2, w, h};
            if (!face_overlap(roi, box))
                continue;
            best = confidence;
            for (int side = 0; side < 2; ++side) {
                px[side] = (c + head_value(9 + i, p[2 * side])) * stride;
                py[side] = (r + head_value(9 + i, p[2 * side + 1])) * stride;
            }
        }
    }
    if (!best)
        return false;
    /* Index zero always points to the left of the upright face. */
    const unsigned first = px[0] <= px[1] ? 0u : 1u;
    for (unsigned side = 0; side < 2; ++side) {
        const unsigned p = first ^ side;
        if (px[p] < roi.x || px[p] >= roi.x + roi.w || py[p] < roi.y || py[p] >= roi.y + 2 * roi.h / 3)
            continue;
        int sx, sy;
        project_pixel(crop, (int)((px[p] - SIDE / 2) * crop.side * (XR / SIDE)),
                            (int)((py[p] - SIDE / 2) * crop.side * (XR / SIDE)), &sx, &sy);
        sx = (sx + XR / 2) / XR;
        sy = (sy + XR / 2) / XR;
        if (sx >= face.x && sx < face.x + face.w && sy >= face.y && sy < face.y + face.h)
            eyes[side] = (struct eye_point){sx, sy, true};
    }
    return eyes[0].valid || eyes[1].valid;
}

bool eye_yunet_landmarks(const uint8_t *yuv, int width, int height, struct eye_box face,
                         int angle, struct eye_point eyes[2])
{
    eyes[0] = eyes[1] = (struct eye_point){0};
    if (!yuv || width != 528 || height != 352 || !eye_box_in_frame(face, width, height) ||
        face.w < 32 || face.h < 32 || face.w > 160 || face.h > 160 || angle < -180 || angle > 180)
        return false;
    if (__atomic_test_and_set(&workspace_busy, __ATOMIC_ACQUIRE))
        return false;
    const struct crop crop = face_crop(face, angle);
    const int8_t *out[12];
    fill_patch(yuv, width, height, crop, workspace.patch);
    yunet_forward(workspace.patch, workspace.arena, out);
    const bool found = decode(out, crop, face, eyes);
    __atomic_clear(&workspace_busy, __ATOMIC_RELEASE);
    return found;
}
