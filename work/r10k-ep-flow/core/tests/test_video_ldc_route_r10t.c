/* Production ARM owner snapshots consume original-P7-generated lifecycle bytes.
 * All OS, process and native-memory endpoints remain explicit models. */
#define main collector_main
#include "../src/nxl-video-probe.c"
#undef main
#include <assert.h>
static unsigned char image[0x1800],parameter[84];
static uint32_t native_owner,native_callback;
struct scripted {unsigned scenario,vectors,headers,reads,process_reads;uint32_t fail_address,owner_override,selected_override;unsigned fail_pass;uint64_t time;};
static void load(const char*name){FILE*f=fopen(name,"rb");assert(f);assert(fread(image,sizeof image,1,f)==1&&fread(parameter,sizeof parameter,1,f)==1);assert(fread(&native_owner,4,1,f)==1&&fread(&native_callback,4,1,f)==1);assert(fgetc(f)==EOF&&!fclose(f));}
static int read_native(void*opaque,uint32_t a,void*out,unsigned n){
 struct scripted*c=opaque;uint32_t*w=out;c->reads++;assert(physical_allowed(a,n));
 assert(a>=0x81006000u); /* New object capture never requests any MMIO/code read. */
 if(a==0x8153cb28u)c->vectors++;
 if(a==c->fail_address&&(!c->fail_pass||c->fail_pass==c->vectors))return 0;
 memset(out,0,n);
 if(a==VP_LATENCY_DATA){assert(n==32);w[0]=0x39544c56;w[1]=9;w[2]=42;w[3]=3;w[4]=100;w[5]=200;
  if(c->scenario==1&&c->headers)w[2]++;c->headers++;return 1;}
 if(a==0x8153cb28u){assert(n==4);*w=c->scenario==10?c->owner_override:native_owner;
  if(c->scenario==2&&c->vectors>1)*w=0x8142f45c;return 1;}
 if(a==0x817d8ae8u){assert(n==4);*w=c->scenario==11?0:c->scenario==12?0x801cab24:native_callback;return 1;}
 if(a>=0x8142e000u&&(uint64_t)a+n<=0x8142f800u){
  memcpy(out,image+a-0x8142e000u,n);
  if(a==0x8142efb8u){assert(n==4);if(c->scenario==3&&c->vectors>1)*w=0x8142f434;if(c->scenario==13)*w=c->selected_override;}
  if(a==0x8142efbcu&&c->scenario==14)*w=0;
  if(a==0x8142efc0u&&c->scenario==15)*w=0;
  if(a==0x8142ed3cu&&c->scenario==16&&c->vectors>1)*w=4;
  if(a==native_owner-12u&&n==16){
   if(c->scenario==17){w[0]=1;w[3]=0xf00;}
   if(c->scenario==18){w[0]=0x80582e70;w[3]=0x80582ed0;}
  }
  if(a==0x8142e018u&&c->scenario==19)*w=0;
  return 1;
 }
 if(a==0x82030000u){assert(n==16);w[0]=0x80582d10;w[3]=0x80582d70;return 1;}
 if(a==0x8153d964u){assert(n==4);*w=c->scenario==20?0:0x82573000;return 1;}
 if(a==0x82573000u){assert(n==4);*w=c->scenario==21?0:0x80592328;return 1;}
 if(a==0x8153cbecu){assert(n==4);*w=0x80592110;return 1;}
 if(a==0x8146e438u){assert(n==4);*w=c->scenario==22?0x1818:0x1717;return 1;}
 if(a==0x813ebf60u){assert(n==4);*w=0xe005;return 1;}
 assert(!"unexpected native read");return 0;
}
static int read_process(void*opaque,uint32_t a,void*out,unsigned n){
 struct scripted*c=opaque;c->process_reads++;assert(n==4);uint32_t value;
 if(a==0x30000)value=c->scenario==23?0:0x11059c28;
 else if(a==0x20560)value=c->scenario==24?0x40000:0x30000;
 else if(a==0x30164)value=c->scenario==25?1:2;
 else{assert(!"unexpected process read");return 0;}
 memcpy(out,&value,4);return 1;
}
static uint64_t fake_now(void*opaque){struct scripted*c=opaque;c->time+=1000;return c->time;}
static struct vp_record sample(void){struct vp_record s;memset(&s,0,sizeof s);s.valid=VP_POLICY|VP_CAMERA|VP_BACKEND|VP_SOURCE|VP_RTOS;
 s.rtos[0]=0x1717;s.rtos[2]=0xe005;s.camera[0]=0x20000;s.source[0]=0x30000;s.source[5]=2;s.hevc[9]=0x80592328;return s;}
static void capture(struct scripted*c,struct vr_header*h,struct vp_record*s,unsigned generation,unsigned ms){
 struct vp_reader r={c,read_native,read_process,0x10000000,0x11000000};vr_capture(&r,h,s,ms,generation,fake_now,c);
}
static void save(const char*name,const struct vr_header*h){FILE*f=fopen(name,"wb");assert(f&&fwrite(h,sizeof*h,1,f)==1&&!fclose(f));}
static unsigned blocking(const struct vr_record*r){return(r->w[7]>>16)&255;}
int main(void){
 const char*fixtures[]={"selected","started","prepared","stopped","closed","reopened","still-owner"};
 for(unsigned i=0;i<7;i++){
  char path[100];snprintf(path,sizeof path,"/native-ldc-%s.bin",fixtures[i]);load(path);
  struct scripted c={0};struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);
  int live=i<2||i==5;
  assert(h.count==(unsigned)live&&h.failed_attempts==(unsigned)!live);
  struct vr_record*r=&h.records[live?0:3];assert(r->w[12]==native_owner&&r->w[24]==native_owner);
  if(i!=6)assert((r->w[6]&VR_TYPES_OK)&&(r->w[6]&VR_MEMBER_OK));
  if(!live)assert(blocking(r)==(i==6?VR_OWNER_RANGE:VR_STATE_BAD));
  if(live&&i)assert(r->w[14]==0&&r->w[26]==0&&!(r->w[6]&VR_SELECTED_RAM));
  snprintf(path,sizeof path,"/routes-%s.bin",fixtures[i]);save(path,&h);
 }
 load("/native-ldc-started.bin");
 unsigned scenarios[]={1,2,16,17,18,11,12,14,19,20,21,22,23,24,25};
 unsigned expected[]={4,5,5,30,30,31,31,32,33,22,34,42,43,44,45};
 for(unsigned i=0;i<sizeof scenarios/sizeof*scenarios;i++){
  struct scripted c={0};c.scenario=scenarios[i];struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);
  assert(!h.count&&h.failed_attempts==1&&blocking(&h.records[3])==expected[i]);
  assert(h.records[3].w[47]==(vp_hash(h.records[3].w,188)^VR_XOR));
  char path[80];snprintf(path,sizeof path,"/negative-%u.bin",scenarios[i]);save(path,&h);
 }
 uint32_t owners[]={0,4,0x8142f442,0xfffffffcu,0x20000000,0x8203000c};
 for(unsigned i=0;i<6;i++){
  struct scripted c={0};c.scenario=10;c.owner_override=owners[i];struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);
  assert(!h.count&&h.failed_attempts==1&&h.records[3].w[12]==owners[i]);
  assert(blocking(&h.records[3])==(i==5?VR_MEMBER_BAD:VR_OWNER_RANGE));
  char path[80];snprintf(path,sizeof path,"/owner-range-%u.bin",i);save(path,&h);
 }
 uint32_t faults[]={0x8153cb28,0x817d8ae8,0x8142efbc,0x8142efc0,0x8142e018,0x8142ed3c,0x8153d964,0x82573000,0x8153cbec,0x8142f434};
 for(unsigned i=0;i<10;i++)for(unsigned pass=1;pass<=2;pass++){
  struct scripted c={0};c.fail_address=faults[i];c.fail_pass=pass;struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);
  assert(!h.count&&h.failed_attempts==1);assert(((h.records[3].w[7]>>24)&3)==pass);
  char path[80];snprintf(path,sizeof path,"/read-fault-%u-%u.bin",i,pass);save(path,&h);
 }
 for(unsigned pass=1;pass<=2;pass++){
  struct scripted c={0};c.fail_address=0x8142efb8;c.fail_pass=pass;struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);
  assert(h.count==1&&!h.failed_attempts&&(h.records[0].w[7]&255)==(pass==1?VR_READ_SELECTED:VR_SELECTED_RANGE));
  assert(!(h.records[0].w[6]&VR_SELECTED_READ));
  assert(h.records[0].w[pass==1?14:26]==VP_UNKNOWN);
  char path[80];snprintf(path,sizeof path,"/selected-read-fault-%u.bin",pass);save(path,&h);
 }
 for(unsigned i=0;i<4;i++){
  struct scripted c={0};c.scenario=13;c.selected_override=i==0?0x8142f436:i==1?0xfffffff4:i==2?0x825ac800:0x8142f434;
  struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);
  assert(h.count==1&&!h.failed_attempts&&h.records[0].w[14]==c.selected_override);
 }
 {struct scripted c={0};c.scenario=3;struct vr_header h;vr_init(&h);struct vp_record s=sample();capture(&c,&h,&s,1,5000);assert(h.count==1&&(h.records[0].w[10]&(1u<<VR_SELECTED)));save("/selected-cleared-between-reads.bin",&h);}
 {struct scripted c={0};struct vr_header h;vr_init(&h);struct vp_record s=sample();s.valid|=VP_CHANGED;capture(&c,&h,&s,1,5000);assert(!h.count&&h.failed_attempts==1&&blocking(&h.records[3])==VR_CONTEXT_FAILED);save("/bad-sample-context.bin",&h);}
 /* Rejections do not consume verified slots or prevent a later retry. */
 {struct vr_header h;vr_init(&h);struct vp_record s=sample();
  for(unsigned gen=1;gen<=3;gen++){
   struct scripted bad={0};bad.scenario=17;capture(&bad,&h,&s,gen,gen*2000);assert(h.failed_attempts==gen);
   struct scripted good={0};capture(&good,&h,&s,gen,gen*2000+100);assert(h.count==gen);
   unsigned reads=good.reads;capture(&good,&h,&s,gen,9999);assert(good.reads==reads);
  }
  assert(h.records[3].w[11]==5&&h.records[3].w[2]==3);unsigned attempt=h.count+h.failed_attempts;
  struct scripted c={0};capture(&c,&h,&s,4,9999);assert(h.overflow&&h.count+h.failed_attempts==attempt&&!c.reads);
  save("/ep-routes.bin",&h);
 }
 puts("PASS7 original lifecycle fixtures,15 semantic faults,6 owner-range/member cases,20 read faults,selected-null/change/read faults,and bounded retries/retention");
 return 0;
}
