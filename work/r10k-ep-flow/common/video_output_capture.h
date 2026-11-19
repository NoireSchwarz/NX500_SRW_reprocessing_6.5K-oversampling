/* SPDX-License-Identifier: AGPL-3.0-only. R10x read-only output association.
 * No RTOS changes, interrupts, ACK, selectors, target calls or HEVC traps.
 * Configuration and submitted-buffer snapshots are NOT hardware busy times.
 */
#ifndef NXL_VIDEO_OUTPUT_CAPTURE_H
#define NXL_VIDEO_OUTPUT_CAPTURE_H
#define PL_MAGIC 0x31504c56u
#define PL_XOR 0x59a130c7u
#define PL_CAPACITY 600u
#define PL_OFFSET HT_OFFSET
#define PL_DATA (PL_OFFSET+128u)
struct pl_header {uint32_t w[32];};
struct pl_record {uint64_t begin_ns,end_ns;uint32_t sequence,burst,kind,flags,data[119],commit;};
enum {PL_SAMPLE=1,PL_ROUTE_A=2,PL_ROUTE_B=3,PL_TARGET=4,PL_BUDGET=5};
enum {PL_BEFORE=1u,PL_AFTER=2u,PL_SAME=4u,PL_ACTIVE=8u,PL_BIT6=16u,
 PL_PENDING=32u,PL_ENABLED=64u,PL_HANDLER=128u,PL_POST_PENDING=256u,
 PL_OUTPUT=512u,PL_IMAGE=1024u,PL_HEVC=2048u,PL_SOURCE=4096u,PL_DEBUG=8192u};
enum {PL_FULL=1u,PL_TIME=2u,PL_CPU=4u,PL_STOP=8u};
/* Each scalar has its own raw word and success bit (data112..114). */
static const uint32_t pl_address[]={
 0x20810418u,0x20810468u,0x813f09bcu,0x20810908u,
 0x208210c8u,0x208210ccu,0x817d8aacu,0x817d8ab0u,0x2082110cu,0x2082120cu,
 0x20821900u,0x20821904u,0x20821908u,0x2082190cu,0x20821910u,
 0x20821980u,0x20821984u,0x20821988u,0x2082198cu,0x20821990u,
 0x2082110cu,0x208210d0u,0x208210d4u,0x208210d8u,
 0x817d8ab4u,0x817d8ab8u,0x817d8abcu};
static const uint32_t pl_owner_offset[2][18]={
 {0xa0,0x88,0x84,0x80,0x7c,0x64,0x60,0x5c,0x58,0x54,0,0x44,0x40,0x28,0x24,0x20,0x1c,0x18},
 {0x9c,0x98,0x94,0x90,0x8c,0x78,0x74,0x70,0x6c,0x68,0x50,0x4c,0x48,0x3c,0x38,0x34,0x30,0x2c}};
struct pl_rom_guard {uint32_t address,words[8];};
static const struct pl_rom_guard pl_rom_guards[]={
{0x80036c78u,{0xe92d40f0u,0xe3a05000u,0xe3425081u,0xe5953408u,0xe24dd00cu,0xe3a06000u,0xe58d3004u,0xe58d6000u}},
{0x8003679cu,{0xe3a03000u,0xe350001fu,0xe3423081u,0x95932458u,0x8593245cu,0x82400020u,0x93a01001u,0x83a01001u}},
{0x804bb5f0u,{0xe92d40f0u,0xe3a04a01u,0xe3424082u,0xe59430c8u,0xe24dd00cu,0xe58d3004u,0xe3a03000u,0xe58d3000u}},
{0x804bb6a4u,{0xe92d40f0u,0xe3a04a01u,0xe3424082u,0xe59430ccu,0xe24dd00cu,0xe58d3004u,0xe3a03000u,0xe58d3000u}},
{0x804b99d0u,{0xe2822582u,0xe2822802u,0xe5921000u,0xe3a03002u,0xe3c11c0fu,0xe3811c07u,0xe5821000u,0xeaffffc2u}},
{0x804b6c4cu,{0xe7de5613u,0xe1a010a1u,0xe5d93012u,0xe5d96010u,0xe084c00cu,0xe7ca5411u,0xe5d9e011u,0xe58dc00cu}},
};
static int pl_read_range(uint32_t a,unsigned n){
 if(n==20u&&(a==0x20821900u||a==0x20821980u))return 1;
 if(n!=4u)return 0;
 for(unsigned i=0;i<sizeof pl_address/sizeof*pl_address;i++)if(a==pl_address[i])return 1;
 return a==0x208210b0u||a==0x208210e0u;
}
static void pl_init(struct pl_header*h){
 memset(h,0,sizeof*h);h->w[0]=PL_MAGIC;h->w[1]=2;h->w[2]=sizeof(struct pl_record);
 h->w[3]=PL_CAPACITY;h->w[7]=100;h->w[8]=2;h->w[9]=24;h->w[10]=5000;
 h->w[11]=2;h->w[12]=512;h->w[14]=1000;
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
static int pl_ram(uint32_t p,unsigned n){return !(p&3u)&&p>=0x82000000u&&(uint64_t)p+n<=0x93000000ULL;}
static void pl_mark(struct pl_record*z,unsigned i){z->data[112u+i/32u]|=1u<<(i%32u);}
static int pl_has(const struct pl_record*z,unsigned i){return(z->data[112u+i/32u]>>(i%32u))&1u;}
static int pl_word(const struct vp_reader*r,struct pl_record*z,unsigned i,uint32_t a,int user){
 if((user?vp_pw(r,a,z->data+32+i):vp_rw(r,a,z->data+32+i))){pl_mark(z,i);return 1;}return 0;
}
static int pl_block(const struct vp_reader*r,struct pl_record*z,unsigned i,uint32_t a,unsigned words,int user){
 if((user?vp_block(r,a,z->data+32+i,words*4u):r->physical(r->context,a,z->data+32+i,words*4u))){for(unsigned j=0;j<words;j++)pl_mark(z,i+j);return 1;}return 0;
}
static int pl_source(const struct vp_reader*r,struct pl_record*z){
 uint32_t*v=z->data+32;
 if(!pl_word(r,z,50,r->modules+0x5b0e4u,1)||!vp_user(v[50],0x564u)||
 !pl_word(r,z,51,v[50],1)||v[51]!=r->modules+0x4c608u||
 !pl_word(r,z,52,v[50]+0x560u,1)||!vp_user(v[52],0x168u)||
 !pl_word(r,z,53,v[52],1)||v[53]!=r->core+0x59c28u)return 0;
 pl_word(r,z,54,v[52]+0x164u,1);pl_word(r,z,55,v[52]+0x158u,1);return 1;
}
static int pl_sample(const struct vp_reader*r,struct pl_header*h,struct pl_record*records,
 uint64_t(*now)(void*),void*c,uint32_t burst,unsigned post,unsigned kind,struct pl_record*out){
 struct pl_record z;memset(&z,0,sizeof z);memset(z.data,255,sizeof z.data);
 z.kind=kind;z.burst=burst;z.begin_ns=now(c);z.data[112]=z.data[113]=z.data[114]=0;
 if(post)z.flags|=PL_POST_PENDING;
 if(pl_read_header(r,z.data))z.flags|=PL_BEFORE;
 pl_word(r,&z,83,0x8146e438u,0);pl_word(r,&z,84,0x813ebf60u,0);
 uint32_t*v=z.data+32;v[68]=r->modules;v[79]=r->core;
 int native=vp_timer_schema(z.data)==11u&&vp_timer_inflight(z.data)&&(z.data[3]&512u);
 if(native){
  for(unsigned i=0;i<27;i++){
   if(i==10){pl_block(r,&z,10,pl_address[10],5,0);i=14;continue;}
   if(i==15){pl_block(r,&z,15,pl_address[15],5,0);i=19;continue;}
   pl_word(r,&z,i,pl_address[i],0);
  }
  uint32_t p=z.data[11];
  if(pl_ram(p,0x74u)){
   pl_word(r,&z,27,p,0);
   if(pl_has(&z,27)&&v[27]==0x80588910u){
    pl_word(r,&z,28,p+0x6cu,0);pl_word(r,&z,29,p+0x70u,0);
    pl_word(r,&z,30,p,0);pl_word(r,&z,31,p+0x6cu,0);
    if(pl_has(&z,28)&&pl_has(&z,30)&&pl_has(&z,31)&&v[30]==v[27]&&v[31]==v[28])z.flags|=PL_IMAGE;
   }
  }
  pl_word(r,&z,60,0x813f09c0u,0);pl_word(r,&z,61,0x813f09d0u,0);
  pl_word(r,&z,62,0x81459970u,0);pl_word(r,&z,63,0x81459974u,0);
  if(pl_word(r,&z,64,0x208210b0u,0)&&v[64]==2u)pl_word(r,&z,65,0x208210e0u,0);
  pl_word(r,&z,66,0x208210b0u,0);
  if(pl_has(&z,65)&&pl_has(&z,66)&&v[64]==2u&&v[66]==2u)z.flags|=PL_DEBUG;
  pl_block(r,&z,69,0x20821900u,5,0);pl_block(r,&z,74,0x20821980u,5,0);
 }
 int source=pl_source(r,&z);
 /* OEM Encode modules+2cbc8..2cbec copies YUV+2c/+34 to HAL+108
  * descriptor+8/+c. +174 advances AFTER result retrieval. +11c is a
  * submit parameter, NOT an active/busy flag. */
 if(source&&v[54]==2u&&pl_word(r,&z,32,r->modules+0x5b144u,1)&&vp_user(v[32],0x178u)&&
    pl_word(r,&z,33,v[32],1)&&v[33]==r->modules+0x4c9c8u&&pl_word(r,&z,34,v[32]+0x108u,1)&&vp_user(v[34],16u)){
  pl_block(r,&z,35,v[34],4,1);pl_word(r,&z,39,v[32]+0x174u,1);
  pl_word(r,&z,40,v[32]+0x54u,1);pl_word(r,&z,41,v[32]+0x11cu,1);
  pl_word(r,&z,42,r->modules+0x5b144u,1);pl_word(r,&z,43,v[32],1);
  pl_word(r,&z,44,v[32]+0x108u,1);pl_block(r,&z,45,v[34],4,1);
  pl_word(r,&z,49,v[32]+0x174u,1);
  int ok=1;for(unsigned i=32;i<=49;i++)if(!pl_has(&z,i))ok=0;
  if(ok&&v[32]==v[42]&&v[33]==v[43]&&v[34]==v[44]&&!memcmp(v+35,v+45,16u)&&v[39]==v[49])z.flags|=PL_HEVC;
 }
 if(source){
  pl_word(r,&z,56,r->modules+0x5b0e4u,1);pl_word(r,&z,57,v[50]+0x560u,1);
  pl_word(r,&z,58,v[52]+0x164u,1);pl_word(r,&z,59,v[52],1);
  pl_word(r,&z,67,v[50],1);
  int ok=1;for(unsigned i=50;i<=59;i++)if(!pl_has(&z,i))ok=0;
  if(ok&&pl_has(&z,67)&&v[51]==v[67]&&v[50]==v[56]&&v[52]==v[57]&&v[54]==v[58]&&v[53]==v[59])z.flags|=PL_SOURCE;
 }
 pl_word(r,&z,85,0x8146e438u,0);pl_word(r,&z,86,0x813ebf60u,0);
 if(pl_read_header(r,z.data+16))z.flags|=PL_AFTER;
 if((z.flags&3u)==3u&&vp_timer_schema(z.data)==11u&&!memcmp(z.data,z.data+16,64u)){
  z.flags|=PL_SAME;
  if(vp_timer_inflight(z.data)&&(z.data[3]&512u))z.flags|=PL_ACTIVE;
  if(((z.data[3]>>28)&15u)==1u&&!(z.data[3]&0x20000u)&&z.data[14]==0x801cdf4cu)z.flags|=PL_BIT6;
 }
 if(pl_has(&z,0)&&(v[0]&4u))z.flags|=PL_PENDING;
 if(pl_has(&z,1)&&(v[1]&4u))z.flags|=PL_ENABLED;
 if(pl_has(&z,2)&&v[2]==0x801cdf6cu)z.flags|=PL_HANDLER;
 if(pl_has(&z,8)&&pl_has(&z,10)&&pl_has(&z,11)&&pl_has(&z,12)&&pl_has(&z,20)&&
    pl_has(&z,69)&&pl_has(&z,70)&&pl_has(&z,71)&&!memcmp(v+10,v+69,12u)&&v[8]==v[20]&&
    ((v[8]>>8)&15u)==7u&&((v[12]>>12)&0x7ffffu)==3840u&&v[10]&&v[11]>v[10]&&v[11]-v[10]==3840u*2160u)z.flags|=PL_OUTPUT;
 z.end_ns=now(c);if(kind==PL_SAMPLE)h->w[13]++;else h->w[15]++;
 if(out)*out=z;return pl_append(h,records,&z);
}
static int pl_qualified(const struct pl_record*z){
 const uint32_t*v=z->data+32;
 return(z->flags&28u)==28u&&pl_has(z,3)&&v[3]==z->data[10]&&
  pl_has(z,83)&&pl_has(z,84)&&pl_has(z,85)&&pl_has(z,86)&&v[83]==v[85]&&v[84]==v[86];
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
static void pl_burst(const struct vp_reader*r,struct pl_header*h,struct pl_record*records,
 pl_clock_fn now,pl_clock_fn cpu,void(*wait)(void*),void*c,uint64_t deadline){
 uint64_t start=now(c),last=(uint64_t)h->w[16]|((uint64_t)h->w[17]<<32);
 if(h->w[6]&PL_FULL||start<last||start-last<(uint64_t)h->w[10]*1000000u||deadline<=start+8000000u)return;
 uint32_t hdr[16];if(!pl_read_header(r,hdr)||vp_timer_schema(hdr)!=11u||!vp_timer_inflight(hdr)||
  !(hdr[3]&512u)||((hdr[3]>>28)&15u)!=1u||(hdr[3]&0x20000u)||hdr[14]!=0x801cdf4cu)return;
 if(h->w[4]+28u>PL_CAPACITY){h->w[6]|=PL_FULL;return;}
 h->w[16]=(uint32_t)start;h->w[17]=(uint32_t)(start>>32);uint32_t burst=++h->w[5];
 uint64_t begun_cpu=cpu(c),limit=deadline-5000000u,own_limit=start+60000000u;
 if(own_limit<limit)limit=own_limit;
 for(unsigned bank=0;bank<2;bank++)if(!pl_route(r,h,records,now,c,burst,bank,0))return;
 unsigned pending=0;struct pl_record z;
 for(unsigned i=0;i<24;i++){
  if(now(c)>=limit){h->w[6]|=PL_TIME;break;}
  if(cpu(c)-begun_cpu>=4000000u){h->w[6]|=PL_CPU;break;}
  if(i)wait(c);
  if(now(c)>=limit){h->w[6]|=PL_TIME;break;}
  if(!pl_sample(r,h,records,now,c,burst,pending,PL_SAMPLE,&z))break;
  if(!pl_qualified(&z)||memcmp(z.data,hdr,64u))break;
  if(pending){pending++;if(pending>=3u||!(z.flags&PL_PENDING)||!(z.flags&PL_ENABLED)||!(z.flags&PL_HANDLER))break;}
  else if(z.flags&PL_PENDING){if((z.flags&(PL_ENABLED|PL_HANDLER))!=(PL_ENABLED|PL_HANDLER))break;pending=1;}
 }
 for(unsigned bank=0;bank<2;bank++)if(!pl_route(r,h,records,now,c,burst,bank,1))break;
 uint64_t end_cpu=cpu(c),cost=end_cpu>=begun_cpu?end_cpu-begun_cpu:0,total=(uint64_t)h->w[18]|((uint64_t)h->w[19]<<32);
 total+=cost;h->w[18]=(uint32_t)total;h->w[19]=(uint32_t)(total>>32);
 if(cost>h->w[20])h->w[20]=cost>UINT32_MAX?UINT32_MAX:(uint32_t)cost;
}
static void pl_budget(struct pl_header*h,struct pl_record*r,uint64_t now,const struct vo_budget*b){
 struct pl_record z;memset(&z,0,sizeof z);z.kind=PL_BUDGET;z.begin_ns=z.end_ns=now;
 z.data[0]=b->checks;z.data[1]=b->heavy_ms;z.data[2]=b->fast_over;z.data[3]=b->heavy_over;z.data[4]=b->dense_over;
 z.data[5]=(uint32_t)b->fast;z.data[6]=(uint32_t)(b->fast>>32);z.data[7]=(uint32_t)b->heavy;z.data[8]=(uint32_t)(b->heavy>>32);
 z.data[11]=b->dense_ms;z.data[12]=b->target_ms;
 z.data[9]=(uint32_t)b->dense;z.data[10]=(uint32_t)(b->dense>>32);pl_append(h,r,&z);
}
_Static_assert(sizeof(struct pl_header)==128,"pipeline header");
_Static_assert(sizeof(struct pl_record)==512,"output and submit metadata, no pixels");
_Static_assert(PL_DATA+PL_CAPACITY*sizeof(struct pl_record)<=HT_END,"reuse disabled HEVC RAM only");
#endif
