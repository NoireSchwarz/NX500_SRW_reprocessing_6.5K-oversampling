/* SPDX-License-Identifier: AGPL-3.0-only.
 * R11e reused Linux working-set intervention. No camera buffer, device write,
 * clock/IRQ/QoS change, thread, fork or target call.
 * Logical loads are NOT measured DDR bus traffic.
 */
#ifndef NXL_VIDEO_MEMORY_EXPERIMENT_H
#define NXL_VIDEO_MEMORY_EXPERIMENT_H
#define DM_BYTES (4u*1024u*1024u)
#define DM_ADMISSION_KB 16384u
#define DM_ARENA_GUARD ((BM_OFFSET+4095u)&~4095u)
#define DM_ARENA_DATA (DM_ARENA_GUARD+4096u)
#define DM_ARENA_END (DM_ARENA_DATA+DM_BYTES+4096u)
_Static_assert(DM_ARENA_GUARD>=BM_OFFSET && DM_ARENA_END<=BV_END,"Own inactive arena bounds");
_Static_assert((DM_ARENA_DATA&4095u)==0,"Page alignment");
#define DM_SMALL 4096u
#define DM_CPU_NS 20000000u
#define DM_LOW_NS 5000000u
#define DM_WALL_NS 30000000u
#define DM_EPOCH_NS 6000000000ULL
#define DM_WARM_NS 10000000000ULL
#define DM_EPOCHS 32u
#define DM_MAGIC 0x31504d44u
enum {DM_META=10,DM_TICKS=11};
enum {DM_READY=1,DM_RUNNING=2,DM_DONE=3,DM_ABORTED=4,DM_UNAVAILABLE=5};
enum {DM_CONFIG=1,DM_START=2,DM_EPOCH=3,DM_END=4,DM_ABORT=5,DM_FINAL=6};
enum {DM_BASELINE=0,DM_SMALL_ONLY=1,DM_MIXED=2,DM_LARGE_ONLY=3};
enum {DM_DID_LOAD=1,DM_DEADLINE_SKIP=2,DM_WALL_CLAMP=4,DM_NO_NATIVE=8,
      DM_INTERRUPTED=16,DM_BATCH_CAP=32,DM_NO_ROOM=64};
enum {DM_PHASE_CHANGED=1,DM_STOPPED=2,DM_LOW_MEMORY=3,DM_LOG_FULL=4,DM_CLOCK_FAILED=5,DM_ARENA_IN_USE=6};
struct dm_io {
 void*context;uint64_t(*now)(void*),(*cpu)(void*);
 uint32_t(*batch)(void*,unsigned,unsigned);int(*stop)(void*);uint32_t(*native_after)(void*);
};
struct dm_state {
 uint32_t status,reason,epoch,seed,started,finalized,cursor,checksum,ticks,groups;
 uint32_t ready_flags,initial_free_kb,cache_bytes,cache_entries,cache_incomplete,pagemap_status;
 uint32_t PA_min,PA_max,present_pages,PFN_hash,PA_samples[8];
 uint32_t cpu_khz,governor,free_kb,ddr_a,ddr_status;
 uint64_t start_ns,finish_ns,telemetry_ns,total_CPU_ns,total_large_bytes,total_small_bytes;
 uint32_t key[8],last_live[8],live_valid;
 struct bv_record pending;
};
/* Exactly the same read kernel for both working sets, including address masking.
 * Each iteration executes eight architectural 32-bit loads, not a simulated
 * bandwidth counter. A 32-byte aligned cursor cannot cross the selected extent.
 */
static uint32_t dm_read32(const uint32_t*base,unsigned iterations,uint32_t mask,uint32_t*cursor){
 uint32_t at=*cursor,out;if(!iterations)return 0;
#if defined(__arm__)
 __asm__ volatile(
  "mov r12, #0\n"
  "1: and r2, %[at], %[mask]\n"
  "add r2, %[base], r2\n"
  "ldmia r2, {r3-r10}\n"
  "eor r12, r12, r3\n"
  "eor r12, r12, r10\n"
  "add %[at], %[at], #32\n"
  "subs %[n], %[n], #1\n"
  "bne 1b\n"
  "mov %[out], r12\n"
  : [at]"+r"(at),[n]"+r"(iterations),[out]"=r"(out)
  : [base]"r"(base),[mask]"r"(mask)
  : "r2","r3","r4","r5","r6","r7","r8","r9","r10","r12","cc","memory");
#else
 out=0;for(unsigned i=0;i<iterations;i++){const volatile uint32_t*p=(const volatile uint32_t*)((const unsigned char*)base+(at&mask));for(unsigned j=0;j<8;j++)out^=p[j];at+=32u;}
#endif
 *cursor=at;return out;
}
static unsigned dm_mode(unsigned epoch,unsigned seed){
 static const unsigned latin[4][4]={{0,1,3,2},{1,2,0,3},{2,3,1,0},{3,0,2,1}};
 unsigned block=epoch/4u,within=epoch%4u;
 unsigned row=(block+(seed&3u))%4u;
 if(seed&4u)within=3u-within;
 return latin[row][within];
}
static void dm_init(struct dm_state*d,unsigned seed,int available){
 memset(d,0,sizeof*d);d->status=available?DM_READY:DM_UNAVAILABLE;d->seed=seed&7u;d->epoch=UINT32_MAX;d->PA_min=UINT32_MAX;
}
static int dm_flush(struct dm_state*d,struct bv_header*h,struct bv_record*r,uint64_t stamp){
 unsigned n=d->pending.d[2];if(!n)return 1;
 d->pending.begin_ns=d->pending.end_ns=stamp;d->pending.kind=DM_TICKS;
 d->pending.d[0]=DM_MAGIC;d->pending.d[1]=2u;d->pending.d[3]=d->checksum;
 int ok=bv_append(h,r,&d->pending);memset(&d->pending,0,sizeof d->pending);if(ok)d->groups++;return ok;
}
static int dm_meta(struct dm_state*d,struct bv_header*h,struct bv_record*r,uint64_t stamp,unsigned event){
 if(!dm_flush(d,h,r,stamp))return 0;
 struct bv_record z;memset(&z,0,sizeof z);z.begin_ns=z.end_ns=stamp;z.kind=DM_META;
 uint32_t*w=z.d;w[0]=DM_MAGIC;w[1]=2;w[2]=event;w[3]=d->status;w[4]=DM_BYTES;w[5]=DM_SMALL;
 w[6]=DM_CPU_NS;w[7]=DM_LOW_NS;w[8]=100;w[9]=6000;w[10]=8;w[11]=DM_EPOCHS;w[12]=2000;w[13]=300;
 w[14]=d->seed;w[15]=d->ready_flags;w[16]=d->initial_free_kb;w[17]=d->cache_bytes;w[18]=d->cache_entries;w[19]=d->cache_incomplete;
 w[20]=d->pagemap_status;w[21]=d->PA_min;w[22]=d->PA_max;w[23]=d->present_pages;w[24]=d->PFN_hash;w[25]=DM_BYTES/4096u;
 memcpy(w+26,d->PA_samples,sizeof d->PA_samples);
 w[34]=DM_WALL_NS;w[35]=96;w[36]=1;w[37]=d->reason;w[38]=d->epoch;w[39]=d->ticks;
 w[40]=(uint32_t)d->start_ns;w[41]=(uint32_t)(d->start_ns>>32);w[42]=(uint32_t)d->finish_ns;w[43]=(uint32_t)(d->finish_ns>>32);
 w[44]=(uint32_t)d->total_CPU_ns;w[45]=(uint32_t)(d->total_CPU_ns>>32);
 w[46]=(uint32_t)d->total_large_bytes;w[47]=(uint32_t)(d->total_large_bytes>>32);
 w[48]=(uint32_t)d->total_small_bytes;w[49]=(uint32_t)(d->total_small_bytes>>32);
 memcpy(w+50,d->key,sizeof d->key);for(unsigned i=0;i<DM_EPOCHS;i++)w[58+i]=dm_mode(i,d->seed);
 w[90]=d->groups;w[91]=d->checksum;w[92]=d->live_valid;memcpy(w+93,d->last_live,sizeof d->last_live);
 w[101]=DM_ARENA_GUARD;w[102]=DM_ARENA_DATA;w[103]=DM_ARENA_END;w[104]=0;w[105]=DM_ADMISSION_KB;w[106]=BV_END;w[107]=0;
 return bv_append(h,r,&z);
}
static void dm_abort(struct dm_state*d,struct bv_header*h,struct bv_record*r,uint64_t stamp,unsigned why){
 if(d->status!=DM_RUNNING&&d->status!=DM_READY)return;
 d->status=DM_ABORTED;d->reason=why;d->finish_ns=stamp;(void)dm_meta(d,h,r,stamp,DM_ABORT);
}
#define DM_STATUS_TAG 0xe1000000u
static void dm_publish(const struct dm_state*d,struct bv_header*h){
 unsigned progress=d->status==DM_DONE?DM_EPOCHS:d->started&&d->epoch<DM_EPOCHS?d->epoch+1u:0;
 __atomic_store_n(h->w+20,DM_STATUS_TAG|d->status|(d->reason<<8)|(progress<<16),__ATOMIC_RELEASE);
}
static int dm_suppressed(const struct dm_state*d){return d->started!=0;}
static void dm_tick(struct dm_state*d,struct bv_header*h,struct bv_record*r,const struct dm_io*io,
 const struct bv_state*b,int native_ok,uint32_t native_before,uint32_t native_after,uint64_t deadline){
 uint64_t now=io->now(io->context);
 if(d->status==DM_READY){
  if(b->phase!=2u||!b->source||!b->encoder||!b->changed_ns||now<b->changed_ns||now-b->changed_ns<DM_WARM_NS||!native_ok)return;
  if(h->w[4]+400u>=BV_CAPACITY){d->status=DM_UNAVAILABLE;d->reason=DM_LOG_FULL;(void)dm_meta(d,h,r,now,DM_ABORT);return;}
  memcpy(d->key,b->key,32);d->status=DM_RUNNING;d->started=1;d->start_ns=now;
  if(!dm_meta(d,h,r,now,DM_START)){dm_abort(d,h,r,now,DM_LOG_FULL);return;}
 }
 if(d->status!=DM_RUNNING)return;
 if(b->phase!=2u||memcmp(b->key,d->key,32)){dm_abort(d,h,r,now,DM_PHASE_CHANGED);return;}
 if(d->free_kb<8192u){dm_abort(d,h,r,now,DM_LOW_MEMORY);return;}
 if(now<d->start_ns){dm_abort(d,h,r,now,DM_CLOCK_FAILED);return;}
 unsigned epoch=(unsigned)((now-d->start_ns)/DM_EPOCH_NS);
 if(epoch>=DM_EPOCHS){d->status=DM_DONE;d->finish_ns=now;(void)dm_meta(d,h,r,now,DM_END);return;}
 if(d->epoch!=epoch){d->epoch=epoch;if(!dm_meta(d,h,r,now,DM_EPOCH)){dm_abort(d,h,r,now,DM_LOG_FULL);return;}}
 unsigned mode=dm_mode(epoch,d->seed),flags=0,batches=0;
 uint64_t start=io->now(io->context),cpu0=io->cpu(io->context),large_end=start,large_cpu=0,large=0,small=0,cost=0,end=start;
 if(!native_ok)flags|=DM_NO_NATIVE;
 else if(mode&&(!cpu0||deadline<=start+32000000u))flags|=!cpu0?DM_INTERRUPTED:DM_DEADLINE_SKIP;
 else if(mode){
  uint64_t large_goal=mode==DM_LARGE_ONLY?DM_CPU_NS:mode==DM_MIXED?DM_LOW_NS:0;
  for(;batches<8192u;batches++){
   uint64_t wall=io->now(io->context),c=io->cpu(io->context);
   if(!wall||c<cpu0||wall<start){flags|=DM_INTERRUPTED;break;}
   cost=c-cpu0;if(cost>=DM_CPU_NS)break;
   if(io->stop&&io->stop(io->context)){flags|=DM_INTERRUPTED;break;}
   if(wall-start>=DM_WALL_NS||wall+2000000u>=deadline){flags|=DM_WALL_CLAMP;break;}
   unsigned is_large=cost<large_goal;
   d->checksum^=io->batch(io->context,is_large,2048u);
   if(is_large){large+=65536u;large_end=io->now(io->context);large_cpu=io->cpu(io->context)-cpu0;}
   else small+=65536u;
  }
  if(batches==8192u)flags|=DM_BATCH_CAP;
  if(large||small)flags|=DM_DID_LOAD;
 }
 end=io->now(io->context);uint64_t cpu1=io->cpu(io->context);
 if(end<start||cpu1<cpu0||cpu1-cpu0>UINT32_MAX||end-start>UINT32_MAX){dm_abort(d,h,r,end,DM_CLOCK_FAILED);return;}
 cost=cpu1-cpu0;unsigned n=d->pending.d[2];uint32_t*w=d->pending.d+7+n*24u;
 memset(w,0,96);w[0]=(uint32_t)start;w[1]=(uint32_t)(start>>32);w[2]=(uint32_t)end;w[3]=(uint32_t)(end>>32);
 w[4]=epoch;w[5]=mode;w[6]=(uint32_t)cost;w[7]=(uint32_t)(end-start);w[8]=(uint32_t)large;w[9]=(uint32_t)(large>>32);
 w[10]=(uint32_t)small;w[11]=(uint32_t)(small>>32);w[12]=flags;w[13]=native_before;w[14]=io->native_after?io->native_after(io->context):native_after;
 w[15]=d->cpu_khz;w[16]=d->governor;w[17]=d->free_kb;w[18]=d->ddr_a;w[19]=d->telemetry_ns&&start>=d->telemetry_ns&&(start-d->telemetry_ns)/1000000u<UINT32_MAX?(uint32_t)((start-d->telemetry_ns)/1000000u):UINT32_MAX;
 w[20]=(uint32_t)large_end;w[21]=(uint32_t)(large_end>>32);w[22]=(uint32_t)large_cpu;w[23]=d->ddr_status;
 d->pending.d[2]++;d->ticks++;d->total_CPU_ns+=cost;d->total_large_bytes+=large;d->total_small_bytes+=small;
 if(d->pending.d[2]==10u&&!dm_flush(d,h,r,end))dm_abort(d,h,r,end,DM_LOG_FULL);
 if(flags&DM_INTERRUPTED)dm_abort(d,h,r,end,DM_STOPPED);
}
static void dm_finish(struct dm_state*d,struct bv_header*h,struct bv_record*r,uint64_t stamp){
 if(d->finalized)return;d->finalized=1;
 if(d->status==DM_RUNNING)dm_abort(d,h,r,stamp,DM_STOPPED);
 (void)dm_meta(d,h,r,stamp,DM_FINAL);dm_publish(d,h);
}
#endif

