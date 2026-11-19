/* SPDX-License-Identifier: AGPL-3.0-only. R11b original wait-site proof.
 * The raw proc syscall number is preserved and is NEVER used as an
 * authority for this proof. No tracing, target call or code write. */
#ifndef NXL_VIDEO_HEVC_WAIT_SITE_H
#define NXL_VIDEO_HEVC_WAIT_SITE_H
#define VZ_WAIT_PC 0xbd04u
#define VZ_WAIT_CODE 0xbce8u
#define VZ_WAIT_HASH 1227089625u
static const uint32_t vz_wait_words[8]={0xe3a070f0u,0xe1a0200au,0xe22b1080u,0xe28d3030u,0xe58d0020u,0xe1a00009u,0xef000000u,0xe1a05000u};
enum {VZJ_LIBRARY=1u,VZJ_PAIR=2u,VZJ_PC=4u,VZJ_ARGUMENTS=8u,VZJ_SEM_READ=16u,VZJ_SEM_CHANGED=32u,
 VZJ_SEM_NOT_WAITING=64u,VZJ_CODE_A=128u,VZJ_CODE_B=256u,VZJ_CODE_CHANGED=512u,VZJ_STACK=1024u,
 VZJ_ENCODE_RETURN=2048u,VZJ_WAIT_RETURN=4096u,VZJ_OBJECTS=8192u,VZJ_LIFETIME=16384u};
static int vz_wait_pair(const struct vz_task*a,const struct vz_task*b){
 return a->tid>1u&&a->tid==b->tid&&a->start_ticks&&a->start_ticks==b->start_ticks&&
  a->status==VZ_SYS&&b->status==VZ_SYS&&a->sp==b->sp&&a->pc==b->pc&&!memcmp(a->arg,b->arg,24);
}
static unsigned vz_known_site(const struct vz_task*t,uint32_t base){
 if(!base||t->status!=VZ_SYS)return 0;
 return t->pc==base+VZ_WAIT_PC?1u:t->pc==base+0xb980u?2u:t->pc==base+0xee04u?3u:0u;
}
static int vz_wait_candidate(const struct vz_task*t,uint32_t base,const uint32_t*sem,unsigned read){
 return vz_known_site(t,base)==1u&&read==7u&&vp_user(sem[1],80u)&&t->arg[0]==sem[1]+12u&&(t->arg[1]&127u)==0u;
}
static int vz_wait_code_read(const struct vp_reader*r,const struct vz_task*t,uint32_t base,uint32_t*out){
 return vz_known_site(t,base)==1u&&vp_user(base+VZ_WAIT_CODE,32u)&&r->process(r->context,base+VZ_WAIT_CODE,out,32u);
}
static unsigned vz_wait_rejection(const struct vz_task*a,const struct vz_task*b,uint32_t base,uint32_t modules,
 const uint32_t*sa,const uint32_t*sb,unsigned sm,const uint32_t*ca,const uint32_t*cb,unsigned cm,
 const uint32_t*stack,unsigned bytes,int objects){
 unsigned bad=0;if(!base)bad|=VZJ_LIBRARY;if(!vz_wait_pair(a,b))bad|=VZJ_PAIR;
 if(!base||a->pc!=base+VZ_WAIT_PC||b->pc!=base+VZ_WAIT_PC)bad|=VZJ_PC;
 if(sm!=63u)bad|=VZJ_SEM_READ;
 if(sm==63u&&(sa[0]!=sb[0]||sa[1]!=sb[1]))bad|=VZJ_SEM_CHANGED;
 if(sm==63u&&((int32_t)sa[2]>=0||(int32_t)sb[2]>=0))bad|=VZJ_SEM_NOT_WAITING;
 if(!vp_user(sa[1],80u)||a->arg[0]!=sa[1]+12u||b->arg[0]!=sa[1]+12u||
    (a->arg[1]&127u)!=0u||(b->arg[1]&127u)!=0u)bad|=VZJ_ARGUMENTS;
 if(!(cm&1u))bad|=VZJ_CODE_A;if(!(cm&2u))bad|=VZJ_CODE_B;
 if(((cm&1u)&&memcmp(ca,vz_wait_words,32u))||((cm&2u)&&memcmp(cb,vz_wait_words,32u)))bad|=VZJ_CODE_CHANGED;
 if(bytes!=VZ_STACK)bad|=VZJ_STACK;
 unsigned encoder=0,wait=0;for(unsigned i=0;i<bytes/4u;i++){encoder|=stack[i]==modules+0x2cc74u;wait|=stack[i]==modules+0x260d0u;}
 if(!encoder)bad|=VZJ_ENCODE_RETURN;if(!wait)bad|=VZJ_WAIT_RETURN;if(!objects)bad|=VZJ_OBJECTS;
 return bad;
}
static int vz_ids_same(const uint32_t*a,const uint32_t*b){
 return !memcmp(a,b,20u)&&a[6]==b[6]&&a[7]==b[7]; /* source frame counter at[5] is independent */
}
static void vz_discover_verified(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 const struct vz_io*io,struct vz_state*v,struct bv_state*b,uint64_t(*now)(void*),void*c){
 if(!io||!io->task||!io->list||v->selected||b->phase!=2u)return;
 uint64_t stamp=now(c);if(stamp<v->scan_due)return;
 if(v->task_at>=v->ntids){
  v->ntids=0;v->task_at=0;v->list_status=0;
  if(!io->list(io->context,v->tids,VZ_TASKS,&v->ntids)){v->scan_due=stamp+5000000000ULL;return;}
  v->list_status=v->ntids>VZ_TASKS?2u:1u;if(v->ntids>VZ_TASKS)v->ntids=VZ_TASKS;
  if(!v->ntids){v->scan_due=stamp+5000000000ULL;return;}
  uint32_t inventory=vp_hash(v->tids,v->ntids*4u);
  if(inventory!=v->task_inventory_hash){memset(v->task_rejection,0,sizeof v->task_rejection);v->task_inventory_hash=inventory;}
 }
 struct bv_record z;memset(&z,0,sizeof z);z.kind=BV_TASK;z.begin_ns=now(c);
 struct vz_task a={0},bb={0};uint32_t ia[8],ib[8],hfa[4],hfb[4],da[8],db[8],sa[4],sb[4],ca[8],cb[8],stack[VZ_STACK/4];
 memset(ia,255,sizeof ia);memset(ib,255,sizeof ib);memset(sa,255,sizeof sa);memset(sb,255,sizeof sb);
 memset(ca,255,sizeof ca);memset(cb,255,sizeof cb);unsigned sm=0,cm=0,bytes=0,om=0,fdka=0,fdkb=0;uint32_t fda=UINT32_MAX,fdb=UINT32_MAX;
 memset(hfa,255,sizeof hfa);memset(hfb,255,sizeof hfb);memset(da,255,sizeof da);memset(db,255,sizeof db);
 uint32_t base=io->pthread_base?io->pthread_base(io->context):0;
 if(bv_identity(r,ia))om|=1u;
 if((om&1u)&&ia[4]==2u&&ia[7]==r->modules+0x4c9c8u&&bv_hal(r,ia[6],hfa,da))om|=4u;
 if((om&4u)&&vp_pw(r,ia[6]+0x54u,&fda)&&fda<4096u&&io->fd&&(fdka=io->fd(io->context,fda)))om|=16u;
 sm=vz_sem(r,sa);
 if(io->task(io->context,v->tids[v->task_at],&a)){
  if(vz_wait_candidate(&a,base,sa,sm)){
   if(vz_wait_code_read(r,&a,base,ca)){cm|=1u;
    if(!memcmp(ca,vz_wait_words,32u)&&vp_user(a.sp,VZ_STACK)&&r->process(r->context,a.sp,stack,VZ_STACK))bytes=VZ_STACK;
   }
  }
  if(io->task(io->context,a.tid,&bb)&&vz_wait_candidate(&bb,base,sa,sm)&&vz_wait_code_read(r,&bb,base,cb))cm|=2u;
 }
 sm|=vz_sem(r,sb)<<3;
 if(bv_identity(r,ib))om|=2u;
 if((om&2u)&&ib[4]==2u&&ib[7]==r->modules+0x4c9c8u&&bv_hal(r,ib[6],hfb,db))om|=8u;
 if((om&8u)&&vp_pw(r,ib[6]+0x54u,&fdb)&&fdb<4096u&&io->fd&&(fdkb=io->fd(io->context,fdb)))om|=32u;
 int objects=om==63u&&vz_ids_same(ia,ib)&&!memcmp(hfa,hfb,16u)&&!memcmp(da,db,32u)&&fda==fdb&&fdka==fdkb;
 unsigned bad=vz_wait_rejection(&a,&bb,base,r->modules,sa,sb,sm,ca,cb,cm,stack,bytes,objects),proof=bad?0u:VZ_HEVC_WAIT;
 if(a.tid!=v->tids[v->task_at]){bad|=VZJ_LIFETIME;proof=0;}
 if(proof){
  if(io->select&&io->select(io->context,a.tid,a.start_ticks)){v->selected=a.tid;v->selected_ticks=a.start_ticks;v->selected_proof=proof;z.flags=1;}
  else{bad|=VZJ_LIFETIME;proof=0;}
 }
 z.d[0]=v->task_at;z.d[1]=v->ntids;z.d[2]=v->list_status;z.d[3]=proof;z.d[4]=a.tid;
 z.d[5]=(uint32_t)a.start_ticks;z.d[6]=(uint32_t)(a.start_ticks>>32);z.d[7]=a.status;z.d[8]=(uint32_t)a.nr;
 memcpy(z.d+9,a.arg,24);z.d[15]=a.sp;z.d[16]=a.pc;z.d[17]=bytes;if(bytes)memcpy(z.d+18,stack,bytes);
 z.d[82]=r->modules;z.d[83]=ia[6];z.d[84]=fda;z.d[85]=++v->task_attempts;z.d[86]=fdka;z.d[87]=sm;
 memcpy(z.d+88,sa,16);memcpy(z.d+92,sb,16);z.d[96]=base;
 z.d[97]=a.failure_stage;z.d[98]=a.read_errno;z.d[99]=bb.failure_stage;z.d[100]=bb.read_errno;
 if(cm&1u)memcpy(z.d+101,ca,32);if(cm&2u)memcpy(z.d+109,cb,32);
 z.d[117]=cm;z.d[118]=bad;z.d[119]=1;z.d[120]=(cm&1u)?vp_hash(ca,32):0;z.d[121]=(cm&2u)?vp_hash(cb,32):0;
 z.d[122]=vz_known_site(&a,base);z.d[123]=vz_known_site(&bb,base);
 z.d[124]=bb.tid;z.d[125]=(uint32_t)bb.start_ticks;z.d[126]=(uint32_t)(bb.start_ticks>>32);z.d[127]=bb.status;z.d[128]=(uint32_t)bb.nr;
 memcpy(z.d+129,bb.arg,24);z.d[135]=bb.sp;z.d[136]=bb.pc;z.d[137]=om;memcpy(z.d+138,ia,32);memcpy(z.d+146,ib,32);
 z.d[154]=fda;z.d[155]=fdb;z.d[156]=fdkb;z.d[157]=v->task_inventory_hash;z.d[159]=a.nr==bb.nr;z.d[160]=8;
 memcpy(z.d+161,hfa,16u);memcpy(z.d+165,hfb,16u);memcpy(z.d+169,da,32u);memcpy(z.d+177,db,32u);
 z.d[185]=r->core;z.d[186]=v->tids[v->task_at];
 unsigned interesting=z.d[122]||z.d[123]||a.nr==240||a.nr==54;
 unsigned changed=interesting&&v->task_rejection[v->task_at]!=(uint16_t)bad;
 z.d[158]=(proof?1u:0u)|(changed?2u:0u)|((v->task_at%16u==15u||v->task_at+1u==v->ntids)?4u:0u);
 if(interesting)v->task_rejection[v->task_at]=(uint16_t)bad;
 z.end_ns=now(c);if(z.d[158])bv_append(h,records,&z);v->task_at++;
 if(v->task_at==v->ntids)v->scan_due=stamp+5000000000ULL+(uint64_t)(v->task_attempts%11u)*137000000u;
}
#endif
