/* SPDX-License-Identifier: AGPL-3.0-only. Fixed-size view of the leased TMC RAM.
 * The RTOS producer cleans each completed record to physical RAM. No target
 * writes, calls, queues or per-frame polling occur on the Linux reader. */
#ifndef NXL_VIDEO_LATENCY_CAPTURE_H
#define NXL_VIDEO_LATENCY_CAPTURE_H
#define VP_LATENCY 65536u
#define VP_LATENCY_DATA 0x817dbbe0u
#define VP_LATENCY_BYTES 256u
#define VP_LATENCY_MAGIC 0x33544c56u
/* Schemas4/6/7/8/9 share the eight-word request header. Schema5 differs. */
static unsigned vp_timer_schema(const uint32_t*a){
 if(a[0]==0x33544c56u&&a[1]==4u)return 4u;
 if(a[0]==0x35544c56u&&a[1]==5u)return 5u;
 if(a[0]==0x36544c56u&&a[1]==6u)return 6u;
 if(a[0]==0x37544c56u&&a[1]==7u)return 7u;
 if(a[0]==0x38544c56u&&a[1]==8u)return 8u;
 if(a[0]==0x39544c56u&&a[1]==9u)return 9u;
 if(a[0]==0x41544c56u&&a[1]==10u)return 10u;
 if(a[0]==0x42544c56u&&a[1]==11u)return 11u;
 return 0;
}
static int vp_timer_common_header(const uint32_t*a){unsigned s=vp_timer_schema(a);return s&&s!=5u;}
/* Schema9 keeps HW/IRQ repeat flags instead of exact low-priority counts. */
static int vp_timer_inflight(const uint32_t*a){
 return (a[3]&15u)==3u&&((a[3]>>24)&3u)==0u&&
  ((vp_timer_schema(a)==9u||vp_timer_schema(a)==10u||vp_timer_schema(a)==11u)?!(a[3]&0x10000u):((a[3]>>16)&255u)==1u);
}
static int vp_latency_capture(const struct vp_reader*r,struct vp_record*s){
 uint32_t a[64],b[8];memset(s->latency,0,sizeof s->latency);
 if(!r->physical(r->context,VP_LATENCY_DATA,a,sizeof a)||!r->physical(r->context,VP_LATENCY_DATA,b,sizeof b)||memcmp(a,b,sizeof b)||
    !vp_timer_schema(a))return 0;
 /* Sequence is published after the ring entry. An in-flight slot is zero
  * and is discarded by the decoder; sequence gaps remain explicit. */
 memcpy(s->latency,a,sizeof a);s->valid|=VP_LATENCY;return 1;
}
#endif
