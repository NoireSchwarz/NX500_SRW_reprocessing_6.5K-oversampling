/* SPDX-License-Identifier: AGPL-3.0-only. Sparse independent input-DMA binding.
 * Reuses the128-byte optional EP-readback field; ABI/allocation stay fixed.
 * Read-only: no ACK, enable, timer-latch or target function calls.
 */
#ifndef NXL_VIDEO_IPC_LINK_CAPTURE_H
#define NXL_VIDEO_IPC_LINK_CAPTURE_H
#define VP_IPC_LINK 2097152u
#define VP_IPC_PAIR 4194304u
#define VPU_MAGIC 0x32555049u
enum {VPU_PARENT,VPU_STATE,VPU_BACKEND,VPU_BACKEND_TYPE,VPU_DRIVER,VPU_PRESET,
 VPU_INPUT,VPU_INPUT_TYPE,VPU_INPUT_PA,VPU_RDMA_PA,VPU_OUTPUT,VPU_OUTPUT_TYPE,VPU_FIELDS};
#define VPU_ALL 0xfffu
static uint32_t vpu_vector(const struct vp_reader*r,uint32_t*v){
 uint32_t mask=0;for(unsigned i=0;i<VPU_FIELDS;i++)v[i]=VP_UNKNOWN;
 const uint32_t at[12]={0x8153cbecu,0x8153d910u,0x8153d964u,0,0x8146e438u,0x813ebf60u,0x8153d930u,0,0,0x20810908u,0x8153d938u,0};
 for(unsigned i=0;i<VPU_FIELDS;i++){
  uint32_t address=at[i];
  if(i==VPU_BACKEND_TYPE){if(!(mask&(1u<<VPU_BACKEND))||!vp_path_ram(v[VPU_BACKEND],4))continue;address=v[VPU_BACKEND];}
  if(i==VPU_INPUT_TYPE||i==VPU_INPUT_PA){if(!(mask&(1u<<VPU_INPUT))||!vp_path_ram(v[VPU_INPUT],0x70))continue;address=v[VPU_INPUT]+(i==VPU_INPUT_PA?0x6cu:0u);}
  if(i==VPU_OUTPUT_TYPE){if(!(mask&(1u<<VPU_OUTPUT))||!vp_path_ram(v[VPU_OUTPUT],4))continue;address=v[VPU_OUTPUT];}
  uint32_t value;if(vp_rw(r,address,&value)){v[i]=value;mask|=1u<<i;}
 }return mask;
}
static int vpu_types(const uint32_t*a){return a[VPU_PARENT]==0x80592110u&&(a[VPU_STATE]&255u)==3u&&a[VPU_INPUT_TYPE]==0x80588910u&&a[VPU_OUTPUT_TYPE]==0x80588910u&&vp_path_ram(a[VPU_INPUT],0x70)&&vp_path_ram(a[VPU_OUTPUT],4)&&a[VPU_INPUT]!=a[VPU_OUTPUT];}
static void vp_ipc_link_capture(const struct vp_reader*r,struct vp_record*s,uint64_t(*now)(void*),void*c){
 uint32_t*w=s->ep_readback,*a=s->progress,*b=a+16;
 memset(w,255,sizeof s->ep_readback);w[0]=VPU_MAGIC;w[1]=0;w[2]=0;w[31]=0;
 if(!vp_video_flow_mode(s)||s->rtos[0]!=0x1717u||s->rtos[2]!=0xe005u||
   (s->valid&(VP_POLICY|VP_BACKEND|VP_SOURCE|VP_CHANGED))!=(VP_POLICY|VP_BACKEND|VP_SOURCE))return;
 s->valid=(s->valid&~262144u)|VP_IPC_LINK|VP_IPC_PAIR;
 memset(a,255,128);w[1]|=1u<<24;uint64_t begin=now(c);
 int ar=r->physical(r->context,VP_LATENCY_DATA,a,64);
 if(ar)w[1]|=1u<<25;else memset(a,255,64);
 uint32_t ma=vpu_vector(r,w+4),value;
 if(vp_rw(r,0x813f09ccu,&value)){w[28]=value;w[31]|=1u;}
 if(vp_rw(r,0x20810468u,&value)){w[3]=value;w[31]|=2u;}
 uint32_t mb=vpu_vector(r,w+16);w[1]|=ma|(mb<<12);
 if(vp_rw(r,0x813f09ccu,&value)){w[29]=value;w[31]|=4u;}
 if(vp_rw(r,0x20810468u,&value)){w[30]=value;w[31]|=8u;}
 int br=r->physical(r->context,VP_LATENCY_DATA,b,64);
 if(br)w[1]|=1u<<26;else memset(b,255,64);
 if(ar&&br&&vp_timer_schema(a)==11u&&!memcmp(a,b,64))w[1]|=1u<<27;
 if(ma==VPU_ALL&&mb==VPU_ALL&&!memcmp(w+4,w+16,VPU_FIELDS*4))w[1]|=1u<<28;
 if(ma==VPU_ALL&&mb==VPU_ALL&&vpu_types(w+4)&&vpu_types(w+16))w[1]|=1u<<29;
 if((ma&mb&(1u<<VPU_BACKEND_TYPE))&&w[4+VPU_BACKEND_TYPE]==s->hevc[9]&&w[16+VPU_BACKEND_TYPE]==s->hevc[9]&&(s->hevc[9]==0x80592268u||s->hevc[9]==0x80592328u))w[1]|=1u<<30;
 if((w[1]&0x7fffffffu)==0x7fffffffu&&vp_timer_inflight(a)&&(a[3]&512u)&&
    w[4+VPU_DRIVER]==s->rtos[0]&&w[4+VPU_PRESET]==s->rtos[2]&&
    w[4+VPU_INPUT_PA]&&w[4+VPU_INPUT_PA]==w[4+VPU_RDMA_PA]&&
    a[9]==w[4+VPU_INPUT]&&a[10]==w[4+VPU_INPUT_PA]&&a[11]==w[4+VPU_OUTPUT])w[1]|=1u<<31;
 uint64_t end=now(c);w[2]=end>=begin&&end-begin<=UINT32_MAX*1000ULL?(uint32_t)((end-begin)/1000u):VP_UNKNOWN;
}
#endif
