#ifndef NXL_EYE_YUNET_H
#define NXL_EYE_YUNET_H

#include "eye_policy.h"

/* NX500 1.12 face-worker frame: packed Y followed by interleaved UV. */
bool eye_yunet_landmarks(const uint8_t *yuv, int width, int height, struct eye_box face,
                         int angle, struct eye_point eyes[2]);

#endif
