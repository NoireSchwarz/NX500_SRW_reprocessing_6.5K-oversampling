/* Production readers and admission code; all memory endpoints are modeled. */
#define main collector_main
#include "../src/nxl-video-probe.c"
#undef main
#include <assert.h>
struct scripted {
 unsigned schema,magic,scenario,headers,reads,process_reads,vectors,waits;
 uint64_t time;
};
static uint32_t magic_for(unsigned s){
 return s==4?0x33544c56u:s>=5&&s<=8?0x30544c56u+(s<<24):0;
}
static int read_native(void*opaque,uint32_t a,void*out,unsigned n){
 struct scripted*c=opaque;uint32_t*w=out;c->reads++;
 assert(physical_allowed(a,n));
 assert(a!=0x20810440u&&!(a>=0x20821050u&&a<=0x20821060u));
 assert(a!=0x06002060u&&a!=0x06002064u);
 memset(out,0,n);
 if(a==VP_LATENCY_DATA){
  assert(n==32||n==256);w[0]=c->magic;w[1]=c->schema;w[2]=42;
  w[3]=3u|(1u<<16);w[4]=12345678;w[5]=200;
  if(c->scenario==1&&c->headers)w[2]++;
  if(c->scenario==11&&c->waits){w[3]|=4;w[6]=300;}
  c->headers++;return 1;
 }
 if(a==0x817d8aacu){assert(n==8);c->vectors++;w[0]=128;w[1]=32;
  if(c->scenario==2&&c->vectors>1)w[0]=0;return 1;}
 if(a==0x20821070u){assert(n==8);w[0]=128;w[1]=32;return c->scenario!=3;}
 if(a==0x817d8ad4u){assert(n==20);for(unsigned i=0;i<5;i++)w[i]=0x80130000u+i*4;
  w[2]=c->scenario==8?0x801cab24u:c->scenario==9?0:w[2];return 1;}
 if(a==0x817d8b4cu){assert(n==4);*w=0x801ca9a8u;return 1;}
 if(a==0x8153cb84u){assert(n==24);w[0]=0x82345000;w[5]=0x82346000;return 1;}
 if(a==0x801cab38u){assert(n==4);*w=0xe5903000u;return 1;}
 if(a==0x804bb640u){assert(n==4);*w=0xea000001u;return 1;}
 if(a==0x8146e438u){assert(n==4);*w=c->scenario==4?0x1818:0x1717;return 1;}
 if(a==0x813ebf60u){assert(n==4);*w=0xe005;return 1;}
 if(a==VC_STATUS){*w=c->scenario>=10?4:0;return 1;}
 if(a==VC_ENABLE){*w=4;return 1;}
 if(a==VC_HANDLER){*w=0x801cdf6c;return 1;}
 if(a==0x208210b0u){*w=0;return 1;}
 for(unsigned i=0;i<8;i++)if(a==vpg_address[i]){assert(n==4);*w=i+1;return 1;}
 for(unsigned i=0;i<4;i++)if(a==vpe_base[i]){assert(n==20);for(unsigned j=0;j<5;j++)w[j]=i*100+j;return 1;}
 assert(!"unexpected native read");return 0;
}
static int read_process(void*opaque,uint32_t a,void*out,unsigned n){
 struct scripted*c=opaque;c->process_reads++;assert(n==4);uint32_t v;
 if(a==0x30000u)v=c->scenario==5?0:0x11059c28u;
 else if(a==0x20560u)v=c->scenario==6?0x40000u:0x30000u;
 else if(a==0x30164u)v=c->scenario==7?1:2;
 else {assert(!"unexpected process read");return 0;}
 memcpy(out,&v,4);return 1;
}
static uint64_t fake_now(void*opaque){struct scripted*c=opaque;c->time+=1000;return c->time;}
static void fake_wait(void*opaque){struct scripted*c=opaque;c->waits++;c->time+=1000000;}
static void reset(struct scripted*c,unsigned schema){memset(c,0,sizeof*c);c->schema=schema;c->magic=magic_for(schema);}
static struct vp_record sample(void){
 struct vp_record s;memset(&s,0,sizeof s);s.valid=VP_POLICY|VP_BACKEND|VP_SOURCE|VP_RTOS;
 s.rtos[0]=0x1717;s.rtos[2]=0xe005;s.camera[0]=0x20000;s.source[0]=0x30000;s.source[5]=2;
 s.hevc[9]=0x80592268;for(unsigned i=0;i<16;i++)s.irq[i]=i+1000;return s;
}
int main(void){
 struct scripted c;struct vp_reader r={&c,read_native,read_process,0x10000000,0x11000000};
 FILE*f=fopen("/reader-records.bin","wb");assert(f);
 unsigned accepted[4]={4,6,7,8};
 for(unsigned i=0;i<4;i++){
  reset(&c,accepted[i]);struct vp_record s=sample();assert(vp_latency_capture(&r,&s));
  vp_completion_capture(&r,&s,fake_now,fake_wait,&c,1,1,3);
  assert((s.valid&VP_COMPLETION)&&((s.completion[1]&7)==7)&&!c.waits);
  vp_progress_capture(&r,&s,fake_now,&c);
  assert((s.valid&VP_PROGRESS)&&(s.progress[1]&19)==19);
  vp_ep_readback_capture(&r,&s,fake_now,&c);
  assert((s.valid&VP_EP_READBACK)&&(s.ep_readback[1]&0xfff)==0xf0f);
  s.sequence=i;assert(fwrite(&s,sizeof s,1,f)==1);
 }
 assert(!fclose(f));
 for(unsigned i=0;i<4;i++){
  reset(&c,i==0?5:i==1?3:i==2?9:8);if(i==3)c.magic=0x37544c56u;
  struct vp_record s=sample();int supported=vp_latency_capture(&r,&s);
  assert(supported==(i==0));s.valid|=VP_LATENCY;
  vp_completion_capture(&r,&s,fake_now,fake_wait,&c,1,1,3);
  assert(!(s.completion[1]&VC_STABLE)&&!c.waits);
  vp_progress_capture(&r,&s,fake_now,&c);assert(!(s.valid&VP_PROGRESS));
  vp_ep_readback_capture(&r,&s,fake_now,&c);assert(!(s.ep_readback[1]&0x700));
 }
 reset(&c,8);c.scenario=1;struct vp_record s=sample();assert(!vp_latency_capture(&r,&s));
 c.headers=0;s.valid|=VP_LATENCY;vp_completion_capture(&r,&s,fake_now,fake_wait,&c,1,1,3);
 assert((s.completion[1]&VC_CHANGED)&&!(s.completion[1]&VC_STABLE));
 c.headers=0;vp_progress_capture(&r,&s,fake_now,&c);assert((s.progress[1]&8)&&!(s.progress[1]&1));
 c.headers=0;vp_ep_readback_capture(&r,&s,fake_now,&c);assert(!(s.ep_readback[1]&1024));
 puts("PASS formats4/6/7/8, distinct format5, bad magic/version and cross-request changes");
 for(unsigned schema=4;schema<=8;schema++)if(schema!=5){
  uint32_t a[8]={magic_for(schema),schema,42,3,123,200,0,0},b[8];
  memcpy(b,a,sizeof b);b[4]++;assert(vc_same_request(a,b));
  b[2]++;assert(!vc_same_request(a,b));b[2]--;b[5]++;assert(!vc_same_request(a,b));
  for(unsigned kind=10;kind<=11;kind++){
   reset(&c,schema);c.scenario=kind;s=sample();s.valid|=VP_LATENCY;
   vp_completion_capture(&r,&s,fake_now,fake_wait,&c,1,1,3);
   if(kind==10)assert((s.completion[1]&VC_PERSISTED)&&c.waits==2&&s.completion[27]==3);
   else assert((s.completion[1]&VC_CHANGED)&&c.waits==1&&!s.completion[28]);
  }
 }
 puts("PASS request identity uses sequence/HW; bounded pending followups detect final-IRQ transitions");
 struct vr_header h;
 for(unsigned kind=1;kind<=7;kind++){
  reset(&c,8);c.scenario=kind;vr_init(&h);s=sample();vr_capture(&r,&h,&s,2000,1,fake_now,&c);
  assert(!h.count&&h.failed_attempts==1);
 }
 reset(&c,8);vr_init(&h);s=sample();s.valid|=VP_CHANGED;vr_capture(&r,&h,&s,2000,1,fake_now,&c);
 assert(!h.count&&!c.reads&&h.failed_attempts==1);
 reset(&c,5);vr_init(&h);s=sample();vr_capture(&r,&h,&s,2000,1,fake_now,&c);assert(!h.count&&h.failed_attempts==1);
 reset(&c,8);vr_init(&h);s=sample();
 for(unsigned i=1;i<=6;i++){
  c.headers=c.vectors=0;c.scenario=i==2?8:i==3?9:0;
  vr_capture(&r,&h,&s,2000+i*1000,i,fake_now,&c);assert(h.count==i);
  struct vr_record*z=&h.records[i-1];assert(z->w[31]==(vp_hash(z->w,124)^VR_XOR));
  assert(z->w[16]==0x82346000&&z->w[17]==0x82345000&&z->w[22]==8&&z->w[30]==1);
  assert(z->w[23]==1004&&z->w[24]==1005&&z->w[25]==1006);
  unsigned reads=c.reads;vr_capture(&r,&h,&s,9999,i,fake_now,&c);assert(c.reads==reads&&h.count==i);
 }
 unsigned reads=c.reads;vr_capture(&r,&h,&s,9999,7,fake_now,&c);assert(h.overflow&&!h.failed_attempts&&c.reads==reads);
 f=fopen("/ep-routes.bin","wb");assert(f&&fwrite(&h,sizeof h,1,f)==1&&!fclose(f));
 for(unsigned a=0x20821050;a<=0x20821060;a+=4)assert(!physical_allowed(a,4));
 assert(!physical_allowed(0x06002060,4)&&!physical_allowed(0x06002064,4));
 assert(!vr_read_range(0x2082106c,8)&&!vr_read_range(0x817d8ad0,20)&&!vr_read_range(0x817d8ad4,24));
 puts("PASS route coherence/type/source/mode failures, deduplication, six pins, overflow, read-only admission");
 return 0;
}
