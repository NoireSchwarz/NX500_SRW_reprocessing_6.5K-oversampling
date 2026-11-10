/* SPDX-License-Identifier: AGPL-3.0-only.
 * Two sparse native status vectors, bracketed by the existing frame header.
 * EP C8/CC/D0/D4/D8 are read in the OEM interrupt handlers; ACK registers
 * 50/54/58/5C/60 and the shared high/low timer are never touched here.
 * NX1 reads E0 after selecting B0=2. We NEVER select it: only observe E0
 * if the already-selected B0 is 2 and unchanged. Raw bits stay raw evidence.
 */
#ifndef NXL_VIDEO_PROGRESS_CAPTURE_H
#define NXL_VIDEO_PROGRESS_CAPTURE_H
#define VP_PROGRESS 262144u
#define VPG_MAGIC 0x33504756u
static const uint32_t vpg_address[8]={0x20810418u,0x208210c8u,0x208210ccu,0x208210d0u,0x208210d4u,0x208210d8u,0x2012f138u,0x208210e0u};
static void vp_progress_capture(const struct vp_reader*r,struct vp_record*s,uint64_t(*now)(void*),void*context){
 uint32_t*z=s->progress,after[8],selector=VP_UNKNOWN,selector2=VP_UNKNOWN;
 memset(z,255,sizeof s->progress);z[0]=VPG_MAGIC;z[1]=0;
 if((s->valid&(VP_POLICY|VP_BACKEND|VP_CHANGED))!=(VP_POLICY|VP_BACKEND)||
    !vp_video_flow_mode(s))return;
 uint64_t begin=now(context);int first=r->physical(r->context,VP_LATENCY_DATA,z+2,32);
 int selected=vp_rw(r,0x208210b0u,&selector)&&selector==2u;
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<8;i++){
  if(i==7u&&!selected)continue;
  if(vp_rw(r,vpg_address[i],z+10u+pass*8u+i))z[1]|=1u<<(8u+pass*8u+i);
 }
 int last=r->physical(r->context,VP_LATENCY_DATA,after,32);
 if(last)memcpy(z+26,after+2,16);
 if(selected&&vp_rw(r,0x208210b0u,&selector2)&&selector2==selector){z[30]=selector;z[1]|=4u;}
 else{z[17]=z[25]=VP_UNKNOWN;z[1]&=~((1u<<15)|(1u<<23));z[30]=VP_UNKNOWN;}
 if(first&&last&&z[2]==0x33544c56u&&z[3]==4u){
  z[1]|=16u;s->valid|=VP_PROGRESS;
  if(!memcmp(z+2,after,32))z[1]|=1u;else z[1]|=8u;
  if((z[5]&15u)==3u&&((z[5]>>24)&3u)==0u&&((z[5]>>16)&255u)==1u)z[1]|=2u;
 }
 uint64_t end=now(context);z[31]=end>=begin&&end-begin<=UINT32_MAX*1000ULL?(uint32_t)((end-begin)/1000u):VP_UNKNOWN;
}
#endif
