/* SPDX-License-Identifier: AGPL-3.0-only. R10y Linux-only bounded observation.
 * No target calls, allocation from CAP, cache operations, IRQ/ACK or selectors.
 * A Linux-visible allocator copy is not proof of RTOS cache publication.
 */
#ifndef NXL_VIDEO_BUFFER_CAPTURE_H
#define NXL_VIDEO_BUFFER_CAPTURE_H
#define BV_MAGIC 0x3156424eu
#define BV_XOR 0x9c28ad51u
#define BV_CAPACITY 1000u
#define BV_OFFSET HT_END
#define BV_DATA (BV_OFFSET+128u)
#define BM_BLOCKS 45056u
#define BM_BYTES (BM_BLOCKS*16u)
#define BM_TABLE 0x81dfc908u
#define BM_CAPACITY 4u
struct bv_header {uint32_t w[32];};
struct bv_record {uint64_t begin_ns,end_ns;uint32_t sequence,window,kind,flags,d[247],commit;};
struct bm_slot {uint32_t h[512];unsigned char table[2][BM_BYTES];};
#define BM_OFFSET (BV_DATA+BV_CAPACITY*sizeof(struct bv_record))
#define BV_END (BM_OFFSET+BM_CAPACITY*sizeof(struct bm_slot))
enum {BV_LIGHT=1,BV_IMAGES=2,BV_END_WINDOW=3};
enum {BV_NATIVE_A=1u,BV_NATIVE_B=2u,BV_SAME=4u,BV_DMA_A=8u,BV_DMA_B=16u,
 BV_ID_A=32u,BV_ID_B=64u,BV_HAL_A=128u,BV_HAL_B=256u,BV_POOL_A=512u,
 BV_POOL_B=1024u,BV_ROSTER_A=2048u,BV_ROSTER_B=4096u,BV_PAYLOAD_A=8192u,
 BV_PAYLOAD_B=16384u,BV_POST=32768u};
enum {BM_READING=1,BM_COMPLETE=2,BM_CHANGED_PHASE=3,BM_READ_FAILED=4,BM_STOPPED=5,BM_TIMEOUT=6};
struct bv_state {
 uint32_t camera,source,encoder,pool,phase,key[8],watch,watch_valid;
 uint32_t roster[2][64*30],pool_type,slot_words,slot_state,slot_count,slot_base,pool_fail[2];
 uint32_t window,count,pending,pending_key[4],pending_seen,pending_seen_key[4],memory_mask,memory_index,memory_stage,memory_at;
 uint64_t changed_ns,recording_ns,next_window,window_begin,window_cpu,next_vector,last_tick;
 uint64_t total_cpu,memory_cpu,memory_identity_due;
};
static uint32_t bv_hash_more(uint32_t h,const void*p,unsigned n){const unsigned char*b=p;while(n--)h=(h^*b++)*16777619u;return h;}
static const struct pl_rom_guard bv_rom_guards[]={
{0x80096970u,{0xe3a02000u,0xe3e01000u,0xe5802028u,0xe5c0202cu,0xe5c02000u,0xe5801024u,0xe5802004u,0xe5802014u}},
{0x80096a8cu,{0xe5d03000u,0xe2433002u,0xe3530005u,0x979ff103u,0xea000005u,0x80096ab8u,0x80096ab8u,0x80096ab8u}},
{0x80096ac0u,{0xe5d03000u,0xe2433002u,0xe3530005u,0x979ff103u,0xea000005u,0x80096aecu,0x80096aecu,0x80096aecu}},
{0x801751dcu,{0xe59f301cu,0xe92d4010u,0xe1a04000u,0xe4803068u,0xebfc85dfu,0xe1a00004u,0xebffffd8u,0xe1a00004u}},
};
static int bv_read_range(uint32_t a,unsigned n){
 if(a==0x2081040cu&&n==4u)return 1; /* FAST4 status omitted by R10x. */
 /* Batch only a run of already admitted bus words; never include a hole. */
 if(n>4u&&n<=24u&&!(n&3u))for(unsigned i=0;i<VP_BUS_REGS;i++)if(vp_bus_address[i]==a){
  unsigned j;for(j=0;j<n/4u;j++)if(i+j>=VP_BUS_REGS||vp_bus_address[i+j]!=a+j*4u)break;
  if(j==n/4u)return 1;
 }
 return n&&n<=4096u&&!(a&3u)&&!(n&3u)&&a>=BM_TABLE&&(uint64_t)a+n<=BM_TABLE+BM_BYTES;
}
static int bv_user_block(const struct vp_reader*r,uint32_t a,void*p,unsigned n){
 if(!vp_user(a,n))return 0;unsigned done=0;
 while(done<n){unsigned k=n-done;if(k>4096u)k=4096u;if(!r->process(r->context,a+done,(unsigned char*)p+done,k))return 0;done+=k;}return 1;
}
static void bv_init(struct bv_header*h,struct bv_state*s){
 memset(h,0,sizeof*h);memset(s,0,sizeof*s);h->w[0]=BV_MAGIC;h->w[1]=1;h->w[2]=sizeof(struct bv_record);
 h->w[3]=BV_CAPACITY;h->w[7]=4;h->w[8]=36;h->w[9]=500;h->w[10]=10000;h->w[11]=2;
 h->w[13]=sizeof(struct bm_slot);h->w[14]=BV_END-HT_END;
}
static int bv_append(struct bv_header*h,struct bv_record*r,struct bv_record*z){
 if(h->w[4]>=BV_CAPACITY){h->w[6]|=1u;return 0;}z->sequence=h->w[4];
 z->commit=vp_hash(z,offsetof(struct bv_record,commit))^BV_XOR;r[z->sequence]=*z;__sync_synchronize();h->w[4]++;return 1;
}
static void bv_bind(struct bv_state*b,const struct vp_record*s,uint64_t now){
 uint32_t k[8]={s->camera[0],s->source[0],s->encoder[0],s->encoder[6],s->source[5],s->rtos[0],s->rtos[2],s->hevc[0]};
 if((s->valid&(VP_CAMERA|VP_SOURCE))!=(VP_CAMERA|VP_SOURCE)||s->valid&VP_CHANGED){memset(k,0,sizeof k);}
 if(memcmp(k,b->key,sizeof k)){memcpy(b->key,k,sizeof k);b->changed_ns=now;b->watch_valid=0;
  if(k[4]==2u)b->recording_ns=now;}
 b->camera=k[0];b->source=k[1];b->encoder=k[2];b->pool=k[3];b->phase=k[4];
}
/* Verify live roots/types every vector, never trust an address across a mode
 * or object lifetime just because a cached pointer still has the same value. */
static int bv_identity(const struct vp_reader*r,uint32_t v[8]){
 memset(v,255,32);uint32_t q[2],state[12];
 if(!vp_pw(r,r->modules+0x5b0e4u,v)||!vp_type(r,v[0],r->modules+0x4c608u)||
 !vp_block(r,v[0]+0x55cu,q,8u)||q[0]!=r->core+0x35738u||!vp_type(r,q[1],r->core+0x59c28u)||
 !vp_block(r,q[1]+0x138u,state,48u))return 0;
 v[1]=r->modules+0x4c608u;v[2]=q[1];v[3]=r->core+0x59c28u;v[4]=state[11];v[5]=state[8];
 if(vp_pw(r,r->modules+0x5b144u,v+6))vp_pw(r,v[6],v+7);
 return 1;
}
static int bv_hal(const struct vp_reader*r,uint32_t p,uint32_t fields[4],uint32_t desc[8]){
 uint32_t v[28];memset(fields,255,16);memset(desc,255,32);
 if(!vp_user(p,0x178u)||!vp_block(r,p+0x108u,v,112u))return 0;
 fields[0]=v[0];fields[1]=v[27];fields[2]=v[5];fields[3]=v[6];
 return fields[0]==r->modules+0x5b148u&&vp_block(r,fields[0],desc,32u);
}
/* OEM GetFullBuffer and SetBufferFree define three distinct layouts.
 * The actual R10x recording input is Que, with a disposable metadata copy. */
static int bv_pool(const struct vp_reader*r,struct bv_state*b,uint32_t head[17],unsigned pass,uint32_t packed[4]){
 memset(head,255,68);memset(packed,255,16);
 b->pool_fail[pass]=1;if(!b->encoder||!b->pool||!vp_type(r,b->encoder,r->core+0x59da8u))return 0;
 uint32_t ports[3]={0};vp_port_queues(r,b->encoder,ports);b->pool_fail[pass]=2;if(ports[0]!=b->pool)return 0;
 b->pool_fail[pass]=3;if(!vp_block(r,b->pool,head,44u))return 0;
 unsigned words,at,n,base;
 if(head[0]==r->core+0x592f8u){if(!vp_pw(r,b->pool+0x38u,head+14))return 0;words=30;at=28;n=head[14];base=head[7];}
 else if(head[0]==r->core+0x59288u){if(!vp_pw(r,b->pool+0x3cu,head+15))return 0;words=28;at=26;n=head[15];base=head[7];}
 else if(head[0]==r->core+0x593b0u){words=28;at=26;n=head[7];base=head[8];}
 else{b->pool_fail[pass]=4;return 0;}
 b->pool_fail[pass]=5;if(!n||n>64u||!vp_user(base,n*words*4u))return 0;
 b->pool_fail[pass]=6;if(pass&&(b->pool_type!=head[0]||b->slot_words!=words||b->slot_count!=n||b->slot_base!=base))return 0;
 b->pool_type=head[0];b->slot_words=words;b->slot_state=at;b->slot_count=n;b->slot_base=base;
 b->pool_fail[pass]=7;if(!bv_user_block(r,base,b->roster[pass],n*words*4u))return 0;
 memset(packed,0,16);
 b->pool_fail[pass]=8;for(unsigned i=0;i<n;i++){unsigned st=b->roster[pass][i*words+at];if(st>3u)return 0;packed[i/16u]|=st<<((i%16u)*2u);}
 b->pool_fail[pass]=0;
 return 1;
}
static int bv_slot_payload(const struct vp_reader*r,struct bv_state*b,unsigned pass,unsigned index,uint32_t slot[30],uint32_t data[16]){
 memset(slot,255,120);memset(data,255,64);if(index>=b->slot_count)return 0;
 memcpy(slot,b->roster[pass]+index*b->slot_words,b->slot_words*4u);
 /* cMmfBuffer stores a 0x60-byte tMmfFrameData at slot+0x10;
  * its first pointer addresses the64-byte tMmfYuvData copied by VideoDataCallback.
  * Que instead stores FrameData at+8. Its metadata allocation is freed
  * and pointer/size cleared on return: NEVER follow a returned Que pointer. */
 if(b->pool_type==r->core+0x592f8u)return slot[5]==64u&&vp_block(r,slot[4],data,64u);
 if(b->pool_type==r->core+0x593b0u&&slot[26]>=1u&&slot[26]<=3u&&slot[3]==64u&&slot[1]&&slot[1]==slot[2])return vp_block(r,slot[2],data,64u);
 return 0;
}
static void bv_light(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,struct bv_state*b,
 uint64_t(*now)(void*),void*c,unsigned post){
 struct bv_record z;memset(&z,0,sizeof z);memset(z.d,255,sizeof z.d);z.kind=BV_LIGHT;z.window=b->window;z.begin_ns=now(c);
 b->pool_fail[0]=b->pool_fail[1]=UINT32_MAX;
 if(post)z.flags|=BV_POST;
 if(pl_read_header(r,z.d))z.flags|=BV_NATIVE_A;
 z.d[224]=r->modules;z.d[225]=r->core;
 if(post){uint32_t key[4]={z.d[2],z.d[5],z.d[9],z.d[10]};
  if(!(z.flags&BV_NATIVE_A)||!vp_timer_inflight(z.d)||memcmp(key,b->pending_key,sizeof key)){
   b->pending=0;z.end_ns=now(c);bv_append(h,records,&z);return;
  }
 }
 if(r->physical(r->context,0x20821900u,z.d+32,20u))z.flags|=BV_DMA_A;
 vp_rw(r,0x2082110cu,z.d+82);vp_rw(r,0x8146e438u,z.d+84);vp_rw(r,0x813ebf60u,z.d+85);
 if(bv_identity(r,z.d+58))z.flags|=BV_ID_A;
 if((z.flags&BV_ID_A)&&z.d[65]==r->modules+0x4c9c8u&&bv_hal(r,z.d[64],z.d+74,z.d+42))z.flags|=BV_HAL_A;
 if((z.flags&BV_ID_A)&&z.d[58]==b->camera&&z.d[60]==b->source&&z.d[62]==b->phase&&bv_pool(r,b,z.d+88,0,z.d+122)){
  z.flags|=BV_POOL_A;
  /* A tracked lifetime is followed through later requests until its slot is
   * free/reused; absence of a processing slot is itself retained. */
  if(!b->watch_valid){for(unsigned wanted=3;wanted>=2u&&!b->watch_valid;wanted--)for(unsigned i=0;i<b->slot_count;i++)if(b->roster[0][i*b->slot_words+b->slot_state]==wanted){b->watch=i;b->watch_valid=1;break;}}
  if(b->watch_valid){z.d[222]=b->watch;if(bv_slot_payload(r,b,0,b->watch,z.d+130,z.d+190))z.flags|=BV_PAYLOAD_A;}
  z.flags|=BV_ROSTER_A;
 }
 vp_rw(r,0x20810418u,z.d+244);vp_rw(r,0x20810468u,z.d+245);vp_rw(r,0x813f09bcu,z.d+246);
 if(bv_identity(r,z.d+66))z.flags|=BV_ID_B;
 if((z.flags&BV_ID_B)&&z.d[73]==r->modules+0x4c9c8u&&bv_hal(r,z.d[72],z.d+78,z.d+50))z.flags|=BV_HAL_B;
 if((z.flags&BV_POOL_A)&&(z.flags&BV_ID_B)&&z.d[66]==b->camera&&z.d[68]==b->source&&z.d[70]==b->phase&&bv_pool(r,b,z.d+105,1,z.d+126)){
  z.flags|=BV_POOL_B|BV_ROSTER_B;
  if(z.d[222]!=UINT32_MAX&&bv_slot_payload(r,b,1,z.d[222],z.d+160,z.d+206))z.flags|=BV_PAYLOAD_B;
  if(b->watch_valid&&b->roster[1][b->watch*b->slot_words+b->slot_state]<=1u)b->watch_valid=0;
 }
 if(r->physical(r->context,0x20821900u,z.d+37,20u))z.flags|=BV_DMA_B;
 vp_rw(r,0x2082110cu,z.d+83);vp_rw(r,0x8146e438u,z.d+86);vp_rw(r,0x813ebf60u,z.d+87);
 if(pl_read_header(r,z.d+16))z.flags|=BV_NATIVE_B;
 if((z.flags&3u)==3u&&vp_timer_schema(z.d)==11u&&!memcmp(z.d,z.d+16,64u))z.flags|=BV_SAME;
 z.d[224]=r->modules;z.d[225]=r->core;z.d[228]=b->encoder;z.d[229]=b->pool;z.d[230]=b->slot_words;z.d[231]=b->slot_state;z.d[232]=b->slot_count;
 z.d[233]=b->pool_fail[0];z.d[234]=b->pool_fail[1];
 z.d[236]=b->slot_base;
 z.end_ns=now(c);uint64_t span=z.end_ns-z.begin_ns;if(span>h->w[19])h->w[19]=span>UINT32_MAX?UINT32_MAX:(uint32_t)span;
 bv_append(h,records,&z);
 if((z.flags&BV_SAME)&&vp_timer_inflight(z.d)&&z.d[244]!=UINT32_MAX&&z.d[245]!=UINT32_MAX&&z.d[246]==0x801cdf6cu&&
    (z.d[244]&4u)&&(z.d[245]&4u)){
  uint32_t key[4]={z.d[2],z.d[5],z.d[9],z.d[10]};
  if(!post){
   if(!b->pending_seen||memcmp(key,b->pending_seen_key,sizeof key)){
    b->pending=1;memcpy(b->pending_key,key,sizeof key);memcpy(b->pending_seen_key,key,sizeof key);b->pending_seen=1;
   }
  }
  else if(memcmp(key,b->pending_key,sizeof key)||++b->pending>=3u)b->pending=0;
 }else b->pending=0;
}
/* CImage contains Buffer at+68: type byte, four addresses at+4 and another
 * four at+14. Capture the whole typed descriptor twice, not guessed +6c/+70. */
static void bv_images(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 struct bv_state*b,uint64_t(*now)(void*),void*c){
 struct bv_record z;memset(&z,0,sizeof z);memset(z.d,255,sizeof z.d);z.kind=BV_IMAGES;z.window=b->window;z.begin_ns=now(c);
 if(pl_read_header(r,z.d))z.flags|=1u;
 for(unsigned j=0;j<2;j++){
  uint32_t p=z.d[9+2*j];z.d[32+j]=p;
  if(pl_ram(p,0x98u))for(unsigned pass=0;pass<2;pass++){
   unsigned at=34+j*76+pass*38;
   if(r->physical(r->context,p,z.d+at,104u)&&z.d[at]==0x80588910u&&r->physical(r->context,p+0x68u,z.d+at+26,48u))z.flags|=1u<<(2+j*2+pass);
  }
 }
 if(pl_read_header(r,z.d+16))z.flags|=2u;z.end_ns=now(c);bv_append(h,records,&z);
}
static void bv_end_window(struct bv_header*h,struct bv_record*r,struct bv_state*b,uint64_t stamp,unsigned reason){
 if(!b->window_begin)return;struct bv_record z;memset(&z,0,sizeof z);z.kind=BV_END_WINDOW;z.window=b->window;z.begin_ns=z.end_ns=stamp;
 z.d[0]=reason;z.d[1]=b->count;z.d[2]=(uint32_t)b->window_cpu;z.d[3]=(uint32_t)(b->window_cpu>>32);z.d[4]=(uint32_t)(stamp-b->window_begin);
 bv_append(h,r,&z);b->window_begin=0;b->pending=0;b->watch_valid=0;
}
static void bv_tick(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,struct bv_state*b,
 uint64_t(*now)(void*),uint64_t(*cpu)(void*),void(*wait)(void*),void*c,uint64_t deadline){
 uint64_t stamp=now(c),begun_cpu=cpu(c);if(deadline<=stamp+8000000u)return;
 if(!b->window_begin&&h->w[4]+40u>=BV_CAPACITY){h->w[6]|=1u;return;}
 if(!b->window_begin&&stamp>=b->next_window&&b->source&&b->phase==2u&&now(c)-b->changed_ns>=1000000000ULL&&h->w[4]+40u<BV_CAPACITY){
  b->window=++h->w[5];b->window_begin=stamp;b->window_cpu=0;b->count=0;b->pending=0;b->next_vector=stamp;
  b->next_window=stamp+(uint64_t)h->w[10]*1000000u;bv_images(r,h,records,b,now,c);
 }
 if(!b->window_begin)return;
 if(b->phase!=2u){bv_end_window(h,records,b,stamp,4);return;}
 unsigned reason=0;
 while(now(c)+5000000u<deadline){
  stamp=now(c);uint64_t cost=cpu(c)-begun_cpu;
  if(cost>=1000000u)break; /* per100ms slice; reserve the next ring drain */
  if(b->window_cpu+cost>=8000000u){h->w[6]|=2u;reason=2;break;}
  if(stamp-b->window_begin>=500000000u||b->count>=36u){reason=b->count>=36u?1:3;break;}
  if(stamp<b->next_vector){wait(c);continue;}
  bv_light(r,h,records,b,now,c,b->pending);b->count++;b->next_vector=now(c)+4000000u;
 }
 uint64_t cost=cpu(c)-begun_cpu;b->window_cpu+=cost;b->total_cpu+=cost;h->w[15]=(uint32_t)b->total_cpu;h->w[16]=(uint32_t)(b->total_cpu>>32);
 if(b->window_cpu>=8000000u){h->w[6]|=2u;reason=2;}
 if(reason)bv_end_window(h,records,b,now(c),reason);
}
static int bm_head(const struct vp_reader*r,uint32_t out[12]){
 uint32_t root[10],head[66],tail[3],upper;
 if(!r->physical(r->context,0x817dc498u,root,40u)||!r->physical(r->context,0x81dfc800u,head,128u)||
 !r->physical(r->context,0x81dfc880u,head+32,128u)||!r->physical(r->context,0x81dfc900u,head+64,8u)||
 !r->physical(r->context,0x81eac908u,tail,12u)||!vp_rw(r,0x81eacda0u,&upper))return 0;
 out[0]=root[0];out[1]=root[1];out[2]=root[9];out[3]=head[0];out[4]=head[65];out[5]=tail[0];out[6]=tail[1];out[7]=tail[2];out[8]=head[0x78/4];out[9]=upper;out[10]=head[0x70/4];out[11]=head[0x74/4];
 return root[0]==0x80574230u&&root[1]==0x805742acu&&root[9]==0x81dfc800u&&head[0]==0x80574b60u&&head[65]==0x80574c48u&&tail[0]==0x94000000u&&tail[1]==BM_BLOCKS&&out[8]>=0x94000000u&&out[8]<upper&&upper<=0xc0000000u&&!((out[8]|upper)&16383u);
}
static void bm_finish(struct bv_header*h,struct bv_state*b,struct bm_slot*m,unsigned status,uint64_t stamp){
 m->h[2]=status;m->h[7]=(uint32_t)stamp;m->h[8]=(uint32_t)(stamp>>32);m->h[14]=b->memory_stage;m->h[15]=b->memory_at;b->memory_stage=0;
 if(status!=BM_COMPLETE)h->w[21]++;m->h[511]=vp_hash(m->h,511*4u);
}
static int bm_side(const struct vp_reader*r,struct bm_slot*m,struct bv_state*b,unsigned pass){
 unsigned at=384u+pass*48u;uint32_t*v=m->h+at;memset(v,255,48u*4u);v[20]=0;
 if(bv_identity(r,v))v[20]|=1u;
 if((v[20]&1u)&&v[7]==r->modules+0x4c9c8u&&bv_hal(r,v[6],v+8,v+12))v[20]|=2u;
 if(pl_read_header(r,v+21))v[20]|=4u;
 v[37]=b->encoder;v[38]=b->pool;
 return(v[20]&1u)&&v[0]==m->h[3]&&v[2]==m->h[4]&&v[4]==m->h[5];
}
/* Sparse cold snapshots, at most16 pages per slice and0.5ms CPU. No device
 * lock, test allocation, RTOS suspension, cache publish or SD hot writes. */
static void bm_tick(const struct vp_reader*r,struct bv_header*h,struct bm_slot*slots,struct bv_state*b,
 uint64_t(*now)(void*),uint64_t(*cpu)(void*),void*c,uint64_t deadline){
 uint64_t stamp=now(c),begun=cpu(c);if(deadline<=stamp+8000000u||b->window_begin)return;
 unsigned phase=0;
 if(b->source&&stamp-b->changed_ns>=1000000000ULL){
  if(b->phase==1u)phase=b->recording_ns?4u:1u;
  if(b->phase==2u)phase=stamp-b->recording_ns>=30000000000ULL?3u:2u;
 }
 if(!b->memory_stage&&phase&&!(b->memory_mask&(1u<<phase))&&h->w[12]<BM_CAPACITY){
  b->memory_mask|=1u<<phase;b->memory_index=h->w[12]++;b->memory_stage=1;b->memory_at=0;
  struct bm_slot*m=slots+b->memory_index;memset(m->h,0,sizeof m->h);m->h[0]=0x314d424eu;m->h[1]=phase;m->h[2]=BM_READING;
  m->h[3]=b->camera;m->h[4]=b->source;m->h[5]=b->phase;m->h[6]=(uint32_t)stamp;m->h[9]=(uint32_t)(stamp>>32);m->h[10]=BM_BLOCKS;
  m->h[11]=m->h[12]=2166136261u; /* rolling table hashes */
  m->h[13]=0; /* RTOS cache publication has NOT been independently proved */
  h->w[22]|=1u<<phase;
 }
 if(!b->memory_stage)return;struct bm_slot*m=slots+b->memory_index;
 uint64_t started=(uint64_t)m->h[6]|((uint64_t)m->h[9]<<32);
 if(stamp-started>25000000000ULL){bm_finish(h,b,m,BM_TIMEOUT,stamp);return;}
 if(b->camera!=m->h[3]||b->source!=m->h[4]||b->phase!=m->h[5]){bm_finish(h,b,m,BM_CHANGED_PHASE,stamp);return;}
 /* Heavy capture checks object lifetime each500ms, complete bookends check
  * it again. Do not spend the0.5ms page budget re-reading roots every100ms. */
 if(stamp>=b->memory_identity_due){uint32_t ident[8];b->memory_identity_due=stamp+1000000000ULL;
  if(!bv_identity(r,ident)||ident[0]!=m->h[3]||ident[2]!=m->h[4]||ident[4]!=m->h[5]){bm_finish(h,b,m,BM_CHANGED_PHASE,now(c));return;}
 }
 unsigned iterations=0;
 while(iterations++<16u&&now(c)+5000000u<deadline&&cpu(c)-begun<500000u){
  unsigned stage=b->memory_stage,at=b->memory_at;
  if(stage==1u||stage==4u||stage==7u){
   unsigned n=stage==1u?0:stage==4u?1:2;
   if(!bm_head(r,m->h+32+n*12u)){bm_finish(h,b,m,BM_READ_FAILED,now(c));break;}
   if(stage==1u&&!bm_side(r,m,b,0)){bm_finish(h,b,m,BM_CHANGED_PHASE,now(c));break;}
   b->memory_stage++;b->memory_at=0;
  }else if(stage==2u||stage==8u){
   unsigned pass=stage==2u?0:1;
   if(at<VP_BUS_REGS){
    unsigned n=1;while(n<6u&&at+n<VP_BUS_REGS&&vp_bus_address[at+n]==vp_bus_address[at]+4u*n)n++;
    if(r->physical(r->context,vp_bus_address[at],m->h+80+pass*136u+at,n*4u)){
     for(unsigned j=0;j<n;j++)m->h[352+pass*5u+(at+j)/32u]|=1u<<((at+j)%32u);
    }else if(n>1u){ /* Retain per-word availability if a batch is rejected. */
     for(unsigned j=0;j<n;j++)if(vp_rw(r,vp_bus_address[at+j],m->h+80+pass*136u+at+j))m->h[352+pass*5u+(at+j)/32u]|=1u<<((at+j)%32u);
    }
    b->memory_at+=n;
   }
   else{b->memory_stage=stage==2u?3u:9u;b->memory_at=0;}
  }else if(stage==3u||stage==5u){
   /* BM_TABLE starts at page offset0x908. The old4KiB stride crossed a
    * page on EVERY call. Read the short prefix, then aligned pages. */
   unsigned pass=stage==3u?0:1,k=BM_BYTES-at,page=4096u-((BM_TABLE+at)&4095u);if(k>page)k=page;
   if(!r->physical(r->context,BM_TABLE+at,m->table[pass]+at,k)){bm_finish(h,b,m,BM_READ_FAILED,now(c));break;}
   m->h[11+pass]=bv_hash_more(m->h[11+pass],m->table[pass]+at,k);b->memory_at+=k;m->h[16+pass]=b->memory_at;
   if(b->memory_at==BM_BYTES){b->memory_stage=stage==3u?4u:7u;b->memory_at=0;}
  }else if(stage==9u){if(!bm_side(r,m,b,1)){bm_finish(h,b,m,BM_CHANGED_PHASE,now(c));break;}bm_finish(h,b,m,BM_COMPLETE,now(c));break;}
  else{bm_finish(h,b,m,BM_READ_FAILED,now(c));break;}
 }
 uint64_t cost=cpu(c)-begun;b->memory_cpu+=cost;h->w[17]=(uint32_t)b->memory_cpu;h->w[18]=(uint32_t)(b->memory_cpu>>32);
 if(b->memory_stage){m->h[14]=b->memory_stage;m->h[15]=b->memory_at;}
}
_Static_assert(sizeof(struct bv_record)==1024,"raw buffer lifetime vector");
_Static_assert(sizeof(struct bm_slot)==2048+2*BM_BYTES,"paired allocator and route snapshots");
#endif
