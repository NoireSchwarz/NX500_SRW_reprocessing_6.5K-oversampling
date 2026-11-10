#include <stdio.h>
#include <stdlib.h>
#include "../../common/video_counter_capture.h"
#include "../../common/video_latency_capture.h"
#include "../../common/video_completion_capture.h"
struct model {unsigned scenario,pairs,waits,reads;uint64_t time;};
static int read_only(void*p,uint32_t a,void*out,unsigned n){
 struct model*m=p;m->reads++;uint32_t h[8]={VP_LATENCY_MAGIC,3,10,3|(1u<<16),100,110,0,0};
 if(a==VP_LATENCY_DATA){
  if(n!=32)abort();m->pairs++;
  if(m->scenario==1&&m->waits){h[3]|=4;h[6]=200;}
  if(m->scenario==6)h[3]|=1u<<24;
  if(m->scenario==7)h[3]&=~2u;
  if(m->scenario==8)h[3]|=4;
  if(m->scenario==9)h[3]|=12;
  if(m->scenario==10&&!(m->pairs&1u)){h[2]++;h[4]+=200;h[5]+=200;}
  if(m->scenario==13)h[0]=0;
  memcpy(out,h,sizeof h);return 1;
 }
 if(n!=4)abort();uint32_t v=0;
 if(a==VC_STATUS){if(m->scenario==5)return 0;v=m->scenario==0?0:4;}
 else if(a==VC_ENABLE)v=m->scenario==3?0:4;
 else if(a==VC_HANDLER)v=m->scenario==4?0xdeadbeef:0x801cdf6c;
 else abort();memcpy(out,&v,4);return 1;
}
static uint64_t now(void*p){struct model*m=p;uint64_t t=m->time;m->time+=1000;return t;}
static void wait_once(void*p){struct model*m=p;m->waits++;m->time+=m->scenario==14?3000000:1000000;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"failed scenario %u line %u\n",scenario,__LINE__);return 1;}}while(0)
int main(void){
 FILE*f=fopen("/completion.bin","wb");if(!f)return 1;
 for(unsigned scenario=0;scenario<15;scenario++){
  struct model m={.scenario=scenario,.time=1000000000};
  struct vp_reader r={&m,read_only,NULL,0,0};struct vp_record s;memset(&s,0,sizeof s);
  s.valid=VP_POLICY|VP_BACKEND|VP_LATENCY;s.rtos[0]=0x1717;s.rtos[2]=0xe005;
  if(scenario==11)s.valid|=VP_CHANGED;if(scenario==12)s.rtos[2]=0xe004;
  vp_completion_capture(&r,&s,now,wait_once,&m,1,1,3);
  struct vp_completion*z=(struct vp_completion*)s.completion;
  CHECK(m.waits<=2);
  if(scenario==11||scenario==12){CHECK(!m.reads&&!(s.valid&VP_COMPLETION));}
  else{
   CHECK(z->magic==VC_MAGIC&&s.valid&VP_COMPLETION);
   if(scenario==0)CHECK(z->flags&VC_INFLIGHT&&!(z->flags&VC_PENDING)&&!m.waits);
   if(scenario==1)CHECK(z->flags&VC_PENDING&&z->flags&VC_CHANGED&&!z->pending_lower_ns&&m.waits==1);
   if(scenario==2)CHECK(z->flags&VC_PERSISTED&&m.waits==2&&z->pending_hits==3&&z->pending_lower_ns>=2000000);
   if(scenario==3)CHECK(z->flags&VC_PENDING&&!(z->flags&VC_ENABLED)&&!m.waits);
   if(scenario==4)CHECK(z->flags&VC_PENDING&&!(z->flags&VC_HANDLER_OK)&&!m.waits);
   if(scenario==5)CHECK(z->flags==VC_ERROR&&!m.waits);
   if(scenario>=6&&scenario<=9)CHECK(!(z->flags&VC_PENDING)&&!m.waits);
   if(scenario==10||scenario==13)CHECK(!(z->flags&VC_STABLE)&&!m.waits);
   if(scenario==14)CHECK(m.waits==1&&z->pending_lower_ns>=3000000);
  }
  if(fwrite(&s,sizeof s,1,f)!=1)return 1;
 }
 return fclose(f)!=0;
}
