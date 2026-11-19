/* Actual production C/ARM including the real admission predicate. Explicit
 * OS-free camera RAM/MMIO models: not a hardware or cache-coherence test. */
#define main collector_main_not_called
#include "../src/nxl-video-probe.c"
#undef main
#define CHECK(x) do{if(!(x)){fprintf(stderr,"case%u line%d\n",scenario,__LINE__);exit(7);}}while(0)
static unsigned scenario,phys_calls,proc_calls,pool_reads,pass_reads;
static uint64_t stamp,cpuns,origin;
static struct bv_header bh;
static struct bv_record br[BV_CAPACITY];
static struct bm_slot ms[BM_CAPACITY];
static struct bv_state bs;
static uint32_t cam[0x600/4],source[0x200/4],enc[0x180/4],hal[0x180/4],pool[17],ports[2],slots[64*30],yuv[64][16],table[BM_BLOCKS*4];
static unsigned current_req,last_req;
static uint32_t surface(unsigned n){return 0xad610000u+(n%10u)*0xca8000u;}
static void refresh(void){
 current_req=100u+(unsigned)((stamp-origin)/42000000u);unsigned idx=(current_req-1u)%64u;
 unsigned stride=scenario>=13?28:30,at=scenario>=13?26:28;
 for(unsigned i=0;i<64;i++){
  slots[i*stride+at]=i==idx?3u:0u;
  if(scenario>=13){if(i!=idx){slots[i*stride+1]=slots[i*stride+2]=slots[i*stride+3]=slots[i*stride+24]=0;}
   else{slots[i*stride+1]=slots[i*stride+2]=0x28000000+i*64;slots[i*stride+3]=64;}}
 }
 if(last_req!=current_req){last_req=current_req;slots[idx*stride+(scenario>=13?4:6)]=current_req;slots[idx*stride+(scenario>=13?5:7)]=0;
  yuv[idx][11]=surface(current_req-1);yuv[idx][13]=yuv[idx][11]+3840u*2160u;}
 source[0x158/4]=current_req;
}
static uint64_t sim_now(void*p){(void)p;stamp+=1000;cpuns+=100;return stamp;}
static uint64_t sim_cpu(void*p){(void)p;return cpuns;}
static void sim_wait(void*p){(void)p;stamp+=2000000u;}
static int fake_phys(void*p,uint32_t a,void*out,unsigned n){
 (void)p;CHECK(physical_allowed(a,n));phys_calls++;stamp+=1000;cpuns+=scenario==10?70000:200;refresh();
 if(a==VP_LATENCY_DATA&&(n==32u||n==64u||n==256u)){
  uint32_t w[64]={0};w[0]=0x42544c56;w[1]=11;w[2]=current_req-1;w[3]=3|512|32|(1u<<20)|(1u<<28);w[4]=current_req;w[5]=current_req*1092000u;
  w[8]=0x8142f440;w[9]=0x8278d800;w[10]=0xa1c00000;w[11]=0x827a0000;w[12]=w[5]+500;w[14]=0x801cdf4c;
  for(unsigned q=current_req-4;q<current_req;q++){unsigned at=16+(q%4)*12;w[at]=q;w[at+1]=15|512|1024|32|(1u<<20)|(1u<<28);w[at+2]=q;w[at+3]=q*1092000u;w[at+4]=w[at+3]+1000000;w[at+5]=w[at+3]+200;w[at+6]=w[8];w[at+7]=w[9];w[at+8]=w[10];w[at+9]=w[11];w[at+10]=w[at+3]+500;}
  if(scenario==11&&phys_calls%2==0)w[9]+=4096;
  memcpy(out,w,n);return 1;
 }
 if(a==0x20821900u&&n==20){uint32_t w[5]={surface(current_req),surface(current_req)+3840u*2160u,0xf00051,0x08000000,0x31};memcpy(out,w,n);return 1;}
 if(a>=BM_TABLE&&(uint64_t)a+n<=BM_TABLE+BM_BYTES){
  unsigned at=a-BM_TABLE;pass_reads++;if(scenario==6)return 0;memcpy(out,(unsigned char*)table+at,n);
  if(scenario==4&&pass_reads>177u)((uint32_t*)out)[0]^=1;return 1;
 }
 if(a==0x817dc498u&&n==40){uint32_t w[10]={0};w[0]=0x80574230;w[1]=0x805742ac;w[9]=0x81dfc800;memcpy(out,w,n);return 1;}
 if(a==0x81dfc800u&&n==128){uint32_t w[32]={0};w[0]=0x80574b60;w[0x78/4]=0x9c1b0000;memcpy(out,w,n);return 1;}
 if(a==0x81dfc880u&&n==128){memset(out,0,n);return 1;}
 if(a==0x81dfc900u&&n==8){uint32_t w[2]={0,0x80574c48};memcpy(out,w,n);return 1;}
 if(a==0x81eac908u&&n==12){uint32_t w[3]={0x94000000,BM_BLOCKS,30000};memcpy(out,w,n);return 1;}
 if((a==0x8278d800u||a==0x827a0000u)&&n==104){uint32_t w[26]={0};w[0]=0x80588910;w[1]=3840;w[2]=2160;w[15]=2;memcpy(out,w,n);return 1;}
 if((a==0x8278d868u||a==0x827a0068u)&&n==48){uint32_t w[12]={7,0};w[5]=surface(current_req);w[7]=w[5]+3840u*2160u;if(scenario==3)w[0]=2;memcpy(out,w,n);return 1;}
 if(n!=4u)return 0;uint32_t w=0;
 if(a==0x81eacda0u)w=0xbb6cc000;
 else if(a==0x2082110cu)w=0x90760;
 else if(a==0x8146e438u)w=0x1717;
 else if(a==0x813ebf60u)w=0xe005;
 else if(a==0x813f09bcu)w=0x801cdf6c;
 else if(a==0x20810468u)w=0xcc;
 else if(a==0x20810418u&&scenario==12)w=4;
 else if(a==0x20810908u)w=0xa1c00000;
 else if(a==0x20821900u)w=surface(current_req);
 else if(a==0x20821904u)w=surface(current_req)+3840u*2160u;
 memcpy(out,&w,4);return 1;
}
static int fake_proc(void*p,uint32_t a,void*out,unsigned n){
 (void)p;proc_calls++;stamp+=1000;cpuns+=scenario==10?70000:200;refresh();
 if(a==0x1005b0e4u&&n==4){uint32_t w=0x20000000;memcpy(out,&w,4);return 1;}
 if(a==0x1005b144u&&n==4){uint32_t w=0x23000000;memcpy(out,&w,4);return 1;}
 if(a==0x1005b148u&&n==32){uint32_t w[8]={0x40000000,0x100000,surface(current_req-1),surface(current_req-1)+3840u*2160u};memcpy(out,w,n);return 1;}
 struct {uint32_t base;void*data;unsigned bytes;}regions[]={
 {0x20000000,cam,sizeof cam},{0x21000000,source,sizeof source},{0x23000000,hal,sizeof hal},
 {0x24000000,enc,sizeof enc},{0x25000000,pool,sizeof pool},{0x26000000,ports,sizeof ports},
 {0x27000000,slots,sizeof slots},{0x28000000,yuv,sizeof yuv}};
 if(a==0x25000000u){pool_reads++;if((scenario==9||scenario==14)&&pool_reads%2==0){memset(out,0,n);return 1;}}
 if(scenario==15&&a==0x27000000u&&pool_reads%2==0)slots[((current_req-1u)%64u)*28+4]++;
 if(scenario==16&&a>=0x28000000u&&a<0x28000000u+sizeof yuv)return 0;
 if(scenario==8&&a>=0x28000000u&&a<0x28000000u+sizeof yuv&&proc_calls%2==0)yuv[(current_req-1)%64][0]^=1;
 for(unsigned i=0;i<sizeof regions/sizeof*regions;i++)if(a>=regions[i].base&&(uint64_t)a+n<=regions[i].base+regions[i].bytes){memcpy(out,(unsigned char*)regions[i].data+a-regions[i].base,n);return 1;}
 return 0;
}
static void init(unsigned c){
 scenario=c;phys_calls=proc_calls=pool_reads=pass_reads=0;stamp=origin=13000000000ULL;cpuns=0;last_req=0;
 memset(cam,0,sizeof cam);memset(source,0,sizeof source);memset(enc,0,sizeof enc);memset(hal,0,sizeof hal);memset(pool,0,sizeof pool);memset(slots,0,sizeof slots);memset(yuv,0,sizeof yuv);memset(table,0,sizeof table);
 memset(ms,0,sizeof ms);bv_init(&bh,&bs);bh.w[23]=0x31415926;bh.w[24]=123;bh.w[25]=(uint32_t)origin;bh.w[26]=(uint32_t)(origin>>32);bh.w[27]=247;bh.w[28]=1;bh.w[30]=0x10000000;
 cam[0]=0x1004c608;cam[0x55c/4]=0x11035738;cam[0x560/4]=0x21000000;source[0]=0x11059c28;source[0x164/4]=2;
 enc[0]=0x11059da8;enc[0x2c/4]=0x26000000;enc[0x30/4]=1;ports[0]=0;ports[1]=0x25000000;
 hal[0]=c==1?0x1004c999:0x1004c9c8;hal[0x108/4]=0x1005b148;hal[0x11c/4]=1;hal[0x174/4]=100;
 pool[0]=c==2?0x110593f0:c>=13?0x110593b0:0x110592f8;
 if(c>=13){pool[7]=64;pool[8]=0x27000000;pool[9]=0x29000000;pool[10]=0x29001000;}
 else{pool[7]=0x27000000;pool[8]=0x29000000;pool[9]=0x29001000;pool[14]=64;}
 for(unsigned i=0;i<64;i++){unsigned stride=c>=13?28:30;slots[stride*i]=i;if(c<13){slots[30*i+4]=0x28000000+i*64;slots[30*i+5]=64;}yuv[i][0]=7;}
 unsigned first=(0xad610000u-0x94000000u)/16384u;table[4*first]=0x23;table[4*first+1]=8100;
 bs.camera=0x20000000;bs.source=0x21000000;bs.encoder=0x24000000;bs.pool=0x25000000;bs.phase=2;bs.changed_ns=stamp-2000000000u;bs.recording_ns=stamp-5000000000u;
}
static void save(unsigned n){
 bh.w[31]=vp_hash(&bh,124);char name[80];snprintf(name,sizeof name,"/buffers-%u.bin",n);FILE*f=fopen(name,"wb");CHECK(f);CHECK(fwrite(&bh,1,sizeof bh,f)==sizeof bh);CHECK(fwrite(br,sizeof*br,bh.w[4],f)==bh.w[4]);
 for(unsigned i=0;i<bh.w[12];i++){if(ms[i].h[2]==BM_READING)ms[i].h[2]=BM_STOPPED;for(unsigned p=0;p<2;p++)ms[i].h[20+p]=vp_hash(ms[i].table[p],BM_BYTES);ms[i].h[511]=vp_hash(ms[i].h,2044);CHECK(fwrite(ms+i,1,sizeof*ms,f)==sizeof*ms);}CHECK(!fclose(f));
}
int main(void){
 CHECK(BV_END==18560000u);CHECK(physical_allowed(0x2081040cu,4));CHECK(!physical_allowed(0x20810440u,4));CHECK(!physical_allowed(0x20821050u,4));
 CHECK(!physical_allowed(0x2081040cu,8));
 CHECK(!physical_allowed(BM_TABLE-4,4096));
 CHECK(!physical_allowed(BM_TABLE+BM_BYTES-4,4096));
 CHECK(physical_allowed(BM_TABLE,4096));
 for(unsigned c=0;c<=16;c++){
  init(c);struct vp_reader reader={NULL,fake_phys,fake_proc,0x10000000,0x11000000};
  struct vf_header fast;static struct vf_record fr[VF_CAPACITY];vf_init(&fast);CHECK(vf_capture(&reader,&fast,fr,sim_now,NULL));CHECK(((fr[fast.count-1].valid>>4)&15u)==15u);
  for(unsigned j=0;j<7;j++){uint64_t begin=stamp;bv_tick(&reader,&bh,br,&bs,sim_now,sim_cpu,sim_wait,NULL,begin+100000000u);if(stamp<begin+100000000u)stamp=begin+100000000u;}
  CHECK(bh.w[4]>0&&bh.w[5]>0);if(c==0)CHECK(bh.w[4]<40);
  if(c==0||c==4||c==5||c==6||c==7){
   unsigned phases=c==0?4:1;
   for(unsigned phase=1;phase<=phases;phase++){
    bs.phase=phase==1||phase==4?1:2;source[0x164/4]=bs.phase;bs.changed_ns=stamp-2000000000u;bs.recording_ns=phase>=3?stamp-31000000000ULL:phase==2?stamp-4000000000ULL:0;
    if(phase==4)bs.recording_ns=1;bs.window_begin=0;
    for(unsigned k=0;k<300;k++){
     unsigned before=bh.w[12];bm_tick(&reader,&bh,ms,&bs,sim_now,sim_cpu,NULL,stamp+100000000u);
     if(c==5&&bs.memory_stage){bs.phase=2;source[0x164/4]=2;}
     if(c==7&&bs.memory_stage)stamp+=26000000000ULL;else stamp+=100000000u;
     if(bh.w[12]&&(!bs.memory_stage)&&(bh.w[12]>before||k>0))break;
    }
   }
   CHECK(bh.w[12]==phases);if(c==0)for(unsigned k=0;k<4;k++)CHECK(ms[k].h[2]==BM_COMPLETE);
   if(c==5)CHECK(ms[0].h[2]==BM_CHANGED_PHASE);if(c==6)CHECK(ms[0].h[2]==BM_READ_FAILED);if(c==7)CHECK(ms[0].h[2]==BM_TIMEOUT);
  }
  save(c);printf("PASS buffer case%u vectors%u snapshots%u physical_reads%u process_reads%u\n",c,bh.w[4],bh.w[12],phys_calls,proc_calls);
 }
 bh.w[4]=BV_CAPACITY-39;bs.window_begin=0;struct vp_reader reader={NULL,fake_phys,fake_proc,0x10000000,0x11000000};bv_tick(&reader,&bh,br,&bs,sim_now,sim_cpu,sim_wait,NULL,stamp+100000000u);CHECK(bh.w[6]&1u);
 return 0;
}
