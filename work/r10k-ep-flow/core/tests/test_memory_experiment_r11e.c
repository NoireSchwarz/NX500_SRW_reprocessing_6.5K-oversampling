/* Production engine and exact ARM read kernel; clocks and OS are explicit models. */
#define main collector_main_not_called
#include "../src/nxl-video-probe.c"
#undef main
#define CHECK(x) do{if(!(x)){fprintf(stderr,"memory case%u line%d\n",scenario,__LINE__);exit(9);}}while(0)
static unsigned scenario,calls,stop_now,frozen,slow;
static uint64_t wall,cpu_clock;
static struct bv_header bh;
static struct bv_record br[BV_CAPACITY];
static struct bv_state bs;
static struct dm_state ds;
static uint32_t data[DM_BYTES/4];
static uint64_t tick_now(void*p){(void)p;return wall;}
static uint64_t tick_cpu(void*p){(void)p;return cpu_clock;}
static uint32_t batch(void*p,unsigned large,unsigned iterations){(void)p;CHECK(iterations==2048u);calls++;
 if(!frozen){wall+=slow?5000000u:2050000u;cpu_clock+=2000000u;}return calls^(large?0x71834891u:0x18a531u);}
static int should_stop(void*p){(void)p;return stop_now!=0;}
static uint32_t after_native(void*p){(void)p;return 10u+(unsigned)(wall/43000000u);}
static struct dm_io io={NULL,tick_now,tick_cpu,batch,should_stop,after_native};
static void reset(unsigned seed){
 memset(&bh,0,sizeof bh);memset(br,0,sizeof br);memset(&bs,0,sizeof bs);bv_init(&bh,&bs);
 bh.w[1]=7;bh.w[7]=2;bh.w[8]=32;bh.w[9]=80;bh.w[10]=10000;bh.w[23]=71;bh.w[24]=123;bh.w[25]=2000000000u;bh.w[27]=247;bh.w[28]=317;bh.w[30]=0x28000000;
 wall=20000000000ULL;cpu_clock=1000000000ULL;calls=stop_now=frozen=slow=0;dm_init(&ds,seed,1);
 ds.telemetry_ns=wall;ds.live_valid=1;ds.last_live[0]=100;ds.last_live[1]=0x2804c608;ds.last_live[2]=101;ds.last_live[4]=2;ds.last_live[6]=107;ds.last_live[7]=0x2804c9c8;ds.ready_flags=15;ds.initial_free_kb=65536;ds.free_kb=65536;ds.cpu_khz=400000;ds.governor=2;ds.ddr_a=700000000;ds.ddr_status=1;ds.cache_incomplete=1;
 for(unsigned i=0;i<8;i++)bs.key[i]=100+i;bs.phase=2;bs.source=101;bs.encoder=102;bs.changed_ns=wall-DM_WARM_NS;
 CHECK(dm_meta(&ds,&bh,br,wall,DM_CONFIG));
}
static void save(void){
 char path[64];snprintf(path,sizeof path,"/memory-%u.bin",scenario);FILE*f=fopen(path,"wb");CHECK(f!=NULL);
 dm_publish(&ds,&bh);bh.w[31]=vp_hash(&bh,124);CHECK(fwrite(&bh,1,sizeof bh,f)==sizeof bh);CHECK(fwrite(br,sizeof br[0],bh.w[4],f)==bh.w[4]);CHECK(!fclose(f));
}
static uint32_t expected(unsigned n,uint32_t mask,uint32_t cursor){
 uint32_t v=0;for(unsigned i=0;i<n;i++){unsigned at=(cursor&mask)/4u;v^=data[at]^data[at+7];cursor+=32;}return v;
}
int main(void){
 for(unsigned i=0;i<DM_BYTES/4;i++)data[i]=i*2654435761u^0x7251c9a3u;
 uint32_t cursor=DM_BYTES-64u,want=expected(4,DM_BYTES-1u,cursor);
 CHECK(dm_read32(data,4,DM_BYTES-1u,&cursor)==want&&cursor==DM_BYTES+64u);
 cursor=DM_SMALL-64u;want=expected(4,DM_SMALL-1u,cursor);CHECK(dm_read32(data,4,DM_SMALL-1u,&cursor)==want&&cursor==DM_SMALL+64u);
 CHECK(dm_read32(data,0,DM_SMALL-1u,&cursor)==0);
 CHECK(sizeof(struct dm_runtime)+sizeof(struct vz_state)+sizeof(struct vz_runtime)<=16384u);
 puts("KERNEL actual ARM LDM8 wrap/extent/checksum PASS");
 for(scenario=0;scenario<8;scenario++){
  reset(scenario);uint64_t origin=wall;
  for(unsigned i=0;i<=1920;i++){wall=origin+i*100000000ULL;ds.telemetry_ns=wall;dm_tick(&ds,&bh,br,&io,&bs,1,i,i+1,wall+100000000ULL);}
  CHECK(ds.status==DM_DONE&&ds.ticks==1920&&dm_suppressed(&ds));
  for(unsigned block=0;block<8;block++){unsigned seen=0;for(unsigned k=0;k<4;k++)seen|=1u<<dm_mode(block*4+k,scenario);CHECK(seen==15);}
  dm_finish(&ds,&bh,br,wall+1000000u);CHECK(ds.finalized);save();
 }
 scenario=8;reset(1);bs.changed_ns=wall;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_READY&&!calls);
 wall+=DM_WARM_NS-1;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_READY&&!calls);
 wall++;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_RUNNING&&calls);dm_finish(&ds,&bh,br,wall);save();
 scenario=9;reset(1);dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+10000000);CHECK(!calls&&ds.pending.d[7+12]==DM_DEADLINE_SKIP);dm_finish(&ds,&bh,br,wall);save();
 scenario=10;reset(1);slow=1;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.pending.d[7+12]&DM_WALL_CLAMP);dm_finish(&ds,&bh,br,wall);save();
 scenario=11;reset(1);stop_now=1;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_ABORTED&&!calls);dm_finish(&ds,&bh,br,wall);save();
 scenario=12;reset(1);dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);unsigned first=calls;wall+=100000000;bs.phase=1;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_ABORTED&&calls==first);bs.phase=2;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(calls==first);dm_finish(&ds,&bh,br,wall);save();
 scenario=13;reset(1);ds.free_kb=8191;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_ABORTED&&!calls);dm_finish(&ds,&bh,br,wall);save();
 scenario=14;reset(1);dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);first=calls;wall+=100000000;dm_tick(&ds,&bh,br,&io,&bs,0,0,0,wall+100000000);CHECK(calls==first);dm_finish(&ds,&bh,br,wall);save();
 scenario=15;reset(1);frozen=1;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(calls==8192&&ds.pending.d[7+12]&DM_BATCH_CAP);dm_finish(&ds,&bh,br,wall);save();
 scenario=16;reset(1);ds.status=DM_UNAVAILABLE;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(!calls);
 scenario=17;reset(1);bh.w[4]=600;dm_tick(&ds,&bh,br,&io,&bs,1,0,0,wall+100000000);CHECK(ds.status==DM_UNAVAILABLE&&!calls);
 scenario=18;struct dm_runtime runtime;memset(&runtime,0,sizeof runtime);
 runtime.allocation=(void*)0x12345000u;runtime.data=(uint32_t*)0x12346000u;runtime.allocation_bytes=DM_BYTES+8192u;
 dm_runtime_release(&runtime);CHECK(!runtime.allocation&&!runtime.data&&!runtime.allocation_bytes);
 scenario=19;reset(0);bh.w[12]=1;dm_runtime_init(&runtime,0,(void*)0x12345000u,&bh);
 CHECK(runtime.state.status==DM_UNAVAILABLE&&runtime.state.reason==DM_ARENA_IN_USE&&!runtime.data);
 CHECK(DM_ARENA_GUARD==12787712u&&DM_ARENA_DATA==12791808u&&DM_ARENA_END==16990208u&&DM_ARENA_END<=BV_END);
 puts("ENGINE 20 scheduling/lifecycle/guard groups PASS; no physical hardware test");
 return 0;
}

