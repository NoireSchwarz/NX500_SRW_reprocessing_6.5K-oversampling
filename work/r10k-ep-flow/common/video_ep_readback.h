/* SPDX-License-Identifier: AGPL-3.0-only. R10k auxiliary EP readback evidence.
 * Exact original EP RDMA/WDMA configuration words, never ACK, enables, clocks,
 * debug selection or frame pixels. IPC header and mode bracket each sample.
 * A configured address/stride is not proof of an actual DMA transfer.
 */
#ifndef NXL_VIDEO_EP_READBACK_H
#define NXL_VIDEO_EP_READBACK_H
#include "video_progress_capture.h"
#define VP_EP_READBACK 1048576u
#define VPE_MAGIC 0x31525045u
static const uint32_t vpe_base[4]={0x20821400u,0x20821800u,0x20821480u,0x20821880u};
static int vp_ep_readback_range(uint32_t a,unsigned n){
 if(n!=20u)return 0;
 for(unsigned i=0;i<4;i++)if(a==vpe_base[i])return 1;
 return 0;
}
static void vp_ep_readback_capture(const struct vp_reader*r,struct vp_record*s,
 uint64_t(*now)(void*),void*context){
 uint32_t*z=s->ep_readback,before[8],after[8],a[5],b[5],mode=0,preset=0;
 memset(z,255,sizeof s->ep_readback);z[0]=VPE_MAGIC;z[1]=0;
 if(!vp_video_flow_mode(s)||(s->valid&VP_CHANGED)||
    (s->valid&(VP_POLICY|VP_BACKEND))!=(VP_POLICY|VP_BACKEND))return;
 uint64_t start=now(context);
 if(r->physical(r->context,VP_LATENCY_DATA,before,sizeof before)&&
    before[0]==VP_LATENCY_MAGIC&&before[1]==4u){z[1]|=256u;z[2]=before[2];z[3]=before[3];}
 for(unsigned group=0;group<4;group++){
  if(!r->physical(r->context,vpe_base[group],a,sizeof a)||
     !r->physical(r->context,vpe_base[group],b,sizeof b))continue;
  z[1]|=1u<<group;memcpy(z+6+group*5,a,sizeof a);
  for(unsigned field=0;field<5;field++)if(a[field]!=b[field]){
   z[1]|=(1u<<(4+group))|(1u<<(12+group*5+field));
  }
 }
 if(r->physical(r->context,VP_LATENCY_DATA,after,sizeof after)&&
    after[0]==VP_LATENCY_MAGIC&&after[1]==4u){
  z[1]|=512u;z[4]=after[2];z[5]=after[3];
  if((z[1]&256u)&&!memcmp(before,after,sizeof before))z[1]|=1024u;
 }
 if(vp_rw(r,0x8146e438u,&mode)&&vp_rw(r,0x813ebf60u,&preset)&&
    mode==s->rtos[0]&&preset==s->rtos[2])z[1]|=2048u;
 /* Preserve already-observed status, without another device read. These
  * belong to the earlier progress vector, not the new DMA sample instant. */
 if((s->valid&VP_PROGRESS)&&(s->progress[1]&(0x3eu<<16))==(0x3eu<<16))
  memcpy(z+26,s->progress+19,20);
 uint64_t end=now(context);
 z[31]=end>=start&&end-start<=UINT32_MAX*1000ULL?(uint32_t)((end-start)/1000u):VP_UNKNOWN;
 s->valid|=VP_EP_READBACK;
}
#endif
