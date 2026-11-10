/* SPDX-License-Identifier: AGPL-3.0-only. Passive NX5001.12 RAM snapshots.
 * IRQ totals retain their native meanings: they are NOT renamed frame counts.
 * Mutable objects are bracketed by root/type checks. No pointer is executed.
 */
#ifndef NXL_VIDEO_COUNTER_CAPTURE_H
#define NXL_VIDEO_COUNTER_CAPTURE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define VP_PERIOD_MS 200u
#define VP_MAX_SAMPLES 6000u
#define VP_HEADER_BYTES 16384u
#define VP_UNKNOWN 0xffffffffu
enum {VP_RTOS=1u,VP_PP=2u,VP_POLICY=4u,VP_CAMERA=8u,VP_SOURCE=16u,VP_ENCODER=32u,VP_HEVC=64u,VP_ROUTE=128u,VP_QUEUE0=256u,VP_BACKEND=16384u,VP_SELECTOR=32768u,VP_CHANGED=0x80000000u};
enum {VP_IDLE=0,VP_RUNNING=1,VP_STOPPING=2,VP_FULL=3,VP_USB=4,VP_CAMERA_GONE=5,VP_SAVED=6,VP_FAILED=7};
static const uint32_t vp_irq_ids[16]={38,39,44,45,46,47,48,49,50,54,55,142,143,153,154,158};
static const uint32_t vp_pp_addresses[4]={0x8154160c,0x815415c8,0x815415a4,0x81541580};
struct vp_record {
 uint64_t begin_ns,end_ns,cpu_ns;
 uint32_t sequence,valid,epoch,period_ms,read_calls,read_bytes,mark,control;
 uint32_t irq[16],pp[4],rtos[4],policy[9];
 uint32_t camera[20],source[6],encoder[8],hevc[10],queue[5][6],reserved[6],latency[64],completion[32],progress[32],flow[32],ep_readback[32],commit;
};
struct vp_header {
 char magic[8];uint32_t schema,header_bytes,record_bytes,capacity,count,phase;
 uint64_t started_ns,finished_ns,cpu_started_ns,cpu_finished_ns,camera_ticks,worker_ticks;
 uint32_t pid,camera_pid,modules_base,core_base,osal_base,cookie,period_ms,reason;
 uint32_t stop_request,mark_request,control_cookie,control_reserved;
 uint64_t max_sample_ns,total_sample_ns,deadline_misses,published_ns;
 char report[256],buffer[160],revision[32];
 uint32_t header_hash;
};
typedef int(*vp_read_fn)(void*,uint32_t,void*,unsigned);
struct vp_reader {void*context;vp_read_fn physical,process;uint32_t modules,core;};
static uint32_t vp_hash(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int vp_user(uint32_t p,unsigned n){return !(p&3u)&&p>=0x10000u&&(uint64_t)p+n<=0xbfff0000ULL;}
static int vp_pw(const struct vp_reader*r,uint32_t a,uint32_t*out){return vp_user(a,4)&&r->process(r->context,a,out,4);}
static int vp_rw(const struct vp_reader*r,uint32_t a,uint32_t*out){return r->physical(r->context,a,out,4);}
static int vp_block(const struct vp_reader*r,uint32_t p,void*out,unsigned n){return vp_user(p,n)&&r->process(r->context,p,out,n);}
static int vp_type(const struct vp_reader*r,uint32_t p,uint32_t type){uint32_t v;return vp_pw(r,p,&v)&&v==type;}
static int vp_queue_count(const struct vp_reader*r,uint32_t p,uint32_t*out,uint32_t*capacity){
 uint32_t body,again,v[7];if(!vp_pw(r,p,&body)||!vp_block(r,body,v,sizeof v)||!vp_pw(r,p,&again)||body!=again||!v[6]||v[6]>1024u||v[5]>v[6])return 0;
 *out=v[5];*capacity=v[6];return 1;
}
static int vp_queue_capture(const struct vp_reader*r,uint32_t p,uint32_t out[6]){
 uint32_t b[17],free_at,full_at,type,fc,ff,again;
 if(!vp_block(r,p,b,sizeof b))return 0;
 if(b[0]==r->core+0x59288u){type=1;free_at=0x24;full_at=0x28;}
 else if(b[0]==r->core+0x593b0u){type=2;free_at=0x24;full_at=0x28;}
 else if(b[0]==r->core+0x592f8u){type=3;free_at=0x20;full_at=0x24;}
 else return 0;
 uint32_t free_count,full_count;
 if(!vp_queue_count(r,b[free_at/4],&free_count,&ff)||!vp_queue_count(r,b[full_at/4],&full_count,&fc)||ff!=fc||!vp_pw(r,p,&again)||again!=b[0])return 0;
 out[0]=p;out[1]=type;out[2]=full_count;out[3]=free_count;out[4]=fc;
 /* Queue counts are not locked by the observer. A concurrent handoff may
  * make free+full exceed capacity transiently: preserve and flag it. */
 out[5]=1u|(free_count+full_count>fc?2u:0u);return 1;
}
static void vp_port_queues(const struct vp_reader*r,uint32_t object,uint32_t out[3]){
 uint32_t h[2],ports[6],again[2];memset(out,0,12);
 if(!vp_block(r,object+0x2c,h,sizeof h)||!h[0]||!h[1]||h[1]>3u||!vp_block(r,h[0],ports,h[1]*8u)||!vp_block(r,object+0x2c,again,sizeof again)||memcmp(h,again,sizeof h))return;
 for(unsigned i=0;i<h[1];i++)out[i]=ports[2*i+1];
}
static int vp_capture(const struct vp_reader*r,struct vp_record*s){
 uint32_t camera=0,source=0,encoder=0,hal=0,again=0,v[36],h[4],queues[5]={0},owner=0;
 s->valid=0;memset(s->irq,255,offsetof(struct vp_record,reserved)-offsetof(struct vp_record,irq));
 /* Two contiguous RAM reads reduce guarded loads and signal-mask syscalls.
  * This is the OEM IRQ-counter array, never a device register window. */
 int ok=1;uint32_t irq_low[18],irq_high[17];
 if(r->physical(r->context,0x813ee860u+38u*4u,irq_low,sizeof irq_low)){for(unsigned i=0;i<11;i++)s->irq[i]=irq_low[vp_irq_ids[i]-38u];}else ok=0;
 if(r->physical(r->context,0x813ee860u+142u*4u,irq_high,sizeof irq_high)){for(unsigned i=11;i<16;i++)s->irq[i]=irq_high[vp_irq_ids[i]-142u];}else ok=0;
 if(ok)s->valid|=VP_RTOS;
 ok=1;for(unsigned i=0;i<4;i++)ok&=vp_rw(r,vp_pp_addresses[i],s->pp+i);if(ok)s->valid|=VP_PP;
 ok=vp_rw(r,0x8146e438u,s->rtos)&&vp_rw(r,0x813f1b30u,s->rtos+1)&&vp_rw(r,0x813ebf60u,s->rtos+2)&&vp_rw(r,0x8014e7ecu,s->rtos+3);
 /* The live scene word carries the 0x1900 operation namespace, not a
  * direct array index. Preserve the raw scene and decode only known IDs. */
 uint32_t scene=s->rtos[1];if((scene&0xffffff00u)==0x1900u)scene&=255u;
 s->reserved[3]=VP_UNKNOWN;
 if(ok&&scene<26u&&r->physical(r->context,0x8100e400u+scene*40u+4,s->policy,36)){s->valid|=VP_POLICY;s->reserved[3]=scene;}
 uint32_t methods[2],setup;
 if(r->physical(r->context,0x80592294u,methods,8)&&vp_rw(r,0x805922bcu,&setup)){s->reserved[0]=methods[0];s->reserved[1]=methods[1];s->reserved[2]=setup;s->valid|=VP_ROUTE;}
 /* Sample the actual native active backend, not a shared UD vtable which
  * need not be active. The setup selector is a literal table data word. */
 uint32_t selector;
 if(vp_rw(r,0x801cd9bcu,&selector)&&(selector==0x801cda40u||selector==0x801cda10u)){s->control=selector;s->valid|=VP_SELECTOR;}
 uint32_t parent,active,type,active2,type2,route[2],builder;
 if(vp_rw(r,0x8153cbecu,&parent)&&parent==0x80592110u&&vp_rw(r,0x8153d964u,&active)&&!(active&3u)&&active>=0x81006000u&&active<=0x93fff000u&&vp_rw(r,active,&type)&&(type==0x80592268u||type==0x80592328u)&&r->physical(r->context,type+0x2cu,route,8)&&vp_rw(r,type+0x54u,&builder)&&vp_rw(r,0x8153d964u,&active2)&&active2==active&&vp_rw(r,active,&type2)&&type2==type){s->reserved[0]=route[0];s->reserved[1]=route[1];s->reserved[2]=builder;s->hevc[9]=type;s->valid|=VP_ROUTE|VP_BACKEND;}
 if(vp_pw(r,r->modules+0x5b0e4u,&camera)&&vp_type(r,camera,r->modules+0x4c608u)&&vp_block(r,camera+0x20,v,24)){
  s->camera[0]=camera;s->camera[1]=v[0];s->camera[2]=v[4];s->camera[3]=v[5];
  if(vp_block(r,camera+0x590,v,112)&&vp_block(r,camera+0x55c,h,8)){
   s->camera[4]=v[2];s->camera[5]=v[3];s->camera[6]=v[8];s->camera[7]=v[9];s->camera[8]=v[20];s->camera[9]=v[21];
   s->camera[10]=v[26];s->camera[11]=v[25];s->camera[12]=v[22];s->camera[13]=h[0];s->camera[14]=h[1];
   s->camera[15]=v[14];s->camera[16]=v[15];s->camera[17]=v[16];s->camera[18]=v[17];s->camera[19]=v[23];
   if(vp_pw(r,r->modules+0x5b0e4u,&again)&&again==camera&&vp_type(r,camera,r->modules+0x4c608u))s->valid|=VP_CAMERA;else s->valid|=VP_CHANGED;
   if((s->valid&VP_CAMERA)&&h[0]==r->core+0x35738u&&vp_type(r,h[1],r->core+0x59c28u))source=h[1];
  }
 }
 if(source&&vp_block(r,source+0x138,v,68)){
  s->source[0]=source;s->source[1]=v[8];s->source[2]=v[9];s->source[3]=v[5];s->source[4]=v[6];s->source[5]=v[11];
  if(vp_type(r,source,r->core+0x59c28u)&&vp_pw(r,camera+0x560,&again)&&again==source)s->valid|=VP_SOURCE;else s->valid|=VP_CHANGED;
  if(s->valid&VP_SOURCE){
   vp_port_queues(r,source,queues);
   if(vp_pw(r,source+0x48,&owner)&&owner>=4u&&vp_type(r,owner,r->core+0x5a11cu)){
    uint32_t nodes[2];owner-=4;
    if(vp_block(r,owner+0x178,nodes,8))for(unsigned i=0;i<2&&!encoder;i++)if(vp_type(r,nodes[i],r->core+0x59da8u))encoder=nodes[i];
   }
  }
 }
 if(encoder&&vp_block(r,encoder+0x98,v,144)){
  s->encoder[0]=encoder;s->encoder[1]=v[35];s->encoder[2]=v[1];s->encoder[3]=v[0];s->encoder[4]=v[32];s->encoder[5]=owner;
  uint32_t eq[3];vp_port_queues(r,encoder,eq);queues[3]=eq[0];queues[4]=eq[1];s->encoder[6]=eq[0];s->encoder[7]=eq[1];
  if(vp_type(r,encoder,r->core+0x59da8u))s->valid|=VP_ENCODER;else s->valid|=VP_CHANGED;
 }
 if(vp_pw(r,r->modules+0x5b144u,&hal)&&vp_type(r,hal,r->modules+0x4c9c8u)&&vp_block(r,hal+0x118,v,96)){
  s->hevc[0]=hal;s->hevc[1]=v[23];s->hevc[2]=v[1];s->hevc[3]=v[6];s->hevc[4]=v[2];s->hevc[5]=v[3];s->hevc[6]=v[20];s->hevc[7]=v[22];
  if(vp_pw(r,hal+0x54,s->hevc+8)&&vp_pw(r,r->modules+0x5b144u,&again)&&again==hal&&vp_type(r,hal,r->modules+0x4c9c8u))s->valid|=VP_HEVC;else s->valid|=VP_CHANGED;
 }
 for(unsigned i=0;i<5;i++)if(queues[i]){
  unsigned j;for(j=0;j<i;j++)if(queues[i]==queues[j]&&(s->valid&(VP_QUEUE0<<j))){memcpy(s->queue[i],s->queue[j],sizeof s->queue[i]);break;}
  if(j<i||vp_queue_capture(r,queues[i],s->queue[i]))s->valid|=VP_QUEUE0<<i;
 }
 return (s->valid&(VP_RTOS|VP_CAMERA|VP_HEVC))!=0;
}
static int vp_record_valid(const struct vp_record*s){return s->commit==(vp_hash(s,offsetof(struct vp_record,commit))^0x91d6037bu);}
_Static_assert(sizeof(struct vp_record)==1280,"fixed cheap counter record");
_Static_assert(sizeof(struct vp_header)<VP_HEADER_BYTES,"bounded shared buffer header");
#endif
