/* SPDX-License-Identifier: AGPL-3.0-only. Fixed real SRW, native 14x3 task. */
#ifndef P39_FRONT_LAYOUT_H
#define P39_FRONT_LAYOUT_H
#include <stdint.h>
#define P39_CAP_BYTES 0xc000000u
#define P39_ACTIVE_BYTES 0x8000000u
#define P49_FRAME_COUNT 1u
#define P39_PROOF_BYTES 0x200000u
#define P39_WINDOW_BYTES 0x100000u
#define P39_TARGET_PROOF_OFFSET 0x100000u
#define P39_CONTEXT_OFFSET 0x3000u
#define P39_RESULT_OFFSET 0x4000u
#define P39_DIAG_OFFSET 0x8000u
#define P39_DIAG_SLOTS 9u
#define P39_DIAG_WORDS 128u
#define P39_DIAG_BYTES (P39_DIAG_SLOTS*P39_DIAG_WORDS*4u)
#define P39_DIAG_MAGIC 0x4338444eu
#define P39_STOP_OFFSET 0x9400u
#define P39_STOP_BYTES 256u
#define P39_STOP_COMMAND_OFFSET 0x9600u
#define P39_STOP_MAGIC 0x44384c4eu
#define P39_JOB_OFFSET 0x5000u
#define P39_INPUT_IMAGE_OFFSET 0x5100u
#define P39_OUTPUT_IMAGE_OFFSET 0x5200u
#define P39_LISTENER_OFFSET 0x6000u
#define P39_VTABLE_OFFSET 0x6040u
#define P39_PACKET_START 0x7000u
#define P39_PACKET_SLOT 128u
#define P39_SOURCE_OFFSET 0x10000u
#define P39_TARGET_OFFSET 0x4000000u
#define P39_SOURCE_W 6608u
#define P39_SOURCE_H 4384u
#define P39_SOURCE_STRIDE 13216u
#define P39_SOURCE_BYTES (P39_SOURCE_STRIDE*P39_SOURCE_H)
#define P39_W 6480u
#define P39_H 4320u
#define P39_TILE_W 464u
#define P39_TILE_H 1472u
#define P39_COLUMNS 14u
#define P39_ROWS 3u
#define P39_TARGET_STRIDE 6496u
#define P39_PLANE_BYTES (P39_TARGET_STRIDE*P39_H)
#define P39_UV_OFFSET ((P39_PLANE_BYTES+65535u)&~65535u)
#define P39_TARGET_BYTES (P39_UV_OFFSET+P39_PLANE_BYTES)
#define P39_CONFIG_VARIANTS 1u
#define P39_ACTIVE_VARIANT 0u
#define P39_GO_XOR 0x38c019a7u
#define P39_MAGIC 0x3833584eu
#define P39_TASK 0x8153e6d4u
#define P39_SLOT 0x8100f7b0u
#define P39_SLOT_PAGE 0x8100f000u
#define P39_ORIGINAL 0x800e454cu
#define P39_MANAGER 0x81dfc800u
#define P39_CACHE 0x8008fd40u
#define P39_READY 2u
#define P39_DONE 7u
/* words0..31 fixed result,32..40 input DMA mirror,41..49 output mirror,
 *50..63 native 56-byte IPC context. GO/cancel are Linux-owned words16/17. */
struct p39_result {uint32_t w[64];};
struct p39_context {uint32_t nonce,cap,cap_bytes,result,source,source_bytes,target,target_bytes,job,listener,vtable,original,slot,manager,cache,task,callback;};
_Static_assert(sizeof(struct p39_result)==256,"result size");
_Static_assert(sizeof(struct p39_context)==68,"context size");
_Static_assert(P39_SOURCE_BYTES==57938944u,"staged real SRW extent");
_Static_assert(P39_SOURCE_OFFSET+P39_SOURCE_BYTES+16384u<P39_TARGET_OFFSET,"source guard");
_Static_assert(P39_TARGET_OFFSET+P39_TARGET_BYTES+16384u<P39_CAP_BYTES,"output guard");
_Static_assert((P39_W+P39_TILE_W-1)/P39_TILE_W==14,"native columns");
_Static_assert((P39_H+P39_TILE_H-1)/P39_TILE_H==3,"native rows");
#endif
