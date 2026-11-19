/* Actual production ARM reader; hardware clocks, flags and RAM are models. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/video_fast_capture.h"
#include "../../common/video_hevc_trace.h"
#include "../../common/video_pipeline_capture.h"
#include "pipeline_callbacks_r10w.h"
struct model {uint64_t ns,cpu;unsigned step,reads,headers,scenario;uint32_t header[64];};
static struct pl_header ph;static struct pl_record pr[PL_CAPACITY];
#define CHECK(x) do{if(!(x)){fprintf(stderr,"assert line%d\n",__LINE__);exit(7);}}while(0)
static uint64_t now(void*p){struct model*m=p;m->ns+=1000;m->cpu+=100;return m->ns;}
static uint64_t cpu(void*p){return ((struct model*)p)->cpu;}
static void wait1(void*p){struct model*m=p;m->step++;m->ns+=m->scenario==8?100000000:1000000;}
static int physical(void*p,uint32_t a,void*out,unsigned n){
 struct model*m=p;m->reads++;m->ns+=1000;m->cpu+=m->scenario==7?30000:200;
 if(a==VP_LATENCY_DATA&&(n==32||n==64||n==256)){
  uint32_t h[64];memcpy(h,m->header,sizeof h);m->headers++;
  if(m->scenario==3&&m->step>=4){h[2]++;h[5]+=10000;}
  if(m->scenario==4&&m->headers%2==0)h[9]+=0x1000;
  if(m->scenario==14&&m->headers>=3)h[9]+=0x1000;
  if((m->scenario==5&&m->headers==4)||(m->scenario==12&&m->headers==5)){memcpy(out,h,12);return 0;}
  memcpy(out,h,n);return 1;
 }
 CHECK(n==4);uint32_t v=0;unsigned bit=m->step;
 if(m->scenario==2)bit=100;
 if(a==0x8146e438)v=0x1717;
 else if(a==0x813ebf60)v=0xe005;
 else if(a==0x2081040c)v=bit>=2?1u<<9:0;
 else if(a==0x20810408)v=bit>=6?0x22:0;
 else if(a==0x20810418){if(m->scenario==6){memset(out,0xaa,2);return 0;}v=bit>=8&&m->scenario!=9?4:0;}
 else if(a==0x20810458)v=m->scenario==1?0:0x22;
 else if(a==0x2081045c)v=m->scenario==1?0:1u<<9;
 else if(a==0x20810468)v=0xcc;
 else if(a==0x208210c8)v=1024u|(bit>=3?2u:0)|(bit>=4?1u<<13:0)|(bit>=6?1u<<6:0);
 else if(a==0x208210cc||a==0x817d8ab0)v=0;
 else if(a==0x817d8aac)v=1024u|2u|(1u<<13)|(1u<<6);
 else if(a==0x20810908)v=m->header[10]+(m->scenario==13?0x1000:0);
 else if(a==0x813f09bc)v=m->scenario==10?0x80012340:0x801cdf6c;
 else if(a==0x813f0c58)v=0x801dd1dc;
 else if(a==0x813f0bc8)v=0x801dd214;
 else if(a==0x813f0bb8)v=0x801dd24c;
 else if(a==0x20821104||a==0x2082110c||a==0x20821204||a==0x2082120c)v=0x777;
 else {
  unsigned found=0;
  for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<18;i++){
   if(a==0x817d8ac0u+bank*112u+i*4u){v=test_callback[bank][i];found=1;}
   if(a==0x8153cb28u+pl_owner_offset[bank][i]){
    if(m->scenario==11&&bank==0&&i==13){memset(out,0xcc,2);return 0;}
    v=bank==0&&i==10?0x8142f440:0x82001000+bank*0x400+i*16;found=1;
   }
  }
  CHECK(found);
 }
 memcpy(out,&v,4);return 1;
}
static void init(struct model*m,unsigned scenario){
 memset(m,0,sizeof*m);m->scenario=scenario;m->ns=10000000000ULL;
 m->header[0]=0x42544c56;m->header[1]=11;m->header[2]=9;m->header[3]=3|512|(1u<<28);
 m->header[5]=123000;m->header[8]=0x8142f440;m->header[9]=0x827a6800;m->header[10]=0xa4b40000;
 m->header[11]=0x827b9000;m->header[12]=124000;m->header[14]=0x801cdf4c;
 pl_init(&ph);memset(pr,0,sizeof pr);
}
static void save(const char*name){
 ph.w[31]=vp_hash(&ph,124);FILE*f=fopen(name,"wb");CHECK(f);CHECK(fwrite(&ph,1,sizeof ph,f)==sizeof ph);
 CHECK(fwrite(pr,sizeof*pr,ph.w[4],f)==ph.w[4]);CHECK(!fclose(f));
}
int main(void){
 struct model m;struct vp_reader r={0};r.context=&m;r.physical=physical;
 for(unsigned scenario=0;scenario<14;scenario++){
  init(&m,scenario);pl_burst(&r,&ph,pr,now,cpu,wait1,&m,m.ns+100000000);
  CHECK(ph.w[5]==1);CHECK(ph.w[4]<=54);CHECK(ph.w[13]<=50);
  unsigned follow=0;for(unsigned i=0;i<ph.w[4];i++)if(pr[i].kind==PL_SAMPLE&&(pr[i].flags&PL_POST_PENDING))follow++;
  CHECK(follow<=2);if(!scenario)CHECK(follow==2);
  if(scenario==7)CHECK(ph.w[6]&PL_CPU);
  if(scenario==8)CHECK(ph.w[6]&PL_TIME);
  if(scenario==9)CHECK(ph.w[13]==50);
  if(scenario==10)CHECK(!follow);
  char name[80];snprintf(name,sizeof name,"/pipeline-%u.bin",scenario);save(name);
  unsigned old=ph.w[4];pl_burst(&r,&ph,pr,now,cpu,wait1,&m,m.ns+100000000);CHECK(ph.w[4]==old);
 }
 for(unsigned test=0;test<7;test++){
  init(&m,0);
  if(test==0)m.header[0]=0;
  if(test==1)m.header[3]&=~512u;
  if(test==2)m.header[3]|=0x10000;
  if(test==3)m.header[3]+=1u<<28;
  if(test==4)m.header[14]=0x80111110;
  if(test==5)m.header[3]|=0x20000;
  if(test==6)m.header[3]|=4;
  pl_burst(&r,&ph,pr,now,cpu,wait1,&m,m.ns+100000000);CHECK(!ph.w[4]&&!ph.w[5]);
 }
 init(&m,0);ph.w[4]=PL_CAPACITY-53;pl_burst(&r,&ph,pr,now,cpu,wait1,&m,m.ns+100000000);CHECK(ph.w[6]&PL_FULL);CHECK(!ph.w[5]);
 init(&m,0);struct vf_header fh;static struct vf_record fr[2];vf_init(&fh);
 CHECK(vf_capture(&r,&fh,fr,now,&m));CHECK(fh.count==1&&fr[0].valid&256);fh.hash=vp_hash(&fh,offsetof(struct vf_header,hash));
 FILE*f=fopen("/fast3.bin","wb");CHECK(f);CHECK(fwrite(&fh,1,sizeof fh,f)==sizeof fh&&fwrite(fr,1,sizeof*fr,f)==sizeof*fr);CHECK(!fclose(f));
 init(&m,14);vf_init(&fh);CHECK(vf_capture(&r,&fh,fr,now,&m));CHECK(fr[0].valid&1);CHECK(!(fr[0].valid&256));
 fh.hash=vp_hash(&fh,offsetof(struct vf_header,hash));f=fopen("/fast3-tuple-changed.bin","wb");CHECK(f);CHECK(fwrite(&fh,1,sizeof fh,f)==sizeof fh&&fwrite(fr,1,sizeof*fr,f)==sizeof*fr);CHECK(!fclose(f));
 puts("PASS fourteen ARM pipeline windows; seven rejected start conditions; finite reads/cadence/CPU/time/capacity; fast3 full-header guard");return 0;
}
