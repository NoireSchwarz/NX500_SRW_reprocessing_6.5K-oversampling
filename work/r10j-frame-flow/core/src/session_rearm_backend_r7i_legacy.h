/* SPDX-License-Identifier: AGPL-3.0-only. Pure retirement, not quantizer reset.
 * EP DMA/register pointer values are physical addresses. Linux userspace VA
 * aliases are deliberately not compared with these identity physical roots. */
#ifndef NX51_REARM_BACKEND_H
#define NX51_REARM_BACKEND_H
#include <stdint.h>
#include <string.h>
#include "../../hardware/src/ep_sync_core.h"
#define R7_BACKEND_SAMPLES 16u
static const unsigned r7_jpeg_offsets[12]={0,4,8,0x280,0x800,0x804,0x808,0x80c,0x810,0x814,0x818,0x820};
static const unsigned r7_dma_offsets[32]={0x1300,0x1304,0x1700,0x1704,0x1380,0x1384,0x1780,0x1784,0x1400,0x1404,0x1800,0x1804,0x1480,0x1484,0x1880,0x1884,0x1500,0x1504,0x1900,0x1904,0x1580,0x1584,0x1980,0x1984,0x1600,0x1604,0x1a00,0x1a04,0x1680,0x1684,0x1a80,0x1a84};
struct r7_backend_sample {unsigned kernel_readable,top_readable,rtos_readable,jpeg_readable;unsigned char kernel[EP_KERNEL_STATE_BYTES];uint32_t mutex[3],clock[2],jpeg[12],rtos[5],owner,mask[5],raw[5],otf[2],dma[32];};
/* This is a retirement-only profile observed after the user's restart.
 * Kernel selector 0 is the pre-JPEG-init state. The original selector ioctl
 * at c02cdb18 explicitly stores 3/5; those values authorize a later encode
 * preflight, not proof that an old allocation has retired.
 *
 * Preserve one exact known liveview IRQ/OTF profile instead of treating every
 * EP channel as our JPEG task. The encoder's ACC2/5 bits (0x24 at TOP+1080,
 * confirmed by original kernel c02cc720) remain disabled in this profile.
 * Any other profile is still refused by the original quiet-path checks. */
static int r7_backend_preview_profile(const struct r7_backend_sample*s){
 static const uint32_t preview[5]={0x401u,0,0,0,0x80u};
 if(!s->kernel_readable||!s->top_readable||!s->rtos_readable||!s->jpeg_readable||
    ep_sync_word(s->kernel+0x400)||s->owner||s->otf[0]!=1||s->otf[1]||
    (s->jpeg[0]&3u)||(s->jpeg[4]&3u))return 0;
 for(unsigned i=0;i<5;i++)if(s->rtos[i]!=preview[i]||s->mask[i]!=preview[i]||s->raw[i])return 0;
 return 1;
}
static int r7_pa_ref(uint32_t v,uint32_t pa,uint32_t bytes){return (uint64_t)v>=pa&&(uint64_t)v<(uint64_t)pa+bytes;}
static int r7_any_old_pa(uint32_t v,uint32_t front,uint32_t back){return r7_pa_ref(v,front,134217728u)||r7_pa_ref(v,back,201326592u);}
/* These three registers are the encoder's latched Y/UV/output configuration.
 * The original encoder reprograms all three before submission; our completed
 * encode also restores the previous configuration after resetting the engine.
 * Their values may therefore name a subsequently freed allocation while idle.
 * Record them, but establish retirement from the allocator, ownership, queues,
 * disabled IRQs, inactive engine and stable samples instead. Live EP DMA roots
 * below retain their old-range rejection. No register is cleared by RESET. */
static unsigned r7_backend_latched_jpeg_refs(const struct r7_backend_sample*s,uint32_t front,uint32_t back){
 unsigned mask=0;if(!s->jpeg_readable)return 0;
 for(unsigned i=8;i<=10;i++)if(r7_any_old_pa(s->jpeg[i],front,back))mask|=1u<<(i-8);
 return mask;
}
static const char *r7_backend_error(const struct r7_backend_sample*s,uint32_t front,uint32_t back){
 if(!s->kernel_readable)return "BACKEND_KERNEL_UNREADABLE";
 const unsigned char*k=s->kernel;uint32_t g=ep_sync_word(k);
 if(g<0xc0008000u||g>=0xe0000000u||(g&3))return "BACKEND_KERNEL_DEVICE_CONTEXT_INVALID";
 if(ep_sync_word(k+4)!=0x20820000u||ep_sync_word(k+8)!=0x1c00u||ep_sync_word(k+60)!=0x2082a000u||ep_sync_word(k+64)!=0x1000u||ep_sync_word(k+68)!=0x2082b000u||ep_sync_word(k+72)!=0x1000u)return "BACKEND_REGISTER_LAYOUT_INVALID";
 if(s->mutex[0]!=1||s->mutex[1]!=EP_KERNEL_MUTEX_VA+4||s->mutex[2]!=EP_KERNEL_MUTEX_VA+4)return "BACKEND_MUTEX_NOT_IDLE";
 for(unsigned i=0;i<5;i++)if(ep_sync_word(k+0x64+i*4))return "BACKEND_KERNEL_INTERRUPT_ENABLED";
 const uint32_t queues[2]={0xc07554e0u,0xc0755504u};for(unsigned q=0;q<2;q++){unsigned off=queues[q]-EP_KERNEL_STATE_VA;if(ep_sync_word(k+off)||ep_sync_word(k+off+4)!=queues[q]+4||ep_sync_word(k+off+8)!=queues[q]+4)return "BACKEND_KERNEL_QUEUE_NOT_EMPTY";}
 if(ep_sync_word(k+0x300))return "BACKEND_KERNEL_ERROR_NONZERO";uint32_t selector=ep_sync_word(k+0x400);
 unsigned preview=r7_backend_preview_profile(s);
 if(selector!=3&&selector!=5&&!preview)return "BACKEND_KERNEL_SELECTOR_UNPROVEN";
 if(!s->top_readable)return "BACKEND_TOP_UNREADABLE";if((s->clock[0]&0x80u)||(s->clock[1]&0x1e0000u))return "BACKEND_JPEG_CLOCK_GATED";
 if(!s->rtos_readable)return "BACKEND_RTOS_STATE_UNREADABLE";if(s->owner)return "BACKEND_RTOS_OWNER_BUSY";
 if(!preview){
  for(unsigned i=0;i<5;i++)if(s->rtos[i]||s->mask[i]||s->raw[i])return "BACKEND_IRQ_NOT_QUIET";
  if(s->otf[0]||s->otf[1])return "BACKEND_OTF_ACTIVE";
 }
 for(unsigned i=0;i<32;i++)if(r7_any_old_pa(s->dma[i],front,back))return "BACKEND_DMA_REFERENCES_OLD_PA";
 if(!s->jpeg_readable)return "BACKEND_JPEG_UNREADABLE";if((s->jpeg[0]&3u)||(s->jpeg[4]&3u))return "BACKEND_JPEG_ENGINE_ACTIVE";
 /* Inactive JPEG configuration is not an outstanding DMA transaction. Its
  * exact values still participate in the multi-sample stability check. */
 if(preview){
  static const unsigned rotating[4]={18,19,26,27};
  for(unsigned i=0;i<4;i++){uint32_t p=s->dma[rotating[i]];if(p<0x94000000u||p>=0xc0000000u||(p&63u))return "BACKEND_PREVIEW_DMA_LAYOUT_UNPROVEN";}
 }
 return NULL;
}
static int r7_backend_stable(const struct r7_backend_sample*a,const struct r7_backend_sample*b){
 if(!r7_backend_preview_profile(a)||!r7_backend_preview_profile(b))return !memcmp(a,b,sizeof *a);
 /* Only the four observed preview write-buffer registers may rotate.
  * Every sample separately rejects a pointer into either old allocation;
  * kernel state, JPEG state, masks and all other DMA words stay exact. */
 struct r7_backend_sample first=*a,next=*b;
 static const unsigned rotating[4]={18,19,26,27};
 for(unsigned i=0;i<4;i++)first.dma[rotating[i]]=next.dma[rotating[i]]=0;
 return !memcmp(&first,&next,sizeof first);
}
#endif
