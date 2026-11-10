/* SPDX-License-Identifier: AGPL-3.0-only. Sequential shared160MiB layouts. */
#ifndef NXL_TEST_GEOMETRY_H
#define NXL_TEST_GEOMETRY_H
#include "../../common/shared_workspace.h"
#define TEST_W SW_W
#define TEST_H SW_H
#define TEST_Y (TEST_W*TEST_H)
#define TEST_FRAME SW_FRAME
#define TEST_BYTES SW_BYTES
#define TEST_BLOCK 16384u
#define TEST_PAGES (TEST_BYTES/TEST_BLOCK)
#ifdef NXL_FAST_PIPELINE
#define TEST_TRANSLATIONS 2u
static unsigned test_translation_offset(unsigned i){return i?TEST_BYTES-TEST_BLOCK:0;}
#else
#define TEST_TRANSLATIONS TEST_PAGES
static unsigned test_translation_offset(unsigned i){return i*TEST_BLOCK;}
#endif
#define TEST_IN SW_LDC_OUT
#define TEST_OUT SW_LUT_OUT
#define TEST_JPEG_OUT SW_JPEG_OUT
#define TEST_JPEG_CAP (SW_TABLE-SW_JPEG_OUT-16384u)
#define TEST_TABLE SW_TABLE
#define TEST_BACKUP (TEST_TABLE+0x5000u)
#define TEST_READ (TEST_TABLE+0xa000u)
#define TEST_BACKUP2 (TEST_TABLE+0xf000u)
_Static_assert(TEST_W%16u==0&&TEST_H%16u==0,"native JPEG alignment");
_Static_assert(TEST_OUT+TEST_FRAME+TEST_BLOCK<TEST_IN,"LUT buffers do not overlap");
_Static_assert(TEST_JPEG_OUT+TEST_JPEG_CAP+16384u==TEST_TABLE,"JPEG output before LUT/lease guards");
#endif
