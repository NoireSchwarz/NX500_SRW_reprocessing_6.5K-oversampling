/* SPDX-License-Identifier: AGPL-3.0-only.
 * P48 single-install, one-CAP, ten-frame transport contract. Original MCB entry
 * RETURNS after each frame; Linux proves worker/liveness/cleanup before reuse.
 * This header defines wire data and validators only, not the native protocol.
 * Words are LE32 on the pinned ARM. Every writer owns its separate region.
 */
#ifndef P48_BATCH_CONTRACT_H
#define P48_BATCH_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#define P48_BATCH_MAX_FRAMES 10u
#define P48_BATCH_CONTROL_OFFSET 0xb400u
#define P48_BATCH_CONTROL_BYTES 1024u
#define P48_BATCH_FROZEN_OFFSET 0xb800u
#define P48_BATCH_FROZEN_BYTES 256u
#define P48_IDLE_HANDOFF_OFFSET 0xba00u
#define P48_PREVIOUS_IDLE_OFFSET 0xbb00u
#define P48_IDLE_HANDOFF_MAGIC 0x48473850u
#define P48_BATCH_LISTENER_OFFSET 0xe000u
#define P48_BATCH_LISTENER_STRIDE 0x80u
#define P48_BATCH_VTABLE_DELTA 0x40u
#define P48_BATCH_SOURCE_BYTES 57938944u
#define P48_BATCH_TARGET_BYTES 56177664u
#define P48_BATCH_CC0_ADDRESS 0xbb71d200u
#define P48_BATCH_CC1_ADDRESS 0xbb71f200u
#define P48_BATCH_REQUEST_MAGIC 0x51523850u
#define P48_BATCH_SAVED_MAGIC 0x56533850u
#define P48_BATCH_REPLY_MAGIC 0x50523850u
#define P48_BATCH_ABORT_MAGIC 0x42413850u
#define P48_BATCH_FROZEN_MAGIC 0x5a463850u
#define P48_BATCH_SCHEMA 1u
#define P48_PLAN_SOURCE_WB 1u
#define P48_PLAN_SOURCE_CC 2u
#define P48_REPLY_STOP_VERIFIED 1u
#define P48_REPLY_TASK_COMPLETED 2u
#define P48_REPLY_CLEANUP_VERIFIED 4u
#define P48_REPLY_MCB_RETURNED 8u /* Set by Linux in its separate audit, NOT RTOS reply. */
#define P48_REPLY_WB_VISIBILITY_WARNING 16u
#define P48_REPLY_STOP_REUSED 32u
enum p48_batch_phase {
 P48_PHASE_READY=1,     /* Original entry validated; waiting short Linux GO. */
 P48_PHASE_ACK=2,       /* Request accepted; no source/target writes by Linux. */
 P48_PHASE_INFLIGHT=3,
 P48_PHASE_DONE=4,      /* Native task+cleanup ended; entry is about to return. */
 P48_PHASE_FAILED=5
};
/* Host request: write words0..30 first, publish seq in word31 LAST. Linux may
 * publish only after prior saved marker and actual MCB return/liveness, and
 * after a newly verified camera pause/short-GO boundary. RTOS reads twice.
 * sha256 words are raw digest bytes grouped LE32, not hex text.
 */
enum p48_request_word {
 P48_Q_MAGIC=0,P48_Q_SCHEMA=1,P48_Q_NONCE=2,P48_Q_SEQ=3,
 P48_Q_FRAME_INDEX=4,P48_Q_TOTAL=5,P48_Q_SOURCE_ID=6,P48_Q_FLAGS=7,
 P48_Q_SOURCE_FNV=8,P48_Q_SOURCE_BYTES=9,P48_Q_WB_CRC=10,P48_Q_CC_CRC=11,
 P48_Q_SOURCE_SHA=12,P48_Q_TARGET_BYTES=20,P48_Q_RESERVED=21,
 P48_Q_CRC=30,P48_Q_PUBLISHED=31
};
/* Host SAVED claim: only after canonical result/audits and full planes were
 * copied/hashed/written/flushed/fsynced. It is a host assertion of saving, not
 * independent RTOS proof of SD durability. No subsequent frame on failed save.
 */
enum p48_saved_word {
 P48_S_MAGIC=0,P48_S_SCHEMA=1,P48_S_NONCE=2,P48_S_SEQ=3,
 P48_S_FRAME_INDEX=4,P48_S_SOURCE_FNV=5,P48_S_TARGET_FNV=6,
 P48_S_FILES_COMMITTED=7,P48_S_REQUEST_CRC=8,P48_S_REPLY_CRC=9,
 P48_S_RESERVED=10,P48_S_CRC=30,P48_S_PUBLISHED=31
};
/* RTOS reply: RTOS alone writes, seq is1..total. Publish word63 last; Linux
 * requires two identical complete copies with CRC/token. A DONE reply does
 * NOT prove nxl_entry returned; Linux MUST additionally observe queued native
 * console liveness after the caller and detached parked worker before reuse.
 * No cleared counters can satisfy the DONE validator.
 */
enum p48_reply_word {
 P48_R_MAGIC=0,P48_R_SCHEMA=1,P48_R_NONCE=2,P48_R_SEQ=3,
 P48_R_PHASE=4,P48_R_FLAGS=5,P48_R_ACK_SEQ=6,P48_R_DONE_SEQ=7,
 P48_R_FRAME_INDEX=8,P48_R_TOTAL=9,P48_R_SOURCE_ID=10,P48_R_PLAN_FLAGS=11,
 P48_R_SOURCE_FNV=12,P48_R_TARGET_FNV=13,P48_R_NATIVE_PHASE=14,
 P48_R_FRAME_COUNT=15,P48_R_FRAME_STATUS=16,P48_R_DMA_COUNT=17,P48_R_DMA_STATUS=18,
 P48_R_CLEANUP_STATUS=19,P48_R_WB_RESTORE_STATUS=20,P48_R_CC_RESTORE_STATUS=21,
 P48_R_ERROR=22,P48_R_LISTENER_PA=23,P48_R_CONFIG_STATUS=24,P48_R_START_STATUS=25,
 P48_R_INPUT_CACHE_STATUS=26,P48_R_OUTPUT_PRE_CACHE_STATUS=27,P48_R_OUTPUT_POST_CACHE_STATUS=28,
 P48_R_REQUEST_CRC=29,P48_R_PREVIOUS_SAVED_CRC=30,P48_R_STALE_IRQ_COUNT=31,
 P48_R_FOREIGN_IRQ_COUNT=32,P48_R_STOP_CALLS=33,P48_R_CONFIG_CALLS=34,
 P48_R_START_CALLS=35,P48_R_CLEANUP_CALLS=36,P48_R_STOP_REUSE_CHECKS=37,
 P48_R_IDLE_HANDOFF_CRC=38,P48_R_PREVIOUS_IDLE_CRC=39,P48_R_RESERVED=40,
 P48_R_CRC=62,P48_R_PUBLISHED=63
};
/* Host abort is the ONLY host writable area while in flight. Abort is not a
 * hardware cancel or permission to restore/cleanup an uncertain DMA. */
enum p48_abort_word {
 P48_A_MAGIC=0,P48_A_SCHEMA=1,P48_A_NONCE=2,P48_A_SEQ=3,
 P48_A_REASON=4,P48_A_RESERVED=5,P48_A_CRC=14,P48_A_PUBLISHED=15
};
/* First config captures an immutable owned template. RTOS writes once, Linux
 * never writes it. Full WB6 and CC10 templates retain opaque bits/halfwords.
 * wb_base is the value of global813ebf5c; cc pointers are exact original bank
 * DDR addresses, not MMIO and not source metadata. valid_mask must15.
 */
enum p48_frozen_word {
 P48_F_MAGIC=0,P48_F_SCHEMA=1,P48_F_NONCE=2,P48_F_CAPTURED_SEQ=3,
 P48_F_WB_BASE=4,P48_F_CC0_ADDRESS=5,P48_F_CC1_ADDRESS=6,P48_F_VALID_MASK=7,
 P48_F_WB_PRIMARY=8,P48_F_WB_SECONDARY=14,P48_F_CC0=20,P48_F_CC1=30,
 P48_F_ORIGINAL_TASK_STATE=40,P48_F_INITIAL_STOP_MAGIC=41,P48_F_INITIAL_STOP_CRC=42,
 P48_F_INITIAL_STOP_PA=43,P48_F_INITIAL_STOP_SEQ=44,P48_F_RESERVED=45,P48_F_CRC=62,P48_F_PUBLISHED=63
};
struct p48_batch_control {
 uint32_t request[32];          /* +0x000 Linux writer */
 uint32_t saved[32];            /* +0x080 Linux writer */
 uint32_t reply[64];            /* +0x100 RTOS writer */
 uint32_t abort[16];            /* +0x200 Linux writer */
 uint32_t reserved[112];        /* +0x240..0x3ff must remain zero */
};
struct p48_batch_frozen {uint32_t w[64];};
_Static_assert(sizeof(struct p48_batch_control)==P48_BATCH_CONTROL_BYTES,"batch control1024");
_Static_assert(offsetof(struct p48_batch_control,saved)==0x80u,"saved offset");
_Static_assert(offsetof(struct p48_batch_control,reply)==0x100u,"reply offset");
_Static_assert(offsetof(struct p48_batch_control,abort)==0x200u,"abort offset");
_Static_assert(sizeof(struct p48_batch_frozen)==P48_BATCH_FROZEN_BYTES,"frozen256");
_Static_assert(P48_BATCH_CONTROL_OFFSET+P48_BATCH_CONTROL_BYTES==P48_BATCH_FROZEN_OFFSET,"control/frozen boundary");
_Static_assert(P48_BATCH_FROZEN_OFFSET+P48_BATCH_FROZEN_BYTES<=P48_BATCH_LISTENER_OFFSET,"frozen/listener boundary");
_Static_assert(P48_BATCH_FROZEN_OFFSET+256u==0xb900u&&0xb900u+256u==P48_IDLE_HANDOFF_OFFSET&&P48_IDLE_HANDOFF_OFFSET+256u==P48_PREVIOUS_IDLE_OFFSET&&P48_PREVIOUS_IDLE_OFFSET+256u<=0xc000u,"owned handoff data before code");
_Static_assert(P48_BATCH_LISTENER_OFFSET+P48_BATCH_MAX_FRAMES*P48_BATCH_LISTENER_STRIDE<=0x10000u,"listeners before Bayer");
_Static_assert(P48_BATCH_LISTENER_OFFSET+P48_BATCH_MAX_FRAMES*P48_BATCH_LISTENER_STRIDE<=0xe800u,"listeners before owned stop code");
static inline uint32_t p48_batch_crc(const void *data,size_t bytes){
 const unsigned char *p=data;uint32_t c=~0u;
 while(bytes--){c^=*p++;for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;
}
static inline uint32_t p48_batch_token(uint32_t nonce,uint32_t seq,uint32_t phase,uint32_t crc){
 return (nonce^seq*0x9e3779b9u^phase*0x85ebca6bu^crc^0x48385042u)|1u;
}
/* Word0 of the existing12B true-entry message. Different from batch nonce and
 * different for each frame; old delayed bridge messages cannot start a new
 * request merely because source PA/bytes are reused. Do NOT use this token for
 * slot pre/post cache messages or substitute it for result/IRQ batch nonce. */
static inline uint32_t p48_batch_entry_token(uint32_t nonce,unsigned seq){
 return nonce&&seq&&seq<=P48_BATCH_MAX_FRAMES?((nonce^(0x48e10000u+seq*0x101u))|1u):0u;
}
static inline int p48_batch_zero(const uint32_t *w,unsigned lo,unsigned hi){for(unsigned i=lo;i<hi;i++)if(w[i])return 0;return 1;}
static inline int p48_idle_handoff_valid(const uint32_t h[64],uint32_t nonce,unsigned seq){
 if(!h||!nonce||!seq||seq>10||h[0]!=P48_IDLE_HANDOFF_MAGIC||h[1]!=1||h[2]!=nonce||h[3]!=seq||h[4]!=1||
    h[5]<0x81000000u||h[5]>=0x93fff000u||(h[5]&3u)||!h[6]||h[6]>65535||!p48_batch_zero(h,36,62))return 0;
 for(unsigned i=0;i<14;i++)if(h[8+i]!=h[22+i])return 0;
 return h[62]==p48_batch_crc(h,248)&&h[63]==p48_batch_token(nonce,seq,P48_IDLE_HANDOFF_MAGIC,h[62]);
}
static inline int p48_batch_count(unsigned total){return total>0&&total<=P48_BATCH_MAX_FRAMES;}
static inline uint32_t p48_batch_listener(uint32_t cap,unsigned frame_index){
 if(frame_index>=P48_BATCH_MAX_FRAMES||(uint64_t)cap+0x10000u>UINT32_MAX)return 0;
 return cap+P48_BATCH_LISTENER_OFFSET+frame_index*P48_BATCH_LISTENER_STRIDE;
}
/* The caller passes compiled-plan values. No request can silently choose a
 * different source or turn a baseline frame into WB/CC by rewriting host data.
 */
static inline int p48_batch_request_valid(const uint32_t q[32],uint32_t nonce,unsigned seq,unsigned total,unsigned source_id,unsigned flags){
 if(!q||!nonce||!p48_batch_count(total)||!seq||seq>total||flags&~3u)return 0;
 return q[P48_Q_MAGIC]==P48_BATCH_REQUEST_MAGIC&&q[P48_Q_SCHEMA]==P48_BATCH_SCHEMA&&q[P48_Q_NONCE]==nonce&&
  q[P48_Q_SEQ]==seq&&q[P48_Q_PUBLISHED]==seq&&q[P48_Q_FRAME_INDEX]==seq-1&&q[P48_Q_TOTAL]==total&&
  q[P48_Q_SOURCE_ID]==source_id&&q[P48_Q_FLAGS]==flags&&q[P48_Q_SOURCE_BYTES]==P48_BATCH_SOURCE_BYTES&&
  q[P48_Q_TARGET_BYTES]==P48_BATCH_TARGET_BYTES&&p48_batch_zero(q,P48_Q_RESERVED,P48_Q_CRC)&&
  q[P48_Q_CRC]==p48_batch_crc(q,P48_Q_CRC*4u);
}
/* Pre-GO identity check only: publication MUST still be zero. This may justify
 * READY but never ACK/config/start; after Linux publishesQ31+oldGO the normal
 * request_valid must pass afresh. Uses a local copy, never changes CAP data. */
static inline int p48_batch_request_staged_valid(const uint32_t q[32],uint32_t nonce,unsigned seq,unsigned total,unsigned source_id,unsigned flags){
 if(!q||q[P48_Q_PUBLISHED])return 0;
 uint32_t copy[32];for(unsigned i=0;i<32;i++)copy[i]=q[i];copy[P48_Q_PUBLISHED]=seq;
 return p48_batch_request_valid(copy,nonce,seq,total,source_id,flags);
}
static inline int p48_batch_saved_valid(const uint32_t s[32],uint32_t nonce,unsigned seq,const uint32_t q[32],const uint32_t r[64]){
 if(!s||!q||!r||!nonce||!seq||seq>P48_BATCH_MAX_FRAMES)return 0;
 return s[P48_S_MAGIC]==P48_BATCH_SAVED_MAGIC&&s[P48_S_SCHEMA]==1&&s[P48_S_NONCE]==nonce&&s[P48_S_SEQ]==seq&&
  s[P48_S_PUBLISHED]==seq&&s[P48_S_FRAME_INDEX]==seq-1&&s[P48_S_FILES_COMMITTED]==1&&
  s[P48_S_SOURCE_FNV]==q[P48_Q_SOURCE_FNV]&&s[P48_S_TARGET_FNV]==r[P48_R_TARGET_FNV]&&
  s[P48_S_REQUEST_CRC]==q[P48_Q_CRC]&&s[P48_S_REPLY_CRC]==r[P48_R_CRC]&&
  p48_batch_zero(s,P48_S_RESERVED,P48_S_CRC)&&s[P48_S_CRC]==p48_batch_crc(s,P48_S_CRC*4u);
}
static inline int p48_batch_reply_valid(const uint32_t r[64],uint32_t nonce,unsigned seq,unsigned total){
 if(!r||!nonce||!p48_batch_count(total)||!seq||seq>total||r[P48_R_MAGIC]!=P48_BATCH_REPLY_MAGIC||
  r[P48_R_SCHEMA]!=1||r[P48_R_NONCE]!=nonce||r[P48_R_SEQ]!=seq||r[P48_R_FRAME_INDEX]!=seq-1||r[P48_R_TOTAL]!=total||
  r[P48_R_PHASE]<P48_PHASE_READY||r[P48_R_PHASE]>P48_PHASE_FAILED||r[P48_R_FLAGS]&~55u||
  !p48_batch_zero(r,P48_R_RESERVED,P48_R_CRC)||r[P48_R_CRC]!=p48_batch_crc(r,P48_R_CRC*4u)||
  r[P48_R_PUBLISHED]!=p48_batch_token(nonce,seq,r[P48_R_PHASE],r[P48_R_CRC]))return 0;
 if(r[P48_R_PHASE]==P48_PHASE_READY)return r[P48_R_ACK_SEQ]==seq-1&&r[P48_R_DONE_SEQ]==seq-1;
 if(r[P48_R_PHASE]==P48_PHASE_ACK||r[P48_R_PHASE]==P48_PHASE_INFLIGHT)return r[P48_R_ACK_SEQ]==seq&&r[P48_R_DONE_SEQ]==seq-1;
 if(r[P48_R_PHASE]==P48_PHASE_DONE){
  return r[P48_R_ACK_SEQ]==seq&&r[P48_R_DONE_SEQ]==seq&&(r[P48_R_FLAGS]&7u)==7u&&
   r[P48_R_FRAME_COUNT]==1&&r[P48_R_DMA_COUNT]==1&&!r[P48_R_FRAME_STATUS]&&!r[P48_R_DMA_STATUS]&&
   !r[P48_R_CLEANUP_STATUS]&&!r[P48_R_WB_RESTORE_STATUS]&&!r[P48_R_CC_RESTORE_STATUS]&&!r[P48_R_ERROR]&&
   !r[P48_R_CONFIG_STATUS]&&!r[P48_R_START_STATUS]&&(int32_t)r[P48_R_INPUT_CACHE_STATUS]>=0&&
   (int32_t)r[P48_R_OUTPUT_PRE_CACHE_STATUS]>=0&&(int32_t)r[P48_R_OUTPUT_POST_CACHE_STATUS]>=0&&
   r[P48_R_STOP_CALLS]==(seq==1?1u:0u)&&r[P48_R_STOP_REUSE_CHECKS]==(seq==1?0u:1u)&&
   !!(r[P48_R_FLAGS]&P48_REPLY_STOP_REUSED)==(seq!=1)&&
   r[P48_R_CONFIG_CALLS]==1&&r[P48_R_START_CALLS]==1&&r[P48_R_CLEANUP_CALLS]==1;
 }
 return r[P48_R_PHASE]==P48_PHASE_FAILED&&r[P48_R_ERROR]!=0;
}
static inline int p48_batch_abort_valid(const uint32_t a[16],uint32_t nonce,unsigned seq){
 return a&&nonce&&seq&&seq<=P48_BATCH_MAX_FRAMES&&a[P48_A_MAGIC]==P48_BATCH_ABORT_MAGIC&&a[P48_A_SCHEMA]==1&&
  a[P48_A_NONCE]==nonce&&a[P48_A_SEQ]==seq&&a[P48_A_REASON]&&p48_batch_zero(a,P48_A_RESERVED,P48_A_CRC)&&
  a[P48_A_CRC]==p48_batch_crc(a,P48_A_CRC*4u)&&a[P48_A_PUBLISHED]==p48_batch_token(nonce,seq,0,a[P48_A_CRC]);
}
static inline int p48_batch_frozen_valid(const uint32_t f[64],uint32_t nonce){
 return f&&nonce&&f[P48_F_MAGIC]==P48_BATCH_FROZEN_MAGIC&&f[P48_F_SCHEMA]==1&&f[P48_F_NONCE]==nonce&&
  f[P48_F_CAPTURED_SEQ]==1&&f[P48_F_WB_BASE]==0x20810000u&&f[P48_F_VALID_MASK]==15u&&
  f[P48_F_CC0_ADDRESS]==P48_BATCH_CC0_ADDRESS&&f[P48_F_CC1_ADDRESS]==P48_BATCH_CC1_ADDRESS&&
  (f[P48_F_ORIGINAL_TASK_STATE]&255u)==7u&&
  f[P48_F_INITIAL_STOP_MAGIC]==0x53543850u&&f[P48_F_INITIAL_STOP_SEQ]==1&&
  f[P48_F_INITIAL_STOP_PA]>=0x9400b900u&&f[P48_F_INITIAL_STOP_PA]<0xc0000000u&&
  (f[P48_F_INITIAL_STOP_PA]&16383u)==0x3900u&&
  p48_batch_zero(f,P48_F_RESERVED,P48_F_CRC)&&f[P48_F_CRC]==p48_batch_crc(f,P48_F_CRC*4u)&&
  f[P48_F_PUBLISHED]==p48_batch_token(nonce,1,P48_BATCH_FROZEN_MAGIC,f[P48_F_CRC]);
}
#endif
