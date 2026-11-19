/* SPDX-License-Identifier: AGPL-3.0-only. R10t read-only registered-owner evidence.
 * Native event10 is unchanged. A missing selected strategy is diagnostic data,
 * not a reason to discard the independently registered notification owner.
 * No function pointer is executed, and no target RAM/MMIO is written. */
#ifndef NXL_VIDEO_EP_ROUTE_CAPTURE_H
#define NXL_VIDEO_EP_ROUTE_CAPTURE_H
#define VR_OFFSET 15552u
#define VR_CAPACITY 4u
#define VR_PIN_CAPACITY 3u
#define VR_XOR 0x6a31d5c8u
#define VR_LDC_BASE 0x8142e018u
#define VR_LDC_CALLBACK 0x801ca2f0u
enum {VR_OWNER,VR_CALLBACK,VR_SELECTED,VR_VIEW,VR_STILL,VR_PARENT,VR_STATE,
 VR_BACKEND_PTR,VR_BACKEND_TYPE,VR_IPC_PARENT,VR_PRIMARY,VR_NOTIFY,VR_FIELDS};
#define VR_VECTOR_MASK 0xfffu
#define VR_REQUIRED_READS (VR_VECTOR_MASK&~(1u<<VR_SELECTED))
#define VR_HEADER_READ (1u<<12)
enum {VR_CONTEXT_OK=1,VR_TIMER_OK=2,VR_READ_OK=4,VR_STABLE=8,VR_CALLBACK_OK=16,
 VR_PARENT_OK=32,VR_BACKEND_OK=64,VR_OWNER_OK=128,VR_TYPES_OK=256,VR_MEMBER_OK=512,
 VR_SOURCE_OK=1024,VR_SELECTED_READ=2048,VR_SELECTED_RAM=4096,VR_SELECTED_MATCH=8192,VR_STATE_OK=16384};
#define VR_BIND_REQUIRED (2047u|VR_STATE_OK)
enum {VR_CONTEXT_FAILED=1,VR_HEADER_BEFORE_FAILED=2,VR_HEADER_AFTER_FAILED=3,
 VR_HEADER_CHANGED=4,VR_VECTOR_CHANGED=5,
 VR_READ_OWNER=10,VR_READ_CALLBACK=11,VR_READ_SELECTED=12,VR_READ_VIEW=13,
 VR_READ_STILL=14,VR_READ_PARENT=15,VR_READ_STATE=16,VR_READ_BACKEND=17,VR_READ_IPC_PARENT=18,
 VR_OWNER_RANGE=20,VR_OWNER_TYPE_READ=21,VR_BACKEND_RANGE=22,VR_BACKEND_TYPE_READ=23,
 VR_SELECTED_RANGE=24,VR_SELECTED_DIFFERENT=25,VR_OWNER_TYPE_BAD=30,VR_CALLBACK_BAD=31,
 VR_MEMBER_BAD=32,VR_PARENT_BAD=33,VR_BACKEND_BAD=34,VR_STATE_BAD=35,
 VR_DRIVER_READ=40,VR_PRESET_READ=41,VR_MODE_CHANGED=42,VR_SOURCE_TYPE=43,
 VR_SOURCE_ROOT=44,VR_SOURCE_STATE=45};
struct vr_record {uint32_t w[48];};
/* Slots0..2 retain verified pins. Slot3 retains the most recent rejected
 * attempt. failed_attempts counts all rejections, including overwritten ones. */
struct vr_header {char magic[8];uint32_t schema,record_bytes,capacity,count,overflow,failed_attempts;struct vr_record records[VR_CAPACITY];};
static void vr_init(struct vr_header*h){memset(h,0,sizeof*h);memcpy(h->magic,"NXLERTE1",8);h->schema=4;h->record_bytes=192;h->capacity=VR_CAPACITY;}
static int vr_needed(struct vr_header*h,unsigned generation){
 for(unsigned i=0;i<h->count;i++)if(h->records[i].w[2]==generation)return 0;
 if(h->count>=VR_PIN_CAPACITY){h->overflow=1;return 0;}return 1;
}
/* Existing read admission is retained; this revision adds no MMIO reads. */
static int vr_read_range(uint32_t a,unsigned n){return (a==0x20821070u&&n==8u)||(a==0x801cab38u&&n==4u)||(a==0x804bb640u&&n==4u);}
static void vr_issue(uint32_t*issue,unsigned pass,unsigned stage){if(!*issue)*issue=(pass<<8)|stage;}
static int vr_owner_ram(uint32_t owner){return owner>=12u&&vp_path_ram(owner-12u,16u);}
static int vr_op_type(uint32_t type,uint32_t notify){return
 (type==0x80582c38u&&notify==0x80582c98u)||
 (type==0x80582d10u&&notify==0x80582d70u)||
 (type==0x80582da8u&&notify==0x80582e08u);}
static void vr_word(const struct vp_reader*r,uint32_t address,uint32_t*v,unsigned field,
 uint32_t*mask,uint32_t*issue,unsigned pass,unsigned stage){
 uint32_t value=VP_UNKNOWN;
 if(vp_rw(r,address,&value)){v[field]=value;*mask|=1u<<field;}
 else vr_issue(issue,pass,stage);
}
static uint32_t vr_vector(const struct vp_reader*r,uint32_t*v,uint32_t*issue,unsigned pass){
 uint32_t mask=0;for(unsigned i=0;i<VR_FIELDS;i++)v[i]=VP_UNKNOWN;
 vr_word(r,0x8153cb28u,v,VR_OWNER,&mask,issue,pass,VR_READ_OWNER);
 vr_word(r,0x817d8ae8u,v,VR_CALLBACK,&mask,issue,pass,VR_READ_CALLBACK);
 vr_word(r,VR_LDC_BASE+0xfa0u,v,VR_SELECTED,&mask,issue,pass,VR_READ_SELECTED);
 if((mask&(1u<<VR_SELECTED))&&!vp_path_ram(v[VR_SELECTED],16u))vr_issue(issue,pass,VR_SELECTED_RANGE);
 vr_word(r,VR_LDC_BASE+0xfa4u,v,VR_VIEW,&mask,issue,pass,VR_READ_VIEW);
 vr_word(r,VR_LDC_BASE+0xfa8u,v,VR_STILL,&mask,issue,pass,VR_READ_STILL);
 vr_word(r,VR_LDC_BASE,v,VR_PARENT,&mask,issue,pass,VR_READ_PARENT);
 vr_word(r,0x8142ed3cu,v,VR_STATE,&mask,issue,pass,VR_READ_STATE);
 vr_word(r,0x8153d964u,v,VR_BACKEND_PTR,&mask,issue,pass,VR_READ_BACKEND);
 vr_word(r,0x8153cbecu,v,VR_IPC_PARENT,&mask,issue,pass,VR_READ_IPC_PARENT);
 if(mask&(1u<<VR_BACKEND_PTR)){
  if(vp_path_ram(v[VR_BACKEND_PTR],4u))vr_word(r,v[VR_BACKEND_PTR],v,VR_BACKEND_TYPE,&mask,issue,pass,VR_BACKEND_TYPE_READ);
  else vr_issue(issue,pass,VR_BACKEND_RANGE);
 }
 if(mask&(1u<<VR_OWNER)){
  if(vr_owner_ram(v[VR_OWNER])){
   uint32_t object[4];
   if(vp_path_read(r,v[VR_OWNER]-12u,object,sizeof object)){
    v[VR_PRIMARY]=object[0];v[VR_NOTIFY]=object[3];mask|=(1u<<VR_PRIMARY)|(1u<<VR_NOTIFY);
   }else vr_issue(issue,pass,VR_OWNER_TYPE_READ);
  }else vr_issue(issue,pass,VR_OWNER_RANGE);
 }
 if((mask&((1u<<VR_SELECTED)|(1u<<VR_OWNER)))==((1u<<VR_SELECTED)|(1u<<VR_OWNER))&&
    vr_owner_ram(v[VR_OWNER])&&vp_path_ram(v[VR_SELECTED],16u)&&v[VR_SELECTED]!=v[VR_OWNER]-12u)
  vr_issue(issue,pass,VR_SELECTED_DIFFERENT);
 return mask;
}
static int vr_member(const uint32_t*a,const uint32_t*b,uint32_t ma,uint32_t mb){
 unsigned field=a[VR_PRIMARY]==0x80582d10u?VR_VIEW:a[VR_PRIMARY]==0x80582da8u?VR_STILL:VR_SELECTED;
 return vr_owner_ram(a[VR_OWNER])&&(ma&(1u<<field))&&(mb&(1u<<field))&&
  a[field]==a[VR_OWNER]-12u&&b[field]==a[field];
}
static unsigned vr_flags(const uint32_t*a,const uint32_t*b,uint32_t ma,uint32_t mb,uint32_t changed,
 int context,int timer,int source,uint32_t expected_backend){
 unsigned f=context?VR_CONTEXT_OK:0;if(timer)f|=VR_TIMER_OK;if(source)f|=VR_SOURCE_OK;
 if((ma&VR_REQUIRED_READS)==VR_REQUIRED_READS&&(mb&VR_REQUIRED_READS)==VR_REQUIRED_READS)f|=VR_READ_OK;
 if((f&VR_READ_OK)&&!(changed&VR_REQUIRED_READS))f|=VR_STABLE;
 if(a[VR_CALLBACK]==VR_LDC_CALLBACK&&b[VR_CALLBACK]==VR_LDC_CALLBACK)f|=VR_CALLBACK_OK;
 if(a[VR_PARENT]==0x80582ba8u&&b[VR_PARENT]==0x80582ba8u)f|=VR_PARENT_OK;
 if(a[VR_IPC_PARENT]==0x80592110u&&b[VR_IPC_PARENT]==0x80592110u&&
    a[VR_BACKEND_PTR]==b[VR_BACKEND_PTR]&&vp_path_ram(a[VR_BACKEND_PTR],4u)&&
    a[VR_BACKEND_TYPE]==b[VR_BACKEND_TYPE]&&a[VR_BACKEND_TYPE]==expected_backend&&
    (a[VR_BACKEND_TYPE]==0x80592268u||a[VR_BACKEND_TYPE]==0x80592328u))f|=VR_BACKEND_OK;
 if(a[VR_OWNER]==b[VR_OWNER]&&vr_owner_ram(a[VR_OWNER]))f|=VR_OWNER_OK;
 if(vr_op_type(a[VR_PRIMARY],a[VR_NOTIFY])&&a[VR_PRIMARY]==b[VR_PRIMARY]&&a[VR_NOTIFY]==b[VR_NOTIFY])f|=VR_TYPES_OK;
 if((f&VR_OWNER_OK)&&vr_member(a,b,ma,mb))f|=VR_MEMBER_OK;
 if(a[VR_STATE]==3u&&b[VR_STATE]==3u)f|=VR_STATE_OK;
 if((ma&mb)&(1u<<VR_SELECTED)){
  f|=VR_SELECTED_READ;
  if(vp_path_ram(a[VR_SELECTED],16u)&&vp_path_ram(b[VR_SELECTED],16u))f|=VR_SELECTED_RAM;
  if((f&VR_OWNER_OK)&&a[VR_SELECTED]==a[VR_OWNER]-12u&&b[VR_SELECTED]==a[VR_SELECTED])f|=VR_SELECTED_MATCH;
 }
 return f;
}
static int vr_source(const struct vp_reader*r,const struct vp_record*s,uint32_t*issue){
 uint32_t driver=VP_UNKNOWN,preset=VP_UNKNOWN,source=VP_UNKNOWN,state=VP_UNKNOWN;
 int dr=vp_rw(r,0x8146e438u,&driver),pr=vp_rw(r,0x813ebf60u,&preset);
 if(!dr)vr_issue(issue,3,VR_DRIVER_READ);if(!pr)vr_issue(issue,3,VR_PRESET_READ);
 int mode=dr&&pr&&driver==s->rtos[0]&&preset==s->rtos[2];if(dr&&pr&&!mode)vr_issue(issue,3,VR_MODE_CHANGED);
 int type=vp_type(r,s->source[0],r->core+0x59c28u);if(!type)vr_issue(issue,3,VR_SOURCE_TYPE);
 int root=vp_pw(r,s->camera[0]+0x560u,&source)&&source==s->source[0];if(!root)vr_issue(issue,3,VR_SOURCE_ROOT);
 int st=vp_pw(r,s->source[0]+0x164u,&state)&&state==s->source[5];if(!st)vr_issue(issue,3,VR_SOURCE_STATE);
 return mode&&type&&root&&st;
}
static void vr_capture(const struct vp_reader*r,struct vr_header*h,const struct vp_record*s,
 unsigned elapsed,unsigned generation,uint64_t(*now)(void*),void*c){
 if(!vr_needed(h,generation))return;
 struct vr_record z;memset(&z,0,sizeof z);uint32_t*w=z.w,issue=0,a[VR_FIELDS],b[VR_FIELDS],before[8],after[8];
 memset(before,255,sizeof before);memset(after,255,sizeof after);uint64_t start=now(c);
 unsigned needed=VP_POLICY|VP_CAMERA|VP_BACKEND|VP_SOURCE;
 int context=(s->valid&(needed|VP_CHANGED))==needed&&vp_video_flow_mode(s);
 if(!context)vr_issue(&issue,0,VR_CONTEXT_FAILED);
 int hb=r->physical(r->context,VP_LATENCY_DATA,before,32);if(!hb)vr_issue(&issue,0,VR_HEADER_BEFORE_FAILED);
 uint32_t ma=vr_vector(r,a,&issue,1),mb=vr_vector(r,b,&issue,2),changed=0;
 int ha=r->physical(r->context,VP_LATENCY_DATA,after,32);if(!ha)vr_issue(&issue,0,VR_HEADER_AFTER_FAILED);
 if(hb)ma|=VR_HEADER_READ;if(ha)mb|=VR_HEADER_READ;
 for(unsigned i=0;i<VR_FIELDS;i++)if(((ma^mb)&(1u<<i))||a[i]!=b[i])changed|=1u<<i;
 int timer=hb&&ha&&vp_timer_common_header(before)&&(before[1]==9u||before[1]==10u||before[1]==11u)&&!memcmp(before,after,32);
 if(!timer){changed|=VR_HEADER_READ;vr_issue(&issue,0,VR_HEADER_CHANGED);}
 if(changed&VR_REQUIRED_READS)vr_issue(&issue,0,VR_VECTOR_CHANGED);
 uint32_t source_issue=0;int source=context?vr_source(r,s,&source_issue):0;if(!issue)issue=source_issue;
 unsigned f=vr_flags(a,b,ma,mb,changed,context,timer,source,s->hevc[9]);
 if(!(f&VR_TYPES_OK))vr_issue(&issue,0,VR_OWNER_TYPE_BAD);
 if(!(f&VR_CALLBACK_OK))vr_issue(&issue,0,VR_CALLBACK_BAD);
 if(!(f&VR_MEMBER_OK))vr_issue(&issue,0,VR_MEMBER_BAD);
 if(!(f&VR_PARENT_OK))vr_issue(&issue,0,VR_PARENT_BAD);
 if(!(f&VR_BACKEND_OK))vr_issue(&issue,0,VR_BACKEND_BAD);
 if(!(f&VR_STATE_OK))vr_issue(&issue,0,VR_STATE_BAD);
 int verified=(f&VR_BIND_REQUIRED)==VR_BIND_REQUIRED;
 unsigned blocking=0,blocking_pass=0;
 if(!context)blocking=VR_CONTEXT_FAILED;
 else if(!timer)blocking=!hb?VR_HEADER_BEFORE_FAILED:!ha?VR_HEADER_AFTER_FAILED:VR_HEADER_CHANGED;
 else if((ma&mb&1u)&&(!vr_owner_ram(a[VR_OWNER])||!vr_owner_ram(b[VR_OWNER]))){blocking=VR_OWNER_RANGE;blocking_pass=!vr_owner_ram(a[VR_OWNER])?1u:2u;}
 else if((ma&mb&(1u<<VR_BACKEND_PTR))&&(!vp_path_ram(a[VR_BACKEND_PTR],4u)||!vp_path_ram(b[VR_BACKEND_PTR],4u))){blocking=VR_BACKEND_RANGE;blocking_pass=!vp_path_ram(a[VR_BACKEND_PTR],4u)?1u:2u;}
 else if(!(f&VR_READ_OK)){
  static const unsigned stage[VR_FIELDS]={VR_READ_OWNER,VR_READ_CALLBACK,VR_READ_SELECTED,VR_READ_VIEW,VR_READ_STILL,
   VR_READ_PARENT,VR_READ_STATE,VR_READ_BACKEND,VR_BACKEND_TYPE_READ,VR_READ_IPC_PARENT,VR_OWNER_TYPE_READ,VR_OWNER_TYPE_READ};
  for(unsigned i=0;i<VR_FIELDS;i++)if((VR_REQUIRED_READS&(1u<<i))&&(!(ma&(1u<<i))||!(mb&(1u<<i)))){
   blocking=stage[i];blocking_pass=!(ma&(1u<<i))?1u:2u;break;
  }
 }else if(!(f&VR_STABLE))blocking=VR_VECTOR_CHANGED;
 else if(!(f&VR_CALLBACK_OK))blocking=VR_CALLBACK_BAD;
 else if(!(f&VR_PARENT_OK))blocking=VR_PARENT_BAD;
 else if(!(f&VR_STATE_OK))blocking=VR_STATE_BAD;
 else if(!(f&VR_BACKEND_OK))blocking=VR_BACKEND_BAD;
 else if(!(f&VR_OWNER_OK))blocking=VR_OWNER_RANGE;
 else if(!(f&VR_TYPES_OK))blocking=VR_OWNER_TYPE_BAD;
 else if(!(f&VR_MEMBER_OK))blocking=VR_MEMBER_BAD;
 else if(!source){blocking=source_issue&255u;blocking_pass=3u;}
 unsigned slot=verified?h->count:VR_PIN_CAPACITY;
 w[0]=slot;w[1]=elapsed;w[2]=generation;w[3]=s->rtos[0];w[4]=s->rtos[2];w[5]=s->source[5];
 w[6]=f;w[7]=issue|(blocking<<16)|(blocking_pass<<24);w[8]=ma;w[9]=mb;w[10]=changed;w[11]=h->count+h->failed_attempts+1u;
 memcpy(w+12,a,sizeof a);memcpy(w+24,b,sizeof b);
 memcpy(w+36,before,12);memcpy(w+39,after,12);w[42]=s->hevc[9];w[43]=s->source[0];
 w[44]=hb?vp_hash(before,32):0;w[45]=ha?vp_hash(after,32):0;
 uint64_t end=now(c);w[46]=end>=start&&end-start<=UINT32_MAX*1000ULL?(uint32_t)((end-start)/1000u):VP_UNKNOWN;
 w[47]=vp_hash(w,188)^VR_XOR;h->records[slot]=z;__sync_synchronize();
 if(verified)h->count++;else h->failed_attempts++;
}
_Static_assert(sizeof(struct vr_header)==800,"three verified pins plus latest rejected attempt");
_Static_assert(VR_OFFSET>=VF_META_OFFSET+sizeof(struct vf_header),"route follows fast header");
_Static_assert(VR_OFFSET+sizeof(struct vr_header)<=VP_HEADER_BYTES,"existing Linux header only");
#endif
