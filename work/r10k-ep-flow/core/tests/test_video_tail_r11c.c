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

static unsigned zs_case,zs_maps,zs_slot_reads,zs_freed_reads,zs_after_free;
static uint64_t zcpu(void*p){(void)p;return zs_case==12?(cpuns/400000u)*400000u:cpuns;}
static int zs_phys(void*p,uint32_t a,void*out,unsigned n){
 if(a>=BM_TABLE&&(uint64_t)a+n<=BM_TABLE+BM_BYTES&&zs_case==12){
  CHECK(((a&4095u)+n)<=4096u);cpuns+=200000u;
 }
 if(zs_case==10)cpuns+=70000u;
 uint32_t mask=(1u<<7)|(1u<<15)|0x401u;unsigned age=(unsigned)((stamp-origin)%42000000u);
 if(a==0x208210c8u&&n==4){uint32_t v=zs_case==1||zs_case==2?0:(age>=8000000u?1u<<15:0)|(age>=26000000u?1u<<7:0);memcpy(out,&v,4);return 1;}
 if(a==0x817d8aacu&&n==4){if(zs_case==1||zs_case==2)mask=0x401;memcpy(out,&mask,4);return 1;}
 uint32_t value=a==0x817d8adcu?0x801ca9a8u:a==0x8153cb84u?0x82560000u:a==0x817d8afcu?0x801ca534u:a==0x8153cb48u?0x82561000u:0;
 if(value&&n==4){memcpy(out,&value,4);return 1;}
 if(n>4u&&n<=24u&&a>=0x20810000u&&a<0x20822000u&&a!=0x20821900u){
  for(unsigned i=0;i<n/4u;i++)if(!fake_phys(p,a+4*i,(uint32_t*)out+i,4))return 0;return 1;
 }
 int ok=fake_phys(p,a,out,n);
 if(zs_case==18&&a==VP_LATENCY_DATA&&n==64u&&++pass_reads%2u==0)((uint32_t*)out)[9]^=4096u;
 if(zs_case==23&&a==VP_LATENCY_DATA&&n==64u)memset(out,0,n);
 if(zs_case==24&&a==VP_LATENCY_DATA&&n==64u){((uint32_t*)out)[2]=99;((uint32_t*)out)[5]=109200000;}
 if(zs_case==26&&a==VP_LATENCY_DATA&&n==64u)((uint32_t*)out)[11]^=4096u;
 if(zs_case==9&&a==0x20810418u&&n==4)*(uint32_t*)out=4;
 return ok;
}
static int zs_proc(void*p,uint32_t a,void*out,unsigned n){
 if(zs_case==10)cpuns+=70000u;refresh();hal[0x174/4]=current_req-1u;hal[0x54/4]=9;
 if(zs_case==14)source[0]=0x11059c20u;
 if(a==0x1005b0f8u&&n==4){*(uint32_t*)out=0x30000000u;return 1;}
 if(a==0x30000000u&&n==4){*(uint32_t*)out=0x30000100u;return 1;}
 if(a==0x30000100u&&n==16){uint32_t w[4]={UINT32_MAX,0,0,37};memcpy(out,w,16);return 1;}
 if(a==0x2f000000u&&n==256){memset(out,0,n);((uint32_t*)out)[3]=0x1002cc74u;((uint32_t*)out)[7]=zs_case==6?0x1002cc70u:0x100260d0u;return 1;}
 int ok=fake_proc(p,a,out,n);
 if(a>=0x27000000u&&a<0x27000000u+64u*112u){
  if(n!=112u)zs_after_free=0;
  else{
   zs_slot_reads++;zs_after_free=0;
   if(zs_case==8&&zs_slot_reads%2u==0){uint32_t*w=out;w[1]=w[2]=w[3]=w[24]=w[26]=0;zs_after_free=1;}
   if(zs_case==17)((uint32_t*)out)[0]^=1;
  }
 }
 if(a>=0x28000000u&&a<0x28001000u&&n==64u){
  if(zs_after_free)zs_freed_reads++;
  uint32_t*w=out;w[1]=w[3]=3840;w[2]=w[4]=2160;
  w[11]-=0x11058000u;w[13]-=0x11058000u;if(zs_case==7)w[11]+=4096u;
 }
 return ok;
}
static int zs_map(void*p,struct vz_maps*m){
 (void)p;memset(m,0,sizeof*m);zs_maps++;m->count=1;m->status=1;m->full_hash=zs_maps;
 m->map[0]=(struct vz_mapping){0x82fa8000u,0xaefa8000u,0x94000000u,1};
 m->pthread_base=zs_case==13?0:0x12000000u;m->pthread_layout_verified=zs_case!=13;
 if(zs_case==3&&zs_maps%2u==0)m->map[0].hi-=4096u;return 1;
}
static int zs_list(void*p,uint32_t*t,unsigned cap,unsigned*n){(void)p;CHECK(cap>=3);t[0]=801;t[1]=802;t[2]=803;*n=3;return 1;}
static int zs_task(void*p,uint32_t tid,struct vz_task*t){
 (void)p;memset(t,0,sizeof*t);t->tid=tid;t->start_ticks=77;
 if(tid!=803){t->status=VZ_RUNNING;return 1;}
 t->status=VZ_SYS;t->nr=240;t->arg[0]=zs_case==5?0x30000200u:0x3000010cu;t->arg[1]=zs_case==20?129:128;t->arg[2]=37;t->sp=0x2f000000u;t->pc=zs_case==19?0x1200be38u:0x1200bd04u;
 if(zs_case==15){t->nr=54;t->arg[0]=9;t->arg[1]=0xc10c7408;t->arg[2]=0x2300005cu;}
 return 1;
}
static int zs_select(void*p,uint32_t tid,uint64_t tick){(void)p;(void)tid;(void)tick;return zs_case!=4;}
static int zs_fd(void*p,unsigned fd){(void)p;return fd==9&&zs_case!=16;}
static uint32_t zs_pthread(void*p){(void)p;return zs_case==13?0:0x12000000u;}
static int zs_pixel(void*p,uint32_t y,uint32_t uv,uint32_t a,void*out,unsigned n){
 (void)p;CHECK(vz_pixel_allowed(y,uv,a,n));if(zs_case==10)cpuns+=70000u;
 unsigned row=0;while(row<4u&&vz_pixel_address(y,uv,row)!=a)row++;CHECK(row<4);
 if(zs_case==11&&row==2u)return 0;
 unsigned age=(unsigned)((stamp-origin)%42000000u);uint32_t thresholds[4]={5000000u,12000000u,25000000u,28000000u};
 uint32_t value=zs_case==2?123u:age>=thresholds[row]?current_req:current_req-10u;
 for(unsigned i=0;i<n/4;i++)((uint32_t*)out)[i]=value+row+i;return 1;
}

static unsigned tc,pixel_n,tail_pass,second_mmio,debug_reads;
static int t_proc(void*p,uint32_t a,void*out,unsigned n){
 if(tc==4&&tail_pass&&pixel_n>=6&&a==0x1005b0e4u){*(uint32_t*)out=0;return 1;}
 if(tc==20&&tail_pass&&pixel_n>=6&&a==0x1005b144u){*(uint32_t*)out=0x23000004;return 1;}
 int ok=zs_proc(p,a,out,n);
 if(tc==21&&tail_pass&&pixel_n>=6&&a==0x1005b148u&&n==32u)((uint32_t*)out)[2]+=4096;
 if(tc==23&&tail_pass&&pixel_n>=6&&a==0x23000108u&&n==112u)((uint32_t*)out)[27]++;
 return ok;
}
static int t_phys(void*p,uint32_t a,void*out,unsigned n){
 if(tail_pass&&pixel_n>=6&&a>=0x20810000u&&a<0x20822000u)second_mmio++;
 if(a==0x208210e0u){debug_reads++;*(uint32_t*)out=0x12345678;return 1;}
 if(a==0x208210d0u&&n==12u){if(tc==10&&pixel_n>=6)return 0;memset(out,0,n);return 1;}
 if(a==VP_LATENCY_DATA&&n==64u&&((tc==16)||(tc==18&&tail_pass&&pixel_n>=6))){memset(out,0,n);return 0;}
 int ok=zs_phys(p,a,out,n);
 if(a==VP_LATENCY_DATA&&n==64u&&tc==17)((uint32_t*)out)[3]&=~512u;
 if(a==0x20821900u&&n==20u){
  if(tc==7)((uint32_t*)out)[2]=0x780051;
  if(tc==8){((uint32_t*)out)[0]=0x20810440;((uint32_t*)out)[1]=0x20810440+3840*2160;}
 }
 if(a==0x2082110cu&&n==4u&&tc==9)*(uint32_t*)out=0x90660;
 if(a==0x20810908u&&n==4u&&tc==14&&pixel_n>=6)*(uint32_t*)out^=4096;
 if(a==0x8146e438u&&n==4u&&tc==15&&tail_pass&&pixel_n>=6)*(uint32_t*)out^=1;
 if(a==0x208210b0u&&n==4u)*(uint32_t*)out=tc==11?(pixel_n>=6?3:2):tc==13?2:0;
 return ok;
}
static int t_pixel(void*p,uint32_t y,uint32_t uv,uint32_t a,void*out,unsigned n){
 (void)p;CHECK(vz_pixel_allowed(y,uv,a,n));unsigned i;for(i=0;i<6;i++)if(vt_pixel_address(y,uv,i)==a)break;CHECK(i<6);
 pixel_n++;if(tc==6&&i==1)return 0;
 unsigned threshold[6]={35000000u,37000000u,39000000u,38000000u,40000000u,41000000u};
 uint32_t value=tc==22?123u:(stamp-origin)%42000000u>=threshold[i]?current_req:current_req-10;
 for(unsigned j=0;j<n/4u;j++)((uint32_t*)out)[j]=value+i+j;
 if(tail_pass&&pixel_n==6){
  if(tc==1||tc==2)stamp=origin+(tc==1?42000000u:84000000u)+200000u;
  if(tc==3)source[0x164/4]=1;if(tc==5)hal[0]=0;
 }
 return 1;
}
static int any_pixel(void*p,uint32_t y,uint32_t uv,uint32_t a,void*out,unsigned n){
 (void)p;CHECK(vz_pixel_allowed(y,uv,a,n));for(unsigned i=0;i<n/4;i++)((uint32_t*)out)[i]=a+i;return 1;
}
int main(void){
 for(tc=0;tc<24;tc++){
  init(13);zs_case=0;tail_pass=pixel_n=second_mmio=debug_reads=0;bh.w[1]=5;memset(br,0,sizeof br);
  if(tc==19)source[0x164/4]=0;
  struct vp_reader r={NULL,t_phys,t_proc,0x10000000u,0x11000000u};
  struct vz_io io={NULL,zs_map,zs_list,zs_task,zs_select,zs_fd,t_pixel,zs_pthread,NULL};
  struct vz_state v;vz_init(&bh,&v);bh.w[1]=5;bs.window=v.cold_window=++bh.w[5];bs.window_begin=stamp;
  zs_map(NULL,&v.cold_map);v.cold_map.scan_begin_ns=stamp-100000u;v.cold_map.scan_end_ns=stamp-1000u;v.cold_map.epoch=1;v.cold_map.lines=1;v.cold_map.bytes=66;
  vz_async_map(&r,&bh,br,&io,&v,sim_now,NULL,1,1);
  stamp=origin+25000000u;vt_capture(&r,&bh,br,&bs,&io,sim_now,NULL,UINT32_MAX);
  tail_pass=1;pixel_n=0;stamp=origin+40000000u;vt_capture(&r,&bh,br,&bs,&io,sim_now,NULL,UINT32_MAX);
  if(tc==3||tc==4||tc==5||tc==20)CHECK(second_mmio==0);
  if(tc==7||tc==8||tc==9||tc==16||tc==17||tc==19)CHECK(pixel_n==0);
  if(tc==12)CHECK(!debug_reads);if(tc==13)CHECK(debug_reads==2);
  stamp+=1000;v.cold_map.scan_begin_ns=stamp;stamp+=100000;v.cold_map.scan_end_ns=stamp;v.cold_map.epoch=2;
  vz_async_map(&r,&bh,br,&io,&v,sim_now,NULL,2,1);
  bs.count=2;bv_end_window(&bh,br,&bs,stamp,3);save(tc);
  printf("PASS tail%u records%u\n",tc,bh.w[4]);
 }
 unsigned reads[2];uint32_t identities[2][8];
 for(unsigned i=0;i<2;i++){
  init(13);zs_case=0;tc=0;struct vp_reader r={NULL,zs_phys,zs_proc,0x10000000u,0x11000000u};
  struct vz_io io={NULL,zs_map,zs_list,zs_task,zs_select,zs_fd,any_pixel,zs_pthread,NULL};struct vz_state v;vz_init(&bh,&v);bh.w[1]=4+i;
  bs.window=++bh.w[5];unsigned before=proc_calls;vz_stage(&r,&bh,br,&bs,&v,&io,sim_now,NULL,0);reads[i]=proc_calls-before;
  memcpy(identities[i],br[0].d+58,32);CHECK(br[0].flags&VZ_IA);CHECK(br[0].flags&VZ_IB);
 }
 CHECK(reads[1]+8<=reads[0]&&!memcmp(identities[0],identities[1],32));printf("COST before%u after%u\n",reads[0],reads[1]);
 init(13);bh.w[4]=BV_CAPACITY;unsigned before=phys_calls;
 struct vp_reader r={NULL,t_phys,t_proc,0x10000000u,0x11000000u};struct vz_io io={0};vt_capture(&r,&bh,br,&bs,&io,sim_now,NULL,0);
 CHECK(bh.w[4]==BV_CAPACITY&&(bh.w[6]&1u)&&phys_calls==before);
 CHECK(vz_pixel_allowed(0xad610000u,0xaddf9000u,vt_pixel_address(0xad610000u,0xaddf9000u,1),16u));
 CHECK(!vz_pixel_allowed(0xad610000u,0xaddf9000u,vt_pixel_address(0xad610000u,0xaddf9000u,1)+4,16u));
 CHECK(!physical_allowed(0x208210ccu,12));CHECK(!physical_allowed(0x208210d4u,12));CHECK(!physical_allowed(0x20821050u,12));
 return 0;
}
