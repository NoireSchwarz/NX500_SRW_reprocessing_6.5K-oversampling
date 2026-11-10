/* SPDX-License-Identifier: AGPL-3.0-only; read-only hardware observation API. */
#ifndef NXL_H3DL_PROBE_H
#define NXL_H3DL_PROBE_H
#include <stdint.h>
#include <string.h>
#include "ep_sync_core.h"
#include "h3dl_offsets.h"
#define H3DL_SAMPLES 16u
#define H3DL_MAGIC 0x39524433u
#define H3DL_RTOS_IRQ_BASE 0x817d8aacu
#define H3DL_RTOS_STATE_BASE 0x817d6b48u
#define H3DL_RTOS_IRQ_WORDS 136u
enum h3dl_domain {H3_TOP,H3_LUT,H3_KERNEL,H3_RTOS};
enum h3dl_flags {H3_CLOCK_GATED=1,H3_LINUX_ACC7_ENABLED=2,H3_RTOS_ACC7_ENABLED=4,H3_ACCESS_MODE=8,H3_REGISTER_CHANGED=16};
struct h3dl_sample {
 uint32_t elapsed_ms,clock_on;
 uint32_t top[H3DL_TOP_COUNT],lut[5],kernel_mutex[3],kernel_raw[341],rtos_irq[H3DL_RTOS_IRQ_WORDS],rtos_state[5];
};
struct h3dl_capture {uint32_t magic,version,count,sample_bytes,top_count,kernel_bytes,rtos_irq_base,rtos_state_base;struct h3dl_sample sample[H3DL_SAMPLES];};
typedef int (*h3dl_reader)(void*,enum h3dl_domain,uint32_t,void*,uint32_t);
static uint32_t h3dl_top_value(const struct h3dl_sample *s,uint32_t offset){for(unsigned i=0;i<H3DL_TOP_COUNT;i++)if(h3dl_top_offsets[i]==offset)return s->top[i];return 0;}
static void h3dl_init(struct h3dl_capture *c){memset(c,0,sizeof *c);c->magic=H3DL_MAGIC;c->version=1;c->sample_bytes=sizeof(struct h3dl_sample);c->top_count=H3DL_TOP_COUNT;c->kernel_bytes=EP_KERNEL_STATE_BYTES;c->rtos_irq_base=H3DL_RTOS_IRQ_BASE;c->rtos_state_base=H3DL_RTOS_STATE_BASE;}
static int h3dl_capture_one(struct h3dl_capture *c,uint32_t elapsed,void *context,h3dl_reader read){
 if(c->count>=H3DL_SAMPLES)return 0;
 struct h3dl_sample *s=c->sample+c->count;s->elapsed_ms=elapsed;
 for(unsigned i=0;i<H3DL_TOP_COUNT;i++)if(!read(context,H3_TOP,h3dl_top_offsets[i],s->top+i,4))return 0;
 uint32_t clock=0,clock2=0;if(!read(context,H3_TOP,0x1020,&clock,4)||!read(context,H3_TOP,0x1024,&clock2,4))return 0;
 s->clock_on=!((h3dl_top_value(s,0x1020)|h3dl_top_value(s,0x1024)|clock|clock2)&0x10);
 if(s->clock_on)for(unsigned i=0;i<5;i++)if(!read(context,H3_LUT,i*4,s->lut+i,4))return 0;
 if(!read(context,H3_KERNEL,EP_KERNEL_MUTEX_VA,s->kernel_mutex,12)||!read(context,H3_KERNEL,EP_KERNEL_STATE_VA,s->kernel_raw,EP_KERNEL_STATE_BYTES)||
    !read(context,H3_RTOS,H3DL_RTOS_IRQ_BASE,s->rtos_irq,sizeof s->rtos_irq)||!read(context,H3_RTOS,H3DL_RTOS_STATE_BASE,s->rtos_state,sizeof s->rtos_state))return 0;
 c->count++;return 1;
}
static int h3dl_capture_valid(const struct h3dl_capture *c){
 if(c->magic!=H3DL_MAGIC||c->version!=1||c->count!=H3DL_SAMPLES||c->sample_bytes!=sizeof(struct h3dl_sample)||c->top_count!=H3DL_TOP_COUNT||c->kernel_bytes!=EP_KERNEL_STATE_BYTES||c->rtos_irq_base!=H3DL_RTOS_IRQ_BASE||c->rtos_state_base!=H3DL_RTOS_STATE_BASE)return 0;
 for(unsigned i=0;i<H3DL_SAMPLES;i++){
  const struct h3dl_sample *s=c->sample+i;
  if(s->clock_on>1||(i&&s->elapsed_ms<=c->sample[i-1].elapsed_ms))return 0;
  if(!s->clock_on)for(unsigned j=0;j<5;j++)if(s->lut[j])return 0;
  struct ep_sync_observation o;ep_sync_decode((const unsigned char*)s->kernel_raw,s->kernel_mutex,&o);
  if(ep_sync_observation_error(&o))return 0;
  const unsigned char *raw=(const unsigned char*)s->kernel_raw;
  if(ep_sync_word(raw+4+8*8)!=0x2082b000u||ep_sync_word(raw+8+8*8)!=0x1000u)return 0;
 }
 return 1;
}
static uint32_t h3dl_capture_flags(const struct h3dl_capture *c){uint32_t flags=0;
 for(unsigned i=0;i<c->count&&i<H3DL_SAMPLES;i++){
  const struct h3dl_sample *s=c->sample+i;const unsigned char *raw=(const unsigned char*)s->kernel_raw;
  if(!s->clock_on)flags|=H3_CLOCK_GATED;
  if(ep_sync_word(raw+EP_KERNEL_ACC_MASK_VA-EP_KERNEL_STATE_VA)&0x80)flags|=H3_LINUX_ACC7_ENABLED;
  if(s->rtos_irq[4]&0x80)flags|=H3_RTOS_ACC7_ENABLED;
  if(s->clock_on&&(s->lut[2]&1))flags|=H3_ACCESS_MODE;
  if(i&&(memcmp(s->top,c->sample[i-1].top,sizeof s->top)||s->clock_on!=c->sample[i-1].clock_on||memcmp(s->lut,c->sample[i-1].lut,sizeof s->lut)))flags|=H3_REGISTER_CHANGED;
 }
 return flags;
}
#endif
