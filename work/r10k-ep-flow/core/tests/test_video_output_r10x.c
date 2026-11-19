/* Production ARM readers; hardware/OS/time endpoints are explicit models. */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/video_fast_capture.h"
#include "../../common/video_hevc_trace.h"
#include "../../common/video_observer_budget.h"
#include "../../common/video_output_capture.h"
#include "pipeline_callbacks_r10w.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"assert line%d case%u\n",__LINE__,m.scenario);exit(7);}}while(0)
struct model {unsigned scenario,step,headers,blocks,descs,WDblocks,selectors,halroots,states,imageTypes,descroots,results;uint64_t ns,cpu;uint32_t header[64];};
static struct model m;
static struct pl_header ph;static struct pl_record pr[PL_CAPACITY];
static uint64_t now(void*p){struct model*x=p;x->ns+=1000;x->cpu+=100;return x->ns;}
static uint64_t cpu(void*p){return((struct model*)p)->cpu;}
static void wait1(void*p){struct model*x=p;x->step++;x->ns+=x->scenario==10?100000000:2000000;}
static int physical(void*p,uint32_t a,void*out,unsigned n){
 struct model*x=p;x->ns+=1000;x->cpu+=x->scenario==9?70000:200;
 if(a==VP_LATENCY_DATA&&(n==32u||n==64u||n==256u)){
  uint32_t h[64];memcpy(h,x->header,sizeof h);x->headers++;
  if((x->scenario==4||x->scenario==29)&&x->headers%2==0){if(x->scenario!=29)h[2]++;h[9]+=0x1000;h[10]+=0x100000;}
  if(x->scenario==5&&x->headers%2==0){memcpy(out,h,12);return 0;}
  if(x->scenario>=20&&x->scenario<=23){
   if(n==256){x->blocks++;if(x->blocks==1&&x->scenario==22)x->ns+=3000000;}
   if(n==32&&(x->blocks==1||(x->scenario==21&&x->blocks==2)))h[2]++;
   if(x->scenario==23)h[0]=0;
  }
  memcpy(out,h,n);return 1;
 }
 if(n==20&&(a==0x20821900||a==0x20821980)){
  uint32_t v[5]={0xb3b50000,0xb4339000,0xf00051,0x08000000,0x31};x->WDblocks++;
  if(a==0x20821980)memset(v,0,sizeof v);
  if(x->scenario==2&&a==0x20821900)v[1]+=4096;
  if(x->scenario==7&&x->WDblocks>=3&&a==0x20821900)v[0]+=4096;
  if(x->scenario==8&&a==0x20821900){memcpy(out,v,4);return 0;}
  memcpy(out,v,n);return 1;
 }
 CHECK(n==4);uint32_t v=0;unsigned found=1;
 if(a==0x8146e438)v=0x1717;
 else if(a==0x813ebf60)v=0xe005;
 else if(a==0x20810418)v=x->step>=8&&x->scenario!=11?4:0;
 else if(a==0x20810468)v=x->scenario==12?0:0xcc;
 else if(a==0x813f09bc)v=x->scenario==13?0x80111110:0x801cdf6c;
 else if(a==0x20810908&&x->scenario==28){memset(out,0xaa,2);return 0;}
 else if(a==0x20810908)v=x->header[10]+(x->scenario==6?4096:0);
 else if(a==0x2082110c)v=0x90760;
 else if(a==0x817d8aac)v=0x401;
 else if(a==0x827b9000){x->imageTypes++;v=x->scenario==25&&x->imageTypes%2==0?0x80588000:0x80588910;}
 else if(a==0x827b906c)v=0xb3b50000;
 else if(a==0x827b9070)v=0;
 else if(a==0x813f09c0)v=0x801cd8b8;
 else if(a==0x813f09d0)v=0x801cd898;
 else if(a==0x208210b0){x->selectors++;v=x->scenario==14||(x->scenario==15&&x->selectors%2)?2:0;}
 else if(a==0x208210e0){CHECK(x->scenario==14||x->scenario==15);v=7;}
 else if(a==0x2081040c||a==0x208210c8||a==0x208210cc||a==0x2082120c||a==0x817d8ab0||a==0x208210d0||a==0x208210d4||a==0x208210d8||a==0x817d8ab4||a==0x817d8ab8||a==0x817d8abc||a==0x81459970||a==0x81459974)v=0;
 else{found=0;for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<18;i++){
  if(a==0x817d8ac0u+bank*112u+i*4u){v=test_callback[bank][i];found=1;}
  if(a==0x8153cb28u+pl_owner_offset[bank][i]){v=0x82001000+bank*1024+i*16;found=1;}
 }}
 CHECK(found);memcpy(out,&v,4);return 1;
}
static int process(void*p,uint32_t a,void*out,unsigned n){
 struct model*x=p;x->ns+=1000;x->cpu+=200;
 if((a==0x23001000)&&n==16){
  uint32_t v[4]={0x40000000,0x100000,0xb3b50000,0xb4339000};x->descs++;
  if(x->scenario==1)v[2]+=4096;
  if(x->scenario==3&&x->descs%2==0)v[3]+=4096;
  if(x->scenario==16){memcpy(out,v,6);return 0;}
  memcpy(out,v,16);return 1;
 }
 CHECK(n==4);uint32_t v=0;
 switch(a){
 case 0x1005b0e4:v=0x20000000;break;
 case 0x20000000:v=0x1004c608;break;
 case 0x20000560:v=0x21000000;break;
 case 0x21000000:v=0x11059c28;break;
 case 0x21000164:x->states++;v=x->scenario==17||(x->scenario==24&&x->states%2==0)?1:2;break;
 case 0x21000158:v=100;break;
 case 0x1005b144:x->halroots++;v=x->scenario==19&&x->halroots%2==0?0x23002000:0x23000000;break;
 case 0x23000000:v=x->scenario==18?0x1004c999:0x1004c9c8;break;
 case 0x23000108:x->descroots++;v=x->scenario==27&&x->descroots%2==0?0x23003000:0x23001000;break;
 case 0x23000174:x->results++;v=x->scenario==26&&x->results%2==0?81:80;if(x->scenario==31)v+=x->step;break;
 case 0x23000054:v=9;break;
 case 0x2300011c:v=1;break;
 default:CHECK(0);
 }
 memcpy(out,&v,4);return 1;
}
static void init(unsigned scenario){
 memset(&m,0,sizeof m);m.scenario=scenario;m.ns=10000000000ULL;
 m.header[0]=0x42544c56;m.header[1]=11;m.header[2]=9;m.header[3]=3|512|(1u<<28);
 m.header[5]=123000;m.header[8]=0x8142f440;m.header[9]=0x827a6800;m.header[10]=0xa4b40000;
 m.header[11]=0x827b9000;m.header[12]=124000;m.header[14]=0x801cdf4c;
 for(unsigned n=6;n<=9;n++){unsigned at=16+(n%4)*12;uint32_t w[12]={n,15|512|1024|(1u<<20)|(1u<<28)|32,n,n*867000,n*867000+810000,n*867000+600000,0x8142f440,0x827a6800,0xa4b40000,0x827b9000,n*867000+700000,0};memcpy(m.header+at,w,sizeof w);}
 pl_init(&ph);memset(pr,0,sizeof pr);
}
static void save(unsigned scenario){
 char name[80];snprintf(name,sizeof name,"/output-%u.bin",scenario);ph.w[31]=vp_hash(&ph,124);FILE*f=fopen(name,"wb");CHECK(f);
 CHECK(fwrite(&ph,1,sizeof ph,f)==sizeof ph);CHECK(fwrite(pr,sizeof*pr,ph.w[4],f)==ph.w[4]);CHECK(!fclose(f));
}
int main(void){
 struct vp_reader r={&m,physical,process,0x10000000,0x11000000};
 for(unsigned s=0;s<=31;s++){
  if((s>=20&&s<=23)||s==30)continue;
  init(s);pl_burst(&r,&ph,pr,now,cpu,wait1,&m,m.ns+100000000);
  CHECK(ph.w[5]==1&&ph.w[13]<=24);unsigned follow=0;
  for(unsigned i=0;i<ph.w[4];i++)if(pr[i].kind==PL_SAMPLE&&pr[i].flags&PL_POST_PENDING)follow++;
  CHECK(follow<=2);if(s==0)CHECK(follow==2);
  if(s==9)CHECK(ph.w[6]&PL_CPU);if(s==10)CHECK(ph.w[6]&PL_TIME);
  save(s);
 }
 for(unsigned s=20;s<=23;s++){
  init(s);static struct vf_record fr[VF_CAPACITY];struct vf_header fh;vf_init(&fh);
  CHECK(vf_capture(&r,&fh,fr,now,&m));CHECK(fh.count<3);
  if(s==20){CHECK(fh.count==2&&fh.reserved[2]==1);CHECK(fr[0].valid&512);}
  if(s==21)CHECK(fh.count==2&&fh.reserved[3]==1);
  if(s==22||s==23)CHECK(fh.count==1&&fh.reserved[3]==1);
  fh.hash=vf_header_hash(&fh);char name[80];snprintf(name,sizeof name,"/fast-%u.bin",s);FILE*f=fopen(name,"wb");CHECK(f);
  CHECK(fwrite(&fh,1,sizeof fh,f)==sizeof fh&&fwrite(fr,sizeof*fr,fh.count,f)==fh.count);CHECK(!fclose(f));
 }
 init(0);struct vo_budget b;vo_init(&b,0);
 for(unsigned i=1;i<=8;i++){
  b.fast=10000000;b.heavy=60000000;b.dense=100000;
  CHECK(vo_check(&b,(uint64_t)i*10000000000ULL));CHECK(b.dense_ms==5000);
  pl_budget(&ph,pr,m.ns,&b);vo_reset_costs(&b);
 }
 CHECK(b.heavy_ms==5000&&b.heavy_over==8&&b.dense_over==0);
 b.dense=30000000;CHECK(vo_check(&b,90000000000ULL));CHECK(b.dense_ms==10000&&b.target_ms==2000);
 pl_budget(&ph,pr,m.ns,&b);save(30);
 init(0);ph.w[4]=PL_CAPACITY-27;pl_burst(&r,&ph,pr,now,cpu,wait1,&m,m.ns+100000000);CHECK(ph.w[6]&PL_FULL&&!ph.w[5]);
 CHECK(!pl_read_range(0x20821050,4)&&!pl_read_range(0x20810440,4)&&!pl_read_range(0x208210b0,8));
 puts("PASS27 output windows;4 retry paths;9 separated budgets;capacity/readonly guard");return 0;
}
