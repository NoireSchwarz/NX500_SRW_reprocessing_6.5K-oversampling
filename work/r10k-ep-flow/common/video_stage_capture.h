/* SPDX-License-Identifier: AGPL-3.0-only. R10z bounded stage windows.
 * All camera/device access is read-only. Row hashes show observed writes,
 * not EOF or buffer retirement. A HEVC semaphore wait includes firmware,
 * IRQ and scheduling. It is NOT a pure codec-busy measurement.
 */
#ifndef NXL_VIDEO_STAGE_CAPTURE_H
#define NXL_VIDEO_STAGE_CAPTURE_H
#define VZ_MAPS 8u
#define VZ_TASKS 256u
#define VZ_STACK 256u
enum {BV_STAGE=4,BV_MAP=5,BV_TASK=6,BV_LEDGER=7};
enum {VZ_SYS=1,VZ_RUNNING=2,VZ_USER=3};
enum {VZ_IOCTL_SUBMIT=1u,VZ_IOCTL_RESULT=2u,VZ_HEVC_WAIT=4u};
enum {VZ_NA=1u,VZ_NB=2u,VZ_SAME=4u,VZ_DA=8u,VZ_DB=16u,
 VZ_IA=32u,VZ_IB=64u,VZ_HA=128u,VZ_HB=256u,VZ_TA=512u,VZ_TB=1024u,
 VZ_STACK_OK=2048u,VZ_QA=4096u,VZ_QB=8192u,VZ_YA=16384u,VZ_YB=32768u,
 VZ_POST=65536u};
struct vz_mapping {uint32_t lo,hi,pa,permissions;};
struct vz_maps {uint32_t count,status,full_hash,pthread_base,pthread_layout_verified;struct vz_mapping map[VZ_MAPS];
 uint64_t scan_begin_ns,scan_end_ns;uint32_t lines,bytes,failure_stage,read_errno,pthread_seen,epoch;};
struct vz_task {uint32_t tid,status;uint64_t start_ticks;int32_t nr;uint32_t arg[6],sp,pc,failure_stage,read_errno;};
struct vz_io {
 void*context;
 int(*maps)(void*,struct vz_maps*);
 int(*list)(void*,uint32_t*,unsigned,unsigned*);
 int(*task)(void*,uint32_t,struct vz_task*);
 int(*select)(void*,uint32_t,uint64_t);
 int(*fd)(void*,unsigned); /* readlink only, NEVER open the device */
 int(*pixel)(void*,uint32_t,uint32_t,uint32_t,void*,unsigned);
 uint32_t(*pthread_base)(void*); /* only after exact original library hash */
 int(*maps_step)(void*,int,struct vz_maps*); /* -1 cancel,1 start,0 resume: at most one cold read */
};
struct vz_state {
 uint32_t tids[VZ_TASKS],ntids,task_at,list_status,selected,selected_proof;
 uint64_t selected_ticks,scan_due,total_cpu;
 uint32_t context_key[8],window,slot_base,pool_type,pool_count,window_maps,stage_count,task_attempts,preview_mask;
 uint32_t pending,pending_key[5],pending_seen_key[5],pending_seen;
 uint32_t header_seen,last_header_sequence,last_header_tick;uint64_t header_progress_ns;
 struct vz_maps cold_map;uint32_t cold_state,cold_reset,cold_window,cold_epoch,cold_legacy;
 uint64_t metadata_cpu,cold_diag_due;
 uint32_t task_inventory_hash;uint16_t task_rejection[VZ_TASKS];
};
static void vz_init(struct bv_header*h,struct vz_state*v){
 memset(v,0,sizeof*v);h->w[1]=2;h->w[7]=2;h->w[8]=32;h->w[9]=80;h->w[10]=5000;
}
static int vz_task_equal(const struct vz_task*a,const struct vz_task*b){
 return a->tid==b->tid&&a->start_ticks==b->start_ticks&&a->status==VZ_SYS&&b->status==VZ_SYS&&
  a->nr==b->nr&&a->sp==b->sp&&a->pc==b->pc&&!memcmp(a->arg,b->arg,sizeof a->arg);
}
/* Exact two return PCs in the OEM wait chain. A stack scan is evidence of
 * that chain, not an unwound backtrace. Never classify an arbitrary futex. */
static unsigned vz_task_proof(const struct vz_task*t,const uint32_t*stack,unsigned bytes,
 uint32_t modules,uint32_t hal,uint32_t fd,uint32_t sem_data,uint32_t pthread_base){
 if(t->status!=VZ_SYS)return 0;
 if(t->nr==54&&t->arg[0]==fd&&t->arg[2]==hal+0x5cu){
  if(t->arg[1]==0xc10c7408u)return VZ_IOCTL_SUBMIT;
  if(t->arg[1]==0xc10c740au)return VZ_IOCTL_RESULT;
 }
 /* Original cMmfSem: backing+8 cond, cond+4 futex. Original pthread
  * cond_timedwait SVC returns at+bd04. Ordinary futexes are excluded. */
 if(t->nr!=240||!sem_data||!pthread_base||t->arg[0]!=sem_data+12u||
    (t->arg[1]&127u)!=0u||t->pc!=pthread_base+0xbd04u)return 0;
 unsigned a=0,b=0;for(unsigned i=0;i<bytes/4u;i++){a|=stack[i]==modules+0x2cc74u;b|=stack[i]==modules+0x260d0u;}
 return a&&b?VZ_HEVC_WAIT:0;
}
static unsigned vz_sem(const struct vp_reader*r,uint32_t*out){
 /* encoder callback and wait independently reference modules+5b0f8. */
 memset(out,255,16);uint32_t data[4];unsigned mask=0;
 if(!vp_pw(r,r->modules+0x5b0f8u,out)||!vp_user(out[0],4))return mask;mask|=1;
 if(!vp_pw(r,out[0],out+1)||!vp_user(out[1],80))return mask;mask|=2;
 if(!vp_block(r,out[1],data,16))return mask;mask|=4;out[2]=data[0];out[3]=data[3];return mask;
}
#include "video_hevc_wait_site.h"
static uint32_t vz_pixel_address(uint32_t y,uint32_t uv,unsigned i){
 return i==0?y+16u:i==1?y+1080u*3840u+16u:i==2?y+2159u*3840u+16u:
        i==3?uv+1079u*3840u+16u:i==4?y+2159u*3840u+3824u:i==5?uv+1079u*3840u+3824u:
        i==6?y+2159u*3840u+1920u:uv+1079u*3840u+1920u;
}
static int vz_pixel_allowed(uint32_t y,uint32_t uv,uint32_t a,unsigned n){
 if(n!=16u||(y|uv|a)&3u||y<0x94000000u||uv<=y||uv-y!=3840u*2160u||
    (uint64_t)uv+3840u*1080u>0xc0000000ULL)return 0;
 for(unsigned i=0;i<8;i++)if(a==vz_pixel_address(y,uv,i))return 1;
 return 0;
}
static int vz_translate(const struct vz_maps*m,uint32_t va,uint32_t*out){
 if(m->status!=1u||!m->count||m->count>VZ_MAPS)return 0;
 unsigned matches=0;uint32_t pa=0;
 for(unsigned i=0;i<m->count;i++){
  const struct vz_mapping*p=m->map+i;
  if(p->permissions!=1u||p->hi<=p->lo||p->pa<0x94000000u||(uint64_t)p->pa+p->hi-p->lo>0xc0000000ULL)return 0;
  if(va>=p->lo&&(uint64_t)va+16u<=p->hi){uint32_t a=p->pa+va-p->lo;if(matches&&a!=pa)return 0;pa=a;matches++;}
 }
 if(!matches)return 0;*out=pa;return 1;
}
static void vz_map_capture(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,const struct vz_io*io,
 struct vz_state*v,uint64_t(*now)(void*),void*c,unsigned window,unsigned side){
 struct bv_record z;memset(&z,0,sizeof z);z.kind=BV_MAP;z.window=window;z.flags=side;z.begin_ns=now(c);
 struct vz_maps m;memset(&m,0,sizeof m);if(io&&io->maps)io->maps(io->context,&m);
 z.d[0]=m.count;z.d[1]=m.status;z.d[2]=m.full_hash;
 memcpy(z.d+4,m.map,sizeof m.map);z.d[36]=v->selected;z.d[37]=(uint32_t)v->selected_ticks;z.d[38]=(uint32_t)(v->selected_ticks>>32);
 z.d[39]=v->selected&&io&&io->select?io->select(io->context,v->selected,v->selected_ticks):0;
 z.d[46]=m.pthread_layout_verified;z.d[47]=m.pthread_base;
 static const uint32_t slots[5]={0x817d8adcu,0x8153cb84u,0x817d8afcu,0x8153cb48u,0x817d8aacu};
 for(unsigned i=0;i<5;i++)if(vp_rw(r,slots[i],z.d+40+i))z.d[45]|=1u<<i;
 if(v->selected&&!z.d[39]){v->selected=0;v->selected_ticks=0;if(io&&io->select)io->select(io->context,0,0);}
 z.end_ns=now(c);bv_append(h,records,&z);
}
/* Discover at most ONE task per100ms tick. Selection requires the actual
 * HAL ioctl tuple or both OEM wait return PCs, plus a stable syscall pair. */
static void vz_discover_legacy(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 const struct vz_io*io,struct vz_state*v,struct bv_state*b,uint64_t(*now)(void*),void*c){
 if(!io||!io->task||!io->list||v->selected||b->phase!=2u)return;
 uint64_t stamp=now(c);if(stamp<v->scan_due)return;
 if(v->task_at>=v->ntids){
  v->ntids=0;v->task_at=0;v->list_status=0;
  if(!io->list(io->context,v->tids,VZ_TASKS,&v->ntids)){v->scan_due=stamp+5000000000ULL;return;}
  v->list_status=v->ntids>VZ_TASKS?2u:1u;if(v->ntids>VZ_TASKS)v->ntids=VZ_TASKS;
  if(!v->ntids){v->scan_due=stamp+5000000000ULL;return;}
 }
 struct bv_record z;memset(&z,0,sizeof z);z.kind=BV_TASK;z.window=0;z.begin_ns=now(c);
 struct vz_task a={0},bb={0};uint32_t ids[8]={0},hf[4],desc[8],stack[VZ_STACK/4],sa[4],sb[4];unsigned proof=0,bytes=0,fd_kind=0,sm=0;uint32_t fd=UINT32_MAX;
 if(bv_identity(r,ids)&&ids[4]==2u&&ids[7]==r->modules+0x4c9c8u&&bv_hal(r,ids[6],hf,desc)&&
    vp_pw(r,ids[6]+0x54u,&fd)&&fd<4096u&&io->fd&&(fd_kind=io->fd(io->context,fd))){
  sm=vz_sem(r,sa);
  if(io->task(io->context,v->tids[v->task_at],&a)&&a.status==VZ_SYS){
   if(a.nr==240&&vp_user(a.sp,VZ_STACK)&&r->process(r->context,a.sp,stack,VZ_STACK))bytes=VZ_STACK;
   if(io->task(io->context,a.tid,&bb)&&vz_task_equal(&a,&bb)){
    sm|=vz_sem(r,sb)<<3;
    uint32_t sem=sm==63u&&sa[0]==sb[0]&&sa[1]==sb[1]&&(int32_t)sa[2]<0&&(int32_t)sb[2]<0?sa[1]:0;
    proof=vz_task_proof(&a,stack,bytes,r->modules,ids[6],fd,sem,io->pthread_base?io->pthread_base(io->context):0);
   }
  }
 }
 z.d[0]=v->task_at;z.d[1]=v->ntids;z.d[2]=v->list_status;z.d[3]=proof;
 z.d[4]=a.tid;z.d[5]=(uint32_t)a.start_ticks;z.d[6]=(uint32_t)(a.start_ticks>>32);z.d[7]=a.status;
 z.d[8]=(uint32_t)a.nr;memcpy(z.d+9,a.arg,24);z.d[15]=a.sp;z.d[16]=a.pc;z.d[17]=bytes;
 if(bytes)memcpy(z.d+18,stack,bytes);z.d[82]=r->modules;z.d[83]=ids[6];z.d[84]=fd;
 z.d[86]=fd_kind;z.d[87]=sm;if(sm&1u)memcpy(z.d+88,sa,16);if(sm&8u)memcpy(z.d+92,sb,16);
 z.d[96]=io->pthread_base?io->pthread_base(io->context):0;
 z.d[97]=a.failure_stage;z.d[98]=a.read_errno;z.d[99]=bb.failure_stage;z.d[100]=bb.read_errno;
 if(proof&&a.start_ticks&&io->select&&io->select(io->context,a.tid,a.start_ticks)){
  v->selected=a.tid;v->selected_ticks=a.start_ticks;v->selected_proof=proof;z.flags=1;
 }
 z.d[85]=++v->task_attempts;z.end_ns=now(c);
 /* R11a also keeps plausible syscall candidates and their failed proof.
  * The source of a rejected wait must not disappear between every16th row. */
 if(proof||(a.status==VZ_SYS&&(a.nr==240||a.nr==54))||v->task_at%16u==15u||v->task_at+1u==v->ntids)bv_append(h,records,&z);
 v->task_at++;
 /* Offset successive inventory passes to avoid phase-locking a short
  * completion wait to the100ms reader. Still only one task per tick. */
 if(v->task_at==v->ntids)v->scan_due=stamp+5000000000ULL+(uint64_t)(v->task_attempts%11u)*137000000u;
}
static void vz_discover(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 const struct vz_io*io,struct vz_state*v,struct bv_state*b,uint64_t(*now)(void*),void*c){
 if(h->w[1]>=4u)vz_discover_verified(r,h,records,io,v,b,now,c);
 else vz_discover_legacy(r,h,records,io,v,b,now,c);
}
static int vz_identity(const struct vp_reader*r,struct bv_state*b,uint32_t out[4]){
 uint32_t root=0;memset(out,255,16);
 if(!vp_pw(r,b->source,out)||out[0]!=r->core+0x59c28u||!vp_pw(r,b->source+0x164u,out+1)||
    out[1]!=b->phase||(out[1]!=1u&&out[1]!=2u)||!vp_pw(r,r->modules+0x5b144u,&root))return 0;
 if(!root&&out[1]==1u){out[2]=out[3]=0;return 1;}
 return vp_user(root,0x178u)&&vp_pw(r,root,out+3)&&out[3]==r->modules+0x4c9c8u&&(out[2]=root)!=0;
}
/* R11c shares one fully typed camera/source/HAL snapshot between the
 * stage identity and the wait proof. No cached lifetime or weaker predicate. */
static int vz_joint_identity(const struct vp_reader*r,struct bv_state*b,uint32_t out[4],uint32_t ids[8]){
 memset(out,255,16);
 if(!bv_identity(r,ids)||ids[0]!=b->camera||ids[2]!=b->source||ids[4]!=b->phase||(ids[4]!=1u&&ids[4]!=2u))return 0;
 if(!(ids[4]==1u&&!ids[6])&&(!vp_user(ids[6],0x178u)||ids[7]!=r->modules+0x4c9c8u))return 0;
 out[0]=ids[3];out[1]=ids[4];out[2]=ids[6];out[3]=ids[6]?ids[7]:0;return 1;
}
/* Candidate slot selection uses the counter only as a hint, followed by
 * actual Que layout, slot index/state, payload and generation validation. */
static int vz_slot(const struct vp_reader*r,struct bv_state*b,unsigned index,uint32_t*out,uint32_t*yuv){
 uint32_t head[11],slot[28];memset(out,255,32);memset(yuv,255,64);
 if(!b->pool||!vp_block(r,b->pool,head,44u)||head[0]!=r->core+0x593b0u||head[7]!=b->slot_count||
    head[8]!=b->slot_base||!head[7]||head[7]>64u||index>=head[7]||!vp_user(head[8],head[7]*112u)||
    !vp_block(r,head[8]+index*112u,slot,112u)||slot[0]!=index||slot[26]>3u)return 0;
 unsigned idx[8]={0,1,2,3,4,5,24,26};for(unsigned i=0;i<8;i++)out[i]=slot[idx[i]];
 return 1;
}
static void vz_stage(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 struct bv_state*b,struct vz_state*v,const struct vz_io*io,uint64_t(*now)(void*),void*c,unsigned post){
 struct bv_record z;memset(&z,0,sizeof z);memset(z.d,255,sizeof z.d);z.kind=BV_STAGE;z.window=b->window;z.begin_ns=now(c);z.d[245]=0;z.d[246]=0;
 if(post)z.flags|=VZ_POST;
 if(pl_read_header(r,z.d))z.flags|=VZ_NA;
 if(post){uint32_t key[5]={z.d[2],z.d[5],z.d[9],z.d[10],z.d[11]};
  if(!(z.flags&VZ_NA)||!vp_timer_inflight(z.d)||memcmp(key,v->pending_key,20)){
   v->pending=0;z.end_ns=now(c);bv_append(h,records,&z);return;
  }
 }
 if(!(z.flags&VZ_NA)||vp_timer_schema(z.d)!=11u||!vp_timer_inflight(z.d)||!(z.d[3]&512u)){
  z.end_ns=now(c);bv_append(h,records,&z);return;
 }
 if(h->w[1]>=4u){z.d[91]=z.d[229]=z.d[237]=0;}
 uint32_t wait_ids_a[8],wait_ids_b[8],wait_fd_a=UINT32_MAX;unsigned wait_path_a=0,wait_ids_mask=0;
 if(r->physical(r->context,0x20821900u,z.d+32,20u))z.flags|=VZ_DA;
  if(h->w[1]>=5u?vz_joint_identity(r,b,z.d+58,wait_ids_a):vz_identity(r,b,z.d+58)){
   z.flags|=VZ_IA;if(h->w[1]>=5u)wait_ids_mask|=1u;if(bv_hal(r,z.d[60],z.d+66,z.d+42))z.flags|=VZ_HA;}
 static const uint32_t regs[9]={0x208210c8u,0x817d8aacu,0x20810418u,0x20810468u,0x813f09bcu,0x8146e438u,0x813ebf60u,0x2082110cu,0x208210b0u};
 for(unsigned i=0;i<9;i++)if(vp_rw(r,regs[i],z.d+74+i))z.d[245]|=1u<<i;
 z.d[220]=b->slot_count;z.d[221]=b->slot_base;z.d[222]=b->pool;z.d[223]=b->pool_type;
 z.d[224]=b->encoder;z.d[225]=b->source;z.d[226]=z.d[60];z.d[227]=r->modules;z.d[228]=r->core;
 z.d[96]=v->selected;z.d[97]=(uint32_t)v->selected_ticks;z.d[98]=(uint32_t)(v->selected_ticks>>32);
 uint32_t sem_a[4],sem_b[4];z.d[240]=vz_sem(r,sem_a);z.d[235]=sem_a[0];z.d[236]=sem_a[1];z.d[241]=sem_a[2];z.d[243]=sem_a[3];
 z.d[246]=io&&io->pthread_base?io->pthread_base(io->context):0;
 if(h->w[1]==4u&&bv_identity(r,wait_ids_a))wait_ids_mask|=1u;
 if(h->w[1]>=4u&&(z.flags&VZ_HA)&&vp_pw(r,z.d[60]+0x54u,&wait_fd_a)&&io&&io->fd)wait_path_a=io->fd(io->context,wait_fd_a);
 struct vz_task ta={0},tb={0};uint32_t stack[VZ_STACK/4],code_a[8],code_b[8];unsigned stack_bytes=0,code_mask=0;
 int got_task_a=v->selected&&io&&io->task?io->task(io->context,v->selected,&ta):0;
 if(got_task_a){
  z.flags|=VZ_TA;z.d[99]=ta.status;z.d[100]=(uint32_t)ta.nr;memcpy(z.d+101,ta.arg,24);z.d[107]=ta.sp;z.d[108]=ta.pc;
  int stack_candidate=ta.status==VZ_SYS&&ta.nr==240;
  if(h->w[1]>=4u){
   stack_candidate=0;
   if(vz_wait_candidate(&ta,z.d[246],sem_a,z.d[240])&&vz_wait_code_read(r,&ta,z.d[246],code_a)){
    code_mask|=1u;z.d[91]=vp_hash(code_a,32u);stack_candidate=!memcmp(code_a,vz_wait_words,32u);
   }
  }
  if(stack_candidate&&vp_user(ta.sp,VZ_STACK)&&r->process(r->context,ta.sp,stack,VZ_STACK)){
   stack_bytes=VZ_STACK;z.flags|=VZ_STACK_OK;memcpy(z.d+120,stack,VZ_STACK);
  }
 }
 z.d[184]=stack_bytes;z.d[185]=0;
 uint32_t hint=UINT32_MAX,ya[16],yb[16];
 if((z.flags&VZ_HA)&&b->pool_type==r->core+0x593b0u&&b->slot_count){
  hint=z.d[67]%b->slot_count;
  if(vz_slot(r,b,hint,z.d+186,ya)){z.flags|=VZ_QA;
   if(z.d[193]>=1u&&z.d[193]<=3u&&z.d[189]==64u&&z.d[187]==z.d[188]&&z.d[188]&&vp_block(r,z.d[188],ya,64u))z.flags|=VZ_YA;
  }
 }
 z.d[234]=hint;
 z.d[83]=0;
 if((z.flags&VZ_DA)&&z.d[32]&&z.d[33]-z.d[32]==3840u*2160u&&((z.d[34]>>12)&0x7ffffu)==3840u&&
    (z.d[245]&(1u<<7))&&((z.d[81]>>8)&15u)==7u&&io&&io->pixel){
  for(unsigned i=0;i<(h->w[1]>=4u?6u:4u);i++){
   uint32_t p=vz_pixel_address(z.d[32],z.d[33],i),bytes[4];unsigned ai=i<4u?92u+i:230u+(i-4u)*2u,hi=i<4u?84u+i:231u+(i-4u)*2u;z.d[ai]=p;
   if(vz_pixel_allowed(z.d[32],z.d[33],p,16u)&&io->pixel(io->context,z.d[32],z.d[33],p,bytes,16u)){
    z.d[hi]=vp_hash(bytes,16u);z.d[83]|=1u<<i;
   }
  }
 }
 if((z.flags&VZ_QA)&&hint!=UINT32_MAX&&vz_slot(r,b,hint,z.d+194,yb)){
  z.flags|=VZ_QB;
  if(z.d[201]>=1u&&z.d[201]<=3u&&z.d[197]==64u&&z.d[195]==z.d[196]&&z.d[196]&&vp_block(r,z.d[196],yb,64u))z.flags|=VZ_YB;
 }
 for(unsigned pass=0;pass<2;pass++)if(z.flags&(pass?VZ_YB:VZ_YA)){
  uint32_t*y=pass?yb:ya,*out=z.d+202+pass*9;memcpy(out,y+1,16);out[4]=y[11];out[5]=y[13];out[6]=vp_hash(y,64u);out[7]=y[0];out[8]=y[15];
 }
 int got_task_b=v->selected&&io&&io->task?io->task(io->context,v->selected,&tb):0;
 if(got_task_b){
  z.flags|=VZ_TB;z.d[109]=tb.status;z.d[110]=(uint32_t)tb.nr;memcpy(z.d+111,tb.arg,24);z.d[117]=tb.sp;z.d[118]=tb.pc;
  if(h->w[1]>=4u){
   if(vz_wait_candidate(&tb,z.d[246],sem_a,z.d[240])&&vz_wait_code_read(r,&tb,z.d[246],code_b)){code_mask|=2u;z.d[229]=vp_hash(code_b,32u);}
  }else if(vz_task_equal(&ta,&tb)&&ta.start_ticks==v->selected_ticks){
   uint32_t fd=UINT32_MAX;if((z.flags&VZ_IA)&&vp_pw(r,z.d[60]+0x54u,&fd)){
    z.d[119]=fd;z.d[237]=io->fd?io->fd(io->context,fd):0;
    if(z.d[237])z.d[185]=vz_task_proof(&ta,stack,stack_bytes,r->modules,z.d[60],fd,sem_a[1],z.d[246]);
   }
  }
 }else if(v->selected){v->selected=0;v->selected_ticks=0;v->scan_due=now(c)+1000000000ULL;if(io&&io->select)io->select(io->context,0,0);}
 if(!(z.flags&VZ_STACK_OK)){z.d[120]=ta.failure_stage;z.d[121]=ta.read_errno;z.d[122]=tb.failure_stage;z.d[123]=tb.read_errno;}
 if(h->w[1]>=5u?vz_joint_identity(r,b,z.d+62,wait_ids_b):vz_identity(r,b,z.d+62)){
  z.flags|=VZ_IB;if(h->w[1]>=5u)wait_ids_mask|=2u;if(bv_hal(r,z.d[64],z.d+70,z.d+50))z.flags|=VZ_HB;}
 if(r->physical(r->context,0x20821900u,z.d+37,20u))z.flags|=VZ_DB;
 for(unsigned i=0;i<2;i++)if(vp_rw(r,regs[5+i],z.d+88+i))z.d[245]|=1u<<(9+i);
 if(vp_rw(r,0x2082110cu,z.d+90))z.d[245]|=1u<<11;
 z.d[240]|=vz_sem(r,sem_b)<<3;z.d[238]=sem_b[0];z.d[239]=sem_b[1];z.d[242]=sem_b[2];z.d[244]=sem_b[3];
 if(h->w[1]>=4u){
  uint32_t fd_a=wait_fd_a,fd_b=UINT32_MAX;unsigned path_a=wait_path_a,path_b=0;
  if((z.flags&VZ_HB)&&vp_pw(r,z.d[64]+0x54u,&fd_b)&&io&&io->fd)path_b=io->fd(io->context,fd_b);
  z.d[119]=fd_a;
  if(h->w[1]==4u&&bv_identity(r,wait_ids_b))wait_ids_mask|=2u;
  int objects=(z.flags&(VZ_IA|VZ_IB|VZ_HA|VZ_HB))==(VZ_IA|VZ_IB|VZ_HA|VZ_HB)&&
   !memcmp(z.d+58,z.d+62,16u)&&z.d[59]==2u&&!memcmp(z.d+66,z.d+70,16u)&&!memcmp(z.d+42,z.d+50,32u)&&
   fd_a<4096u&&fd_a==fd_b&&path_a&&path_a==path_b&&wait_ids_mask==3u&&vz_ids_same(wait_ids_a,wait_ids_b)&&
   wait_ids_a[0]==b->camera&&wait_ids_a[2]==b->source&&wait_ids_a[4]==2u&&wait_ids_a[6]==z.d[60]&&wait_ids_a[7]==r->modules+0x4c9c8u;
  unsigned bad=vz_wait_rejection(&ta,&tb,z.d[246],r->modules,sem_a,sem_b,z.d[240],code_a,code_b,code_mask,stack,stack_bytes,objects);
  if(ta.tid!=z.d[96]||tb.tid!=z.d[96]||ta.start_ticks!=v->selected_ticks||tb.start_ticks!=v->selected_ticks)bad|=VZJ_LIFETIME;
  if(!bad&&(!io||!io->select||!io->select(io->context,v->selected,v->selected_ticks)))bad|=VZJ_LIFETIME;
  z.d[237]=(path_a&3u)|(code_mask<<2)|(bad<<8);z.d[185]=bad?0u:VZ_HEVC_WAIT;
 }
 if(z.d[185]==VZ_HEVC_WAIT&&(z.d[240]!=63u||sem_a[0]!=sem_b[0]||sem_a[1]!=sem_b[1]||(int32_t)sem_a[2]>=0||(int32_t)sem_b[2]>=0))z.d[185]=0;
 if(pl_read_header(r,z.d+16))z.flags|=VZ_NB;
 if((z.flags&3u)==3u&&vp_timer_schema(z.d)==11u&&!memcmp(z.d,z.d+16,64u))z.flags|=VZ_SAME;
 if(h->w[1]<4u){z.d[230]=z.d[67];z.d[231]=z.d[71];z.d[232]=z.d[59];z.d[233]=z.d[63];}
 z.end_ns=now(c);uint64_t span=z.end_ns-z.begin_ns;if(span>h->w[19])h->w[19]=span>UINT32_MAX?UINT32_MAX:(uint32_t)span;
 bv_append(h,records,&z);
 if((z.flags&VZ_SAME)&&vp_timer_inflight(z.d)&&z.d[76]!=UINT32_MAX&&z.d[77]!=UINT32_MAX&&z.d[78]==0x801cdf6cu&&(z.d[76]&4u)&&(z.d[77]&4u)){
  uint32_t key[5]={z.d[2],z.d[5],z.d[9],z.d[10],z.d[11]};
  if(post){if(++v->pending>=3u)v->pending=0;}
  else if(!v->pending_seen||memcmp(key,v->pending_seen_key,20)){v->pending=1;memcpy(v->pending_key,key,20);memcpy(v->pending_seen_key,key,20);v->pending_seen=1;}
 }else v->pending=0;
}
static void vz_ledger(struct bv_header*h,struct bv_record*records,uint64_t stamp,const struct vo_budget*b){
 struct bv_record z;memset(&z,0,sizeof z);z.kind=BV_LEDGER;z.begin_ns=z.end_ns=stamp;
 z.d[0]=b->checks;z.d[1]=b->stage_ms;z.d[2]=b->stage_over;z.d[3]=b->memory_over;
 z.d[4]=(uint32_t)b->stage;z.d[5]=(uint32_t)(b->stage>>32);z.d[6]=(uint32_t)b->memory;z.d[7]=(uint32_t)(b->memory>>32);
 z.d[8]=b->heavy_ms;z.d[9]=b->dense_ms;z.d[10]=(uint32_t)b->metadata;z.d[11]=(uint32_t)(b->metadata>>32);
 z.d[12]=b->metadata_over;bv_append(h,records,&z);
}
static void vz_tick_impl(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,struct bv_state*b,
 struct vz_state*v,const struct vz_io*io,uint64_t(*now)(void*),uint64_t(*cpu)(void*),void(*wait)(void*),void*c,uint64_t deadline){
 uint64_t stamp=now(c),begun_cpu=cpu(c);
 if(memcmp(v->context_key,b->key,32)){memcpy(v->context_key,b->key,32);v->selected=0;v->selected_ticks=0;v->ntids=v->task_at=0;v->scan_due=stamp;
  b->next_window=stamp;v->header_seen=0;v->header_progress_ns=0;
  if(io&&io->select)io->select(io->context,0,0);
 }
 if(deadline<=stamp+10000000u||(b->phase!=1u&&b->phase!=2u)||!b->source||stamp-b->changed_ns<1000000000ULL)return;
 unsigned preview=b->phase==1u?(b->recording_ns?2u:1u):0;
 if(preview&&(v->preview_mask&preview))return;
 if(h->w[4]+40u>=BV_CAPACITY){h->w[6]|=1u;return;}
 vz_discover(r,h,records,io,v,b,now,c);
 /* A stale active header after shutdown is not enough to touch EP MMIO.
  * First observe recent request progress, using RAM only. */
 uint32_t hdr[16];if(!pl_read_header(r,hdr)||vp_timer_schema(hdr)!=11u||!vp_timer_inflight(hdr)||!(hdr[3]&512u)){v->header_seen=0;return;}
 if(!v->header_seen){v->header_seen=1;v->last_header_sequence=hdr[2];v->last_header_tick=hdr[5];return;}
 if(v->last_header_sequence!=hdr[2]||v->last_header_tick!=hdr[5]){v->header_progress_ns=stamp;v->last_header_sequence=hdr[2];v->last_header_tick=hdr[5];}
 if(now(c)<b->next_window)return;
 if(!v->header_progress_ns||stamp-v->header_progress_ns>200000000u)return;
 b->window=++h->w[5];b->window_begin=now(c);b->window_cpu=0;b->count=0;b->watch_valid=0;v->pending=0;
 v->preview_mask|=preview;
 b->next_window=b->window_begin+(uint64_t)h->w[10]*1000000u;
 vz_map_capture(r,h,records,io,v,now,c,b->window,1);
 bv_images(r,h,records,b,now,c);if(b->phase==2u)bv_light(r,h,records,b,now,c,0);
 unsigned reason=0,target=b->phase==2u?26u:16u;
 while(now(c)+8000000u<deadline){
  if(cpu(c)-begun_cpu>=12000000u){h->w[6]|=2u;reason=2;break;}
  if(now(c)-b->window_begin>=80000000u||b->count>=target){reason=b->count>=target?1:3;break;}
  if(now(c)<b->next_vector){wait(c);continue;}
  vz_stage(r,h,records,b,v,io,now,c,v->pending);b->count++;b->next_vector=now(c)+2000000u;
 }
 /* End context is retained even for a CPU/wall/deadline-limited window. */
 if(b->phase==2u&&now(c)+5000000u<deadline)bv_light(r,h,records,b,now,c,0);
 vz_map_capture(r,h,records,io,v,now,c,b->window,2);
 b->window_cpu=cpu(c)-begun_cpu;if(b->window_cpu>=12000000u){h->w[6]|=2u;reason=2;}
 bv_end_window(h,records,b,now(c),reason?reason:3);
}
static void vz_tick(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,struct bv_state*b,
 struct vz_state*v,const struct vz_io*io,uint64_t(*now)(void*),uint64_t(*cpu)(void*),void(*wait)(void*),void*c,uint64_t deadline){
 uint64_t before=cpu(c);vz_tick_impl(r,h,records,b,v,io,now,cpu,wait,c,deadline);
 v->total_cpu+=vo_cost(before,cpu(c));h->w[15]=(uint32_t)v->total_cpu;h->w[16]=(uint32_t)(v->total_cpu>>32);
}
#endif
