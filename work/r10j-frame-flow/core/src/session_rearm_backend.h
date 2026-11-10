/* SPDX-License-Identifier: AGPL-3.0-only. Pure retirement, not quantizer reset.
 * EP DMA/register pointer values are physical addresses. Linux userspace VA
 * aliases are deliberately not compared with these identity physical roots. */
#ifndef NX51_REARM_BACKEND_H
#define NX51_REARM_BACKEND_H
#include <stdint.h>
#include <string.h>
#include "../../hardware/src/ep_sync_core.h"
#include "allocation_lifetime.h"
#define R7_BACKEND_SAMPLES 16u
static const unsigned r7_jpeg_offsets[12]={0,4,8,0x280,0x800,0x804,0x808,0x80c,0x810,0x814,0x818,0x820};
static const unsigned r7_dma_offsets[32]={0x1300,0x1304,0x1700,0x1704,0x1380,0x1384,0x1780,0x1784,0x1400,0x1404,0x1800,0x1804,0x1480,0x1484,0x1880,0x1884,0x1500,0x1504,0x1900,0x1904,0x1580,0x1584,0x1980,0x1984,0x1600,0x1604,0x1a00,0x1a04,0x1680,0x1684,0x1a80,0x1a84};
struct r7_backend_sample {unsigned kernel_readable,top_readable,rtos_readable,jpeg_readable;unsigned char kernel[EP_KERNEL_STATE_BYTES];uint32_t mutex[3],clock[2],jpeg[12],rtos[5],owner,mask[5],raw[5],otf[2],dma[32];};
/* Global IRQ/OTF activity is context, not an initialization whitelist. */
static unsigned r7_backend_background_activity(const struct r7_backend_sample*s){
 for(unsigned i=0;i<5;i++)if(s->rtos[i]||s->mask[i]||s->raw[i])return 1;
 return s->otf[0]||s->otf[1];
}
static int r7_pa_ref(uint32_t v,uint32_t pa,uint32_t bytes){return pa>=0x94000000u&&bytes&&(uint64_t)v>=pa&&(uint64_t)v<(uint64_t)pa+bytes;}
static int r7_any_old_pa(uint32_t v,uint32_t front,uint32_t back){return r7_pa_ref(v,front,r7_front_bytes)||r7_pa_ref(v,back,r7_backend_bytes);}
/* These three registers are the encoder's latched Y/UV/output configuration.
 * The original encoder reprograms all three before submission; our completed
 * encode also restores the previous configuration after resetting the engine.
 * Their values may therefore name a subsequently freed allocation while idle.
 * Record them, but establish retirement from the allocator, ownership, queues,
 * driver transaction state, inactive engine and stable samples instead. EP DMA roots
 * below retain their old-range rejection. No register is cleared by RESET. */
static unsigned r7_backend_latched_jpeg_refs(const struct r7_backend_sample*s,uint32_t front,uint32_t back){
 unsigned mask=0;if(!s->jpeg_readable)return 0;
 for(unsigned i=8;i<=10;i++)if(r7_any_old_pa(s->jpeg[i],front,back))mask|=1u<<(i-8);
 return mask;
}
static uint32_t r7_backend_dma_refs(const struct r7_backend_sample*s,uint32_t front,uint32_t back){
 uint32_t mask=0;for(unsigned i=0;i<32;i++)if(r7_any_old_pa(s->dma[i],front,back))mask|=1u<<i;return mask;
}
static const char *r7_backend_transaction_error(const struct r7_backend_sample*s){
 if(!s->kernel_readable)return "BACKEND_KERNEL_UNREADABLE";
 const unsigned char*k=s->kernel;uint32_t g=ep_sync_word(k);
 if(g<0xc0008000u||g>=0xe0000000u||(g&3))return "BACKEND_KERNEL_DEVICE_CONTEXT_INVALID";
 if(ep_sync_word(k+4)!=0x20820000u||ep_sync_word(k+8)!=0x1c00u||ep_sync_word(k+60)!=0x2082a000u||ep_sync_word(k+64)!=0x1000u||ep_sync_word(k+68)!=0x2082b000u||ep_sync_word(k+72)!=0x1000u)return "BACKEND_REGISTER_LAYOUT_INVALID";
 if(s->mutex[0]!=1||s->mutex[1]!=EP_KERNEL_MUTEX_VA+4||s->mutex[2]!=EP_KERNEL_MUTEX_VA+4)return "BACKEND_MUTEX_NOT_IDLE";
 for(unsigned i=0;i<5;i++)if(ep_sync_word(k+0x64+i*4))return "BACKEND_KERNEL_INTERRUPT_ENABLED";
 const uint32_t queues[2]={0xc07554e0u,0xc0755504u};for(unsigned q=0;q<2;q++){unsigned off=queues[q]-EP_KERNEL_STATE_VA;if(ep_sync_word(k+off)||ep_sync_word(k+off+4)!=queues[q]+4||ep_sync_word(k+off+8)!=queues[q]+4)return "BACKEND_KERNEL_QUEUE_NOT_EMPTY";}
 if(ep_sync_word(k+0x300))return "BACKEND_KERNEL_ERROR_NONZERO";
 if(!s->top_readable)return "BACKEND_TOP_UNREADABLE";if((s->clock[0]&0x80u)||(s->clock[1]&0x1e0000u))return "BACKEND_JPEG_CLOCK_GATED";
 if(!s->rtos_readable)return "BACKEND_RTOS_STATE_UNREADABLE";if(s->owner)return "BACKEND_RTOS_OWNER_BUSY";
 if(!s->jpeg_readable)return "BACKEND_JPEG_UNREADABLE";if((s->jpeg[0]&3u)||(s->jpeg[4]&3u))return "BACKEND_JPEG_ENGINE_ACTIVE";
 /* The selector and global IRQ/OTF/DMA configuration include native liveview.
  * This verifies retirement of the previous allocations, never permission to
  * render/encode. Both old extents must separately be observed free, roots
  * clear and workers idle in resources(). Fresh worker preflights are intact. */
 return NULL;
}
/* Conservative historical predicate, retained for old-policy regressions. */
static const char *r7_backend_retirement_error(const struct r7_backend_sample*s){
 const char*e=r7_backend_transaction_error(s);
 /* A clocked-off JPEG block is not read. Serialized driver queues/IRQs,
  * mutex and RTOS owner must still prove that no transaction is active. */
 if(e&&!strcmp(e,"BACKEND_JPEG_CLOCK_GATED")){
  if(!s->rtos_readable)return "BACKEND_RTOS_STATE_UNREADABLE";
  if(s->owner)return "BACKEND_RTOS_OWNER_BUSY";
  return NULL;
 }return e;
}
static const char *r7_backend_error(const struct r7_backend_sample*s,uint32_t front,uint32_t back){
 const char *e=r7_backend_transaction_error(s);if(e)return e;
 return r7_backend_dma_refs(s,front,back)?"BACKEND_DMA_OLD_RANGE_OWNER_UNPROVEN":NULL;
}
static const char *r7_backend_lifetime_error(const struct r7_backend_sample*s,const struct r7_allocation_lifetime*m){
 const char *e=r7_backend_transaction_error(s);if(e)return e;
 if(!m->valid)return "ALLOCATION_LAYOUT_UNVERIFIED";
 for(unsigned i=0;i<32;i++)if(r7_any_old_pa(s->dma[i],m->front_pa,m->backend_pa)&&!r7_reused_reference(m,s->dma[i]))return "BACKEND_DMA_ORIGINAL_IDENTITY_UNPROVEN";
 return NULL;
}
static int r7_backend_stable(const struct r7_backend_sample*a,const struct r7_backend_sample*b){
 if(!a->kernel_readable||!b->kernel_readable||!a->top_readable||!b->top_readable||
    !a->rtos_readable||!b->rtos_readable||!a->jpeg_readable||!b->jpeg_readable)return 0;
 /* Only the serialized driver's identity, ownership, queues and activity
  * must remain stable. DMA on any channel may rotate outside the old extents;
  * each sample still checks overlaps. No address/mask/selector whitelist. */
 static const unsigned ranges[][2]={{0,76},{0x64,20},{0x300,4},{0x31c,12},{0x340,12}};
 for(unsigned i=0;i<sizeof ranges/sizeof *ranges;i++)if(memcmp(a->kernel+ranges[i][0],b->kernel+ranges[i][0],ranges[i][1]))return 0;
 if(memcmp(a->mutex,b->mutex,sizeof a->mutex)||a->owner!=b->owner)return 0;
 if((a->clock[0]&0x80u)!=(b->clock[0]&0x80u)||(a->clock[1]&0x1e0000u)!=(b->clock[1]&0x1e0000u))return 0;
 return (a->jpeg[0]&3u)==(b->jpeg[0]&3u)&&(a->jpeg[4]&3u)==(b->jpeg[4]&3u);
}
static int r7_backend_retirement_stable(const struct r7_backend_sample*a,const struct r7_backend_sample*b){
 if(!a->top_readable||!b->top_readable)return 0;
 unsigned ga=(a->clock[0]&0x80u)||(a->clock[1]&0x1e0000u),gb=(b->clock[0]&0x80u)||(b->clock[1]&0x1e0000u);
 if(ga&&gb){
  /* Clock-off means that no JPEG register was read. Compare only the
   * accessible serialized owner/queue state and the gate state itself. */
  if(r7_backend_retirement_error(a)||r7_backend_retirement_error(b))return 0;
  static const unsigned ranges[][2]={{0,76},{0x64,20},{0x300,4},{0x31c,12},{0x340,12}};
  for(unsigned i=0;i<sizeof ranges/sizeof *ranges;i++)if(memcmp(a->kernel+ranges[i][0],b->kernel+ranges[i][0],ranges[i][1]))return 0;
  return !memcmp(a->mutex,b->mutex,sizeof a->mutex)&&a->owner==b->owner&&(a->clock[0]&0x80u)==(b->clock[0]&0x80u)&&(a->clock[1]&0x1e0000u)==(b->clock[1]&0x1e0000u);
 }return !ga&&!gb&&r7_backend_stable(a,b);
}
#endif
