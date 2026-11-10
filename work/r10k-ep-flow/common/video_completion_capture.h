/* SPDX-License-Identifier: AGPL-3.0-only. R10h sparse, read-only IPC status.
 * Native IRQ44 reads 20810418; ACK is a separate write at 20810440.
 * Never read/write the ACK and never sample the shared high/low timer here.
 * A zero status is only a missed pending observation, NOT proof of DDR wait.
 */
#ifndef NXL_VIDEO_COMPLETION_CAPTURE_H
#define NXL_VIDEO_COMPLETION_CAPTURE_H
#define VP_COMPLETION 131072u
#define VC_MAGIC 0x31435056u
#define VC_STATUS 0x20810418u
#define VC_ENABLE 0x20810468u
#define VC_HANDLER 0x813f09bcu
enum {VC_READ=1u,VC_STABLE=2u,VC_INFLIGHT=4u,VC_PENDING=8u,
 VC_ENABLED=16u,VC_HANDLER_OK=32u,VC_PERSISTED=64u,VC_CHANGED=128u,
 VC_ERROR=256u,VC_IRQ=512u,VC_CALLBACK=1024u};
struct vp_completion {
 uint32_t magic,flags,before[8],status_first,enable,handler,after[8];
 uint64_t first_ns,last_ns;
 uint32_t status_last,followups,pending_hits,pending_lower_ns,phase,result,governor;
} __attribute__((packed,aligned(4)));
typedef uint64_t(*vc_now_fn)(void*);
typedef void(*vc_wait_fn)(void*);
static int vc_pair(const struct vp_reader*r,struct vp_completion*z){
 return r->physical(r->context,VP_LATENCY_DATA,z->before,32)&&
  vp_rw(r,VC_STATUS,&z->status_first)&&vp_rw(r,VC_ENABLE,&z->enable)&&
  vp_rw(r,VC_HANDLER,&z->handler)&&
  r->physical(r->context,VP_LATENCY_DATA,z->after,32);
}
static int vc_same_request(const uint32_t*a,const uint32_t*b){
 return a[0]==VP_LATENCY_MAGIC&&a[1]==4u&&b[0]==a[0]&&b[1]==a[1]&&
  a[2]==b[2]&&a[4]==b[4]&&a[5]==b[5];
}
static int vc_inflight(const uint32_t*a){
 return (a[3]&15u)==3u&&((a[3]>>24)&3u)==0u&&((a[3]>>16)&255u)==1u;
}
static void vp_completion_capture(const struct vp_reader*r,struct vp_record*s,
 vc_now_fn now,vc_wait_fn wait,void*clock_context,uint32_t phase,uint32_t result,uint32_t governor){
 struct vp_completion z;memset(&z,0,sizeof z);z.magic=VC_MAGIC;
 z.phase=phase;z.result=result;z.governor=governor;
 /* Full-width backend only. Failed/changed observations retain explicit flags. */
 if((s->valid&(VP_POLICY|VP_BACKEND|VP_LATENCY|VP_CHANGED))!=(VP_POLICY|VP_BACKEND|VP_LATENCY)||
  !vp_video_flow_mode(s))return;
 s->valid|=VP_COMPLETION;
 uint64_t start=now(clock_context);int ok=vc_pair(r,&z);uint64_t end=now(clock_context);
 z.first_ns=end;z.last_ns=start;z.status_last=z.status_first;
 if(!ok){z.flags=VC_ERROR;goto publish;}
 z.flags=VC_READ;
 if(!vc_same_request(z.before,z.after)||memcmp(z.before,z.after,32)){z.flags|=VC_CHANGED;goto publish;}
 z.flags|=VC_STABLE;
 if(z.enable&4u)z.flags|=VC_ENABLED;
 if(z.handler==0x801cdf6cu)z.flags|=VC_HANDLER_OK;
 if(z.before[3]&4u)z.flags|=VC_IRQ;
 if(z.before[3]&8u)z.flags|=VC_CALLBACK;
 if(!vc_inflight(z.before))goto publish;
 z.flags|=VC_INFLIGHT;
 if(!(z.status_first&4u))goto publish;
 z.flags|=VC_PENDING;z.pending_hits=1;
 if((z.flags&(VC_ENABLED|VC_HANDLER_OK))!=(VC_ENABLED|VC_HANDLER_OK))goto publish;
 /* At most two extra pairs, only after a positive pending observation.
  * Sleep rather than spin; scheduler delay is measured, never called HW time.
  * Header equality brackets each status read, including the final read. */
 for(unsigned i=0;i<2;i++){
  if(now(clock_context)-end>=2000000u)break;
  wait(clock_context);uint64_t began=now(clock_context);struct vp_completion q;memset(&q,0,sizeof q);
  int read_ok=vc_pair(r,&q);(void)now(clock_context);z.followups++;
  z.status_last=q.status_first;memcpy(z.after,q.after,sizeof z.after);
  if(!read_ok){z.flags|=VC_ERROR;break;}
  if(!vc_same_request(z.before,q.before)||!vc_same_request(z.before,q.after)||
   memcmp(q.before,q.after,32)||memcmp(z.before,q.before,32)){
   z.flags|=VC_CHANGED;break;
  }
  if(!(q.enable&4u)||q.handler!=z.handler){z.flags|=VC_CHANGED;break;}
  if(!(q.status_first&4u))break;
  z.pending_hits++;z.flags|=VC_PERSISTED;z.last_ns=began;
  uint64_t span=began>end?began-end:0;z.pending_lower_ns=span>0xffffffffu?0xffffffffu:(uint32_t)span;
 }
publish:memcpy(s->completion,&z,sizeof z);
}
_Static_assert(sizeof(struct vp_completion)==128,"sparse hardware status record");
#endif
