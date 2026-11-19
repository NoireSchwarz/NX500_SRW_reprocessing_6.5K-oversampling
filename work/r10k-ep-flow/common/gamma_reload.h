/* SPDX-License-Identifier: AGPL-3.0-only. Native Gamma reload experiment.
 * Callbacks remain in an owned 2MiB CAP lease until an actual OS boot.
 * Neither unpublishing a callback nor an equal DDR image retires old PCs.
 */
#ifndef NXL_GAMMA_RELOAD_H
#define NXL_GAMMA_RELOAD_H
#include <stdint.h>
#define NG_MAGIC 0x3147524eu
#define NG_SCHEMA 3u
#define NG_STATE 0x5000u
#define NG_OBJECT 0x6000u
#define NG_META 0x7100u
#define NG_WIDTHS 0x7200u
#define NG_PARENT 0x7300u
#define NG_GAMMA_SLOT 0x8062f6c8u
#define NG_PARENT_SLOT0 0x8062f600u
#define NG_PARENT_SLOT1 0x8063e350u
#define NG_CALCULATE 0x8038f90cu
#define NG_PARENT_UPDATE 0x803c595cu
#define NG_INVALIDATE 0x803c53a0u
#define NG_WRITER 0x8049e4d4u
#define NG_DDR0 0xbb71d228u
#define NG_DDR1 0xbb71f228u
#define NG_BYTES 432u
#define NG_WORDS 108u
#define NG_FACTORY 0u
#define NG_LIFT 1u
#define NG_LOG 2u
#define NG_OFF 3u
#define NG_PARENT_VPTR0 0x8062f5f8u
#define NG_PARENT_VPTR1 0x8063e348u
#define NG_COLOR_VPTR 0x8062fd60u
#define NG_COLOR_SLOT 0x8062fd78u
#define NG_COLOR_CALCULATE 0x80391f60u
#define NG_COLOR_INVALIDATE 0x80392640u
#define NG_COLOR_WRITER 0x8017cf84u
#define NG_PARAM_FIELD 0x803c55b8u
#define NG_UPDATE_CHILDREN 0x803c5668u
#define NG_QUEUE 0x8017da10u
#define NG_COLOR_DDR0 0xbb71d200u
#define NG_COLOR_DDR1 0xbb71f200u
struct ng_state {
 uint32_t magic,schema,cap,bytes,nonce,code_hash,owner[4];
 uint32_t mode,epoch,busy,sequence,ever_published,slots_installed;
 uint32_t parent_active,refresh_epoch,refresh_object,refresh_parent,invalidations,detach_requested,detached;
 uint32_t video_queues,video_epoch,video_object,video_parent,video_context,video_driver_modes,video_hash,video_head[8],video_target[NG_WORDS];
 uint32_t calls,qualified,applies,failures,bypasses,bank1_calls,reentrant;
 uint32_t last_object,last_parent,last_context,last_input,original_return;
 uint32_t calculator_return,writer_return,applied_epoch,applied_object;
 uint32_t queues,queued_epoch,queued_object,queue_return,queue_without_apply;
 uint32_t qualification_reason,fault,target_hash,factory_hash,bank1_hash,bank1_same;
 uint32_t live_gamma_callback,live_parent_callback0,live_parent_callback1;
 uint32_t effective_x[19],effective_y[19],factory[NG_WORDS],target[NG_WORDS];
 uint32_t profile,scene_white_q16,native_black,native_white,physical_reflectance_calibrated;
 uint32_t active_parent,active_context,transaction_fault,force_factory,suspended;
 uint32_t color_calls,color_applies,color_failures,color_reuses,color_object,color_parent,color_input,color_epoch,color_bank1_same;
 uint32_t color_factory[10],color_target[10],color_source_low[9],color_source_high[9];
 uint32_t color_source_key,color_target_hash,color_original_return,color_writer_return,live_color_callback;
 uint32_t factory_x[19],factory_y[19],native_context[49];
 uint32_t video_color_epoch,video_color_object,video_color_hash,video_color_factory[10],video_color_target[10],video_native_context[49];
 uint32_t rollback_count,rollback_failed,parent_reentrant;
 uint32_t parent_calls,parent_ignored,ignored_parent_last,ignored_parent_input_raw;
 uint32_t binding_reason,binding_parent,binding_parent_vptr,binding_input_raw,binding_input_resolved;
 uint32_t binding_gamma,binding_color,binding_gamma_context_before,binding_color_context_before;
 uint32_t binding_getter,binding_getter_calls;
};
_Static_assert(sizeof(struct ng_state)<0x1000,"Gamma state must precede scratch object");
static __attribute__((noinline)) uint32_t ng_mask(unsigned i){/* Callers index the two 54-word banks: 0 <= i < NG_WORDS. */unsigned j=i<54u?i:i-54u;return j<11u?0x7fff7fffu:j<22u?0x07ff07ffu:j==53u?0x0000ffffu:0xffffffffu;}
#endif
