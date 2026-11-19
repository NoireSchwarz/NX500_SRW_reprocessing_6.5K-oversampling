/* SPDX-License-Identifier: AGPL-3.0-only. R10w native completion markers.
 * Read-only Linux windows of the original RTOS hardware pipeline. No new
 * RTOS entry, IRQ enable/ACK, selector write, timer latch or target call.
 * The disabled HEVC trace RAM/export slot is reused; total RAM is unchanged.
 */
#ifndef NXL_VIDEO_PIPELINE_CAPTURE_H
#define NXL_VIDEO_PIPELINE_CAPTURE_H
#define PL_MAGIC 0x31504c56u
#define PL_XOR 0x59a130c7u
#define PL_CAPACITY 1200u
#define PL_OFFSET HT_OFFSET
#define PL_DATA (PL_OFFSET+128u)
struct pl_header {uint32_t w[32];};
struct pl_record {uint64_t begin_ns,end_ns;uint32_t sequence,burst,kind,flags,data[55],commit;};
enum {PL_SAMPLE=1,PL_ROUTE_A=2,PL_ROUTE_B=3};
enum {PL_BEFORE=1u,PL_AFTER=2u,PL_SAME=4u,PL_ACTIVE=8u,PL_BIT6=16u,
 PL_PENDING=32u,PL_ENABLED=64u,PL_HANDLER=128u,PL_POST_PENDING=256u};
enum {PL_FULL=1u,PL_TIME=2u,PL_CPU=4u,PL_STOP=8u};
/* IPC DMA EOF banks and native CS status/enables; EP CORE status and masks.
 * Mux words are configuration, never interpreted as progress counters. */
static const uint32_t pl_address[16]={0x2081040cu,0x20810408u,0x20810418u,
 0x20810458u,0x2081045cu,0x20810468u,0x208210c8u,0x208210ccu,
 0x817d8aacu,0x817d8ab0u,0x20821104u,0x2082110cu,
 0x20821204u,0x2082120cu,0x20810908u,0x813f09bcu};
/* Exact native EnableInterrupt/owner access offsets, event0..17, both CS.
 * Event0 PATH_CFG_WDMA is NOT a per-channel DMA retirement marker. */
static const uint32_t pl_owner_offset[2][18]={
 {0xa0,0x88,0x84,0x80,0x7c,0x64,0x60,0x5c,0x58,0x54,0,0x44,0x40,0x28,0x24,0x20,0x1c,0x18},
 {0x9c,0x98,0x94,0x90,0x8c,0x78,0x74,0x70,0x6c,0x68,0x50,0x4c,0x48,0x3c,0x38,0x34,0x30,0x2c}};
struct pl_rom_guard {uint32_t address,words[8];};
static const struct pl_rom_guard pl_rom_guards[]={
{0x80036c78u,{0xe92d40f0u,0xe3a05000u,0xe3425081u,0xe5953408u,0xe24dd00cu,0xe3a06000u,0xe58d3004u,0xe58d6000u}},
{0x8003679cu,{0xe3a03000u,0xe350001fu,0xe3423081u,0x95932458u,0x8593245cu,0x82400020u,0x93a01001u,0x83a01001u}},
{0x804bb5f0u,{0xe92d40f0u,0xe3a04a01u,0xe3424082u,0xe59430c8u,0xe24dd00cu,0xe58d3004u,0xe3a03000u,0xe58d3000u}},
{0x804bb6a4u,{0xe92d40f0u,0xe3a04a01u,0xe3424082u,0xe59430ccu,0xe24dd00cu,0xe58d3004u,0xe3a03000u,0xe58d3000u}},
};
static int pl_read_range(uint32_t a,unsigned n){
 if(n!=4u)return 0;
 for(unsigned i=0;i<16;i++)if(a==pl_address[i])return 1;
 return 0;
}
static void pl_init(struct pl_header*h){
 memset(h,0,sizeof*h);h->w[0]=PL_MAGIC;h->w[1]=1;h->w[2]=sizeof(struct pl_record);
 h->w[3]=PL_CAPACITY;h->w[7]=100;h->w[8]=1;h->w[9]=50;h->w[10]=3000;
 h->w[11]=2;h->w[12]=512; /* HEVC breakpoints remain disabled. */
}
static int pl_append(struct pl_header*h,struct pl_record*r,struct pl_record*z){
 if(h->w[4]>=PL_CAPACITY){h->w[6]|=PL_FULL;return 0;}
 z->sequence=h->w[4];z->commit=vp_hash(z,offsetof(struct pl_record,commit))^PL_XOR;
 r[z->sequence]=*z;__sync_synchronize();h->w[4]++;return 1;
}
static int pl_read_header(const struct vp_reader*r,uint32_t*out){
 if(r->physical(r->context,VP_LATENCY_DATA,out,64u))return 1;
 memset(out,255,64u);return 0;
}
static int pl_sample(const struct vp_reader*r,struct pl_header*h,struct pl_record*records,
 uint64_t(*now)(void*),void*c,uint32_t burst,unsigned post,struct pl_record*out){
 struct pl_record z;memset(&z,0,sizeof z);memset(z.data,255,sizeof z.data);
 z.kind=PL_SAMPLE;z.burst=burst;z.begin_ns=now(c);z.data[48]=0;
 if(post)z.flags|=PL_POST_PENDING;
 if(pl_read_header(r,z.data))z.flags|=PL_BEFORE;
 for(unsigned i=0;i<16;i++)if(vp_rw(r,pl_address[i],z.data+32+i))z.data[48]|=1u<<i;
 {static const uint32_t slot[3]={0x813f0c58u,0x813f0bc8u,0x813f0bb8u};
  for(unsigned i=0;i<3;i++)if(vp_rw(r,slot[i],z.data+49+i))z.data[48]|=1u<<(16u+i);
 }
 if(pl_read_header(r,z.data+16))z.flags|=PL_AFTER;
 if((z.flags&3u)==3u&&vp_timer_schema(z.data)==11u&&!memcmp(z.data,z.data+16,64u)){
  z.flags|=PL_SAME;
  if(vp_timer_inflight(z.data)&&(z.data[3]&512u))z.flags|=PL_ACTIVE;
  if(((z.data[3]>>28)&15u)==1u&&!(z.data[3]&0x20000u)&&z.data[14]==0x801cdf4cu)z.flags|=PL_BIT6;
 }
 if((z.data[48]&(1u<<2))&&(z.data[34]&4u))z.flags|=PL_PENDING;
 if((z.data[48]&(1u<<5))&&(z.data[37]&4u))z.flags|=PL_ENABLED;
 if((z.data[48]&(1u<<15))&&z.data[47]==0x801cdf6cu)z.flags|=PL_HANDLER;
 z.end_ns=now(c);h->w[13]++;if(out)*out=z;return pl_append(h,records,&z);
}
static int pl_route(const struct vp_reader*r,struct pl_header*h,struct pl_record*records,
 uint64_t(*now)(void*),void*c,uint32_t burst,unsigned bank,unsigned final){
 struct pl_record z;memset(&z,0,sizeof z);memset(z.data,255,sizeof z.data);
 z.kind=bank?PL_ROUTE_B:PL_ROUTE_A;z.burst=burst;z.flags=final?2u:1u;z.begin_ns=now(c);
 if(pl_read_header(r,z.data))z.flags|=4u;
 uint64_t mask=0;
 for(unsigned i=0;i<18;i++){
  if(vp_rw(r,0x817d8ac0u+bank*112u+i*4u,z.data+16+i))mask|=1ULL<<i;
  if(vp_rw(r,0x8153cb28u+pl_owner_offset[bank][i],z.data+34+i))mask|=1ULL<<(18u+i);
 }
 z.data[52]=(uint32_t)mask;z.data[53]=(uint32_t)(mask>>32);z.data[54]=bank;
 z.end_ns=now(c);return pl_append(h,records,&z);
}
typedef uint64_t(*pl_clock_fn)(void*);
/* One bounded window per3s. Start only inside a HW-bound, single-bit6 request.
 * At most50 paced1ms samples; finish before the next100ms ring deadline.
 * Once CS done-pending is seen, no more than TWO additional pairs are read.
 * Actual wall/CPU spans are measured; sleeping is never counted as HW time.
 */
static void pl_burst(const struct vp_reader*r,struct pl_header*h,struct pl_record*records,
 pl_clock_fn now,pl_clock_fn cpu,void(*wait)(void*),void*c,uint64_t deadline){
 uint64_t start=now(c),last=(uint64_t)h->w[16]|((uint64_t)h->w[17]<<32);
 if(h->w[6]&PL_FULL||start<last||start-last<3000000000ULL||deadline<=start+8000000u)return;
 uint32_t hdr[16];if(!pl_read_header(r,hdr)||vp_timer_schema(hdr)!=11u||!vp_timer_inflight(hdr)||
  !(hdr[3]&512u)||((hdr[3]>>28)&15u)!=1u||(hdr[3]&0x20000u)||hdr[14]!=0x801cdf4cu)return;
 if(h->w[4]+54u>PL_CAPACITY){h->w[6]|=PL_FULL;return;}
 h->w[16]=(uint32_t)start;h->w[17]=(uint32_t)(start>>32);uint32_t burst=++h->w[5];
 uint64_t begun_cpu=cpu(c),limit=deadline-5000000u,own_limit=start+60000000u;
 if(own_limit<limit)limit=own_limit;
 for(unsigned bank=0;bank<2;bank++)if(!pl_route(r,h,records,now,c,burst,bank,0))return;
 unsigned pending=0;struct pl_record z;
 for(unsigned i=0;i<50;i++){
  if(now(c)>=limit){h->w[6]|=PL_TIME;break;}
  if(cpu(c)-begun_cpu>=4000000u){h->w[6]|=PL_CPU;break;}
  if(i)wait(c);
  if(now(c)>=limit){h->w[6]|=PL_TIME;break;}
  if(!pl_sample(r,h,records,now,c,burst,pending,&z))break;
  if((z.flags&(PL_SAME|PL_ACTIVE|PL_BIT6))!=(PL_SAME|PL_ACTIVE|PL_BIT6)||memcmp(z.data,hdr,64u))break;
  if(!(z.data[48]&(1u<<14))||z.data[46]!=hdr[10])break;
  if(pending){pending++;if(pending>=3u||!(z.flags&PL_PENDING))break;}
  else if(z.flags&PL_PENDING){
   if((z.flags&(PL_ENABLED|PL_HANDLER))!=(PL_ENABLED|PL_HANDLER))break;
   pending=1;
  }
 }
 for(unsigned bank=0;bank<2;bank++)if(!pl_route(r,h,records,now,c,burst,bank,1))break;
 uint64_t end_cpu=cpu(c),cost=end_cpu>=begun_cpu?end_cpu-begun_cpu:0,total=(uint64_t)h->w[18]|((uint64_t)h->w[19]<<32);
 total+=cost;h->w[18]=(uint32_t)total;h->w[19]=(uint32_t)(total>>32);
 if(cost>h->w[20])h->w[20]=cost>UINT32_MAX?UINT32_MAX:(uint32_t)cost;
}
_Static_assert(sizeof(struct pl_header)==128,"pipeline header");
_Static_assert(sizeof(struct pl_record)==256,"pipeline marker/raw-header record");
_Static_assert(PL_DATA+PL_CAPACITY*sizeof(struct pl_record)<=HT_END,"reuse disabled HEVC RAM only");
#endif
