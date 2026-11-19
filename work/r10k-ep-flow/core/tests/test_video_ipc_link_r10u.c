/* Actual ARM reader; physical memory and clocks are explicit fixtures. */
#include <stdio.h>
#include "../../common/video_path_capture.h"
#include "../../common/video_latency_capture.h"
#include "../../common/video_flow_capture.h"
#include "../../common/video_ipc_link_capture.h"
struct model {unsigned pass,timers,variant;uint64_t ns;};
static int physical(void*opaque,uint32_t at,void*out,unsigned n){
 struct model*m=opaque;uint32_t v=VP_UNKNOWN;
 if(at==VP_LATENCY_DATA&&n==32){
  m->timers++;if((m->variant==1&&m->timers==1)||(m->variant==2&&m->timers==2))return 0;
  uint32_t h[8]={0x41544c56,10,7,3,20,26000000,0,26000156};
  if(m->variant==3&&m->timers==2)h[2]++;
  if(m->variant==4){h[0]=0x39544c56;h[1]=9;}
  memcpy(out,h,32);return 1;
 }
 if(n!=4)return 0;
 if(at==0x8153cbec)m->pass++;
 if(m->variant==5&&m->pass==1&&at==0x8153d930)return 0;
 if(m->variant==6&&m->pass==2&&at==0x827c5800)return 0;
 switch(at){
 case 0x8153cbec:v=0x80592110;break;
 case 0x8153d910:v=m->variant==7?7:0x303;break;
 case 0x8153d964:v=0x82573000;break;
 case 0x82573000:v=0x80592328;break;
 case 0x8146e438:v=0x1717;break;
 case 0x813ebf60:v=0xe005;break;
 case 0x8153d930:v=m->variant==8?0xfffffff0:0x827b5000;break;
 case 0x827b5000:v=m->variant==9?0xdeadbeef:0x80588910;break;
 case 0x827b506c:v=m->variant==10?0:0xa7888900;break;
 case 0x20810908:v=m->variant==11?0xa7889900:0xa7888900;break;
 case 0x8153d938:v=m->variant==12&&m->pass==2?0x827c5804:0x827c5800;break;
 case 0x827c5800:v=0x80588910;break;
 case 0x827c5804:v=0x80588910;break;
 default:return 0;
 }
 memcpy(out,&v,4);return 1;
}
static uint64_t now(void*opaque){struct model*m=opaque;m->ns+=400000;return m->ns;}
int main(void){
 for(unsigned i=0;i<13;i++){
  struct model m={0,0,i,0};struct vp_reader r={&m,physical,physical,0,0};struct vp_record s;memset(&s,0,sizeof s);
  s.valid=VP_POLICY|VP_BACKEND|VP_SOURCE;s.rtos[0]=0x1717;s.rtos[2]=0xe005;s.hevc[9]=0x80592328;s.source[0]=0x70b9a8;s.source[5]=2;
  vp_ipc_link_capture(&r,&s,now,&m);
  if(!(s.valid&VP_IPC_LINK)||!!(s.ep_readback[1]&0x80000000u)!=(i==0))return 10+i;
  char name[32];snprintf(name,sizeof name,"/link-%02u.bin",i);FILE*f=fopen(name,"wb");if(!f)return 40;
  if(fwrite(s.ep_readback,1,128,f)!=128||fclose(f))return 41;
 }
 puts("PASS13 independent reader success/read-failure/type/state/address/sequence cases");return 0;
}
