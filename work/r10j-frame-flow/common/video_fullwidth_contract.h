/* SPDX-License-Identifier: AGPL-3.0-only. NX5001.12 pp19 -> UD with guarded LCAC instructions.
 * No sensor/MMIO writes. Apply only while native movie buffers are absent.
 * The OEM recipe, RAW12 processing, resize, codec and IRQs remain original.
 */
#ifndef NXL_VIDEO_FULLWIDTH_CONTRACT_H
#define NXL_VIDEO_FULLWIDTH_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#ifdef VFW_BARE
void*memset(void*,int,size_t);
void*memcpy(void*,const void*,size_t);
int memcmp(const void*,const void*,size_t);
#else
#include <string.h>
#endif
#include "video_fullwidth_blocks.h"
#define VFW_MAGIC 0x3146574eu
#define VFW_SCHEMA 4u
#define VFW_ROOT 0x81541808u
#define VFW_FRONT_VTABLE 0x80587498u
#define VFW_PROFILE_MANAGER 0x80592d08u
#define VFW_FULL_VTABLE 0x805a0170u
#define VFW_CROP_VTABLE 0x805a0208u
#define VFW_IMAGE_VTABLE 0x80588910u
#define VFW_MAP_WORD 0x8014e7ecu
#define VFW_MAP_CROP 0x8014e8a0u
#define VFW_MAP_FULL 0x8014e858u
#define VFW_DRIVER 0x8146e430u
#define VFW_DRIVER_VTABLE 0x80587f08u
#define VFW_ALLOCATOR 0x81dfc800u
#define VFW_WORK_META (VFW_ALLOCATOR+0xb0570u)
#define VFW_SRC_META (VFW_ALLOCATOR+0xb055cu)
#define VFW_WORK_TABLE 0x8057dd14u
#define VFW_SRC_TABLE 0x8057dcc4u
#define VFW_WORK_FRAME 0x02e44480u
#define VFW_SRC_FRAME 0x00ca8000u
#define VFW_PROFILE_BYTES 192u
#define VFW_CAP_BYTES 0x200000u
#define VFW_CAP_BLOCKS (VFW_CAP_BYTES/16384u)
#define VFW_CONTEXT 0x4000u
#define VFW_REPLY 0x4100u
#define VFW_PACKET 0x4800u
#define VFW_CONTROL 0x4200u
#define VFW_GO_XOR 0x91f47ca3u
#define VFW_SLOT 0x8100f7b0u
#define VFW_ORIGINAL 0x800e454cu
#define VFW_CACHE 0x8008fd40u
#define VFW_MAP_FUNCTION 0x8014e78cu
#define VFW_POWER_ROOT 0x8146e730u
#define VFW_POWER_MANAGER_VTABLE 0x80588128u
#define VFW_POWER_VIDEO_VTABLE 0x80588288u
#define VFW_POWER_OPS_VTABLE 0x80588430u
#define VFW_POWER_ROWS 0x8100e400u
#define VFW_POWER_LOOKUP 0x8016ed04u
#define VFW_IPC_SETUP_WORD 0x805922bcu
#define VFW_IPC_SETUP_CROP 0x801ce78cu
#define VFW_IPC_SETUP_SCALE 0x801cf038u
#define VFW_IPC_INIT_WORD 0x80592294u
#define VFW_IPC_UPDATE_WORD 0x80592298u
#define VFW_IPC_INIT_CROP 0x801cec38u
#define VFW_IPC_INIT_SCALE 0x801cf628u
#define VFW_IPC_UPDATE_CROP 0x801ceda0u
#define VFW_IPC_UPDATE_SCALE 0x801cf4e4u
#define VFW_BACKEND_WORD 0x801cd9bcu
#define VFW_BACKEND_UD 0x801cda40u
#define VFW_BACKEND_SCALE 0x801cda10u
#define VFW_BACKEND_ROOT 0x8153cbecu
#define VFW_BACKEND_VTABLE 0x80592110u
#define VFW_UD_VTABLE 0x80592268u
#define VFW_QHD_VTABLE 0x80592328u
/* Original UD recipe writes one of two native LDC policy flags. Route its
 * existing literal to the native scaled policy only for full-width mode.
 * ProcessStop clears both flags; the LDC wrapper selects its own mode.
 * This is a data literal, not an instruction or a persistent callback. */
#define VFW_LDC_POLICY_WORD 0x8010daa4u
#define VFW_LDC_POLICY_UD 0x8142d0d8u
#define VFW_LDC_POLICY_SCALE 0x8142d0dcu
/* NX1-aligned LCAC-related control bit, only while full width is armed.
 * Exact instruction pair is restored before native UHD/QHD selection. */
#define VFW_LCAC_MASK_WORD 0x801cf0e4u
#define VFW_LCAC_OR_WORD 0x801cf0fcu
#define VFW_LCAC_MASK_STOCK 0xe20770f5u
#define VFW_LCAC_OR_STOCK 0xe3877030u
#define VFW_LCAC_MASK_FULL 0xe20770e5u
#define VFW_LCAC_OR_FULL 0xe3877020u
#define VFW_COMMIT_XOR 0xc74f918bu
enum {VFW_UNKNOWN=0,VFW_STOCK=1,VFW_FULL=2,VFW_LEGACY_FULL=3,VFW_LEGACY_SCALE=4,VFW_LEGACY_UPDATE=5,VFW_LEGACY_LDC=6,VFW_LEGACY_LCAC=7,VFW_LEGACY_WIDTH=8,VFW_LEGACY_BOUNDARY=9};
enum {VFW_APPLY=1,VFW_RESTORE=2,VFW_TRACE=3};
enum {VFW_OK=0,VFW_BAD_CONTEXT=1,VFW_CHANGED=2,VFW_MOVIE_ACTIVE=3,VFW_BAD_PROFILE=4,VFW_BAD_BUDGET=5,VFW_WRITE_FAILED=6,VFW_CACHE_FAILED=7,VFW_VERIFY_FAILED=8,VFW_WAITING=9,VFW_ABORTED=10,VFW_TMC_UNSTABLE=11,VFW_TMC_BUSY=12};
static const uint32_t vfw_offsets[6]={0x14u,0x18u,0x1cu,0x20u,0x3cu,0x40u};
/* The original NX500 pp6 profile remains immutable.  PP19's experimental
 * output is widened by 16 pixels within the existing captured 6608-wide
 * RAW envelope, matching NX1's native active width. */
static const uint32_t vfw_native_full[6]={6608u,3680u,6480u,3644u,92u,36u};
static const uint32_t vfw_full[6]={6608u,3680u,6496u,3644u,92u,36u};
static const uint32_t vfw_crop[6]={4096u,2436u,3856u,2176u,232u,260u};
struct vfw_snapshot {
 uint32_t root,manager,vector,count,profile6,profile19,mapping,driver_modes;
 uint32_t work[5],src[5],work_frame,src_frame;
 uint32_t full[VFW_PROFILE_BYTES/4u],crop[VFW_PROFILE_BYTES/4u];
 uint32_t power_manager,power_video,power_ops,power_entries,power_current,power_scene_word;
 uint32_t power_operations[24],scene_full[10],scene_crop[10];
 uint32_t ipc_setup,native_ipc_preset;
};
struct vfw_context {uint32_t magic,schema,nonce,cap,bytes,operation,root,profile6,profile19,power_manager,power_video,power_ops,power_entries,power_scene_word,reserved[2];};
struct vfw_reply {
 uint32_t magic,schema,nonce,operation,status,before,after,writes;
 uint32_t rollback_verified,slot_restored,cache_return,source_mode,target_mode,root,profile6,profile19;
 uint32_t before_values[6],after_values[6],work_frame,src_frame,raw_dma_max_bytes,work_total,src_total;
 uint32_t phase,power_before,power_after,power_scene_word,power_manager,power_video,power_ops,native_power_scene;
 uint32_t ipc_setup_before,ipc_setup_after,ipc_init_before,ipc_init_after,ipc_update_before,ipc_update_after,backend_before,backend_after,ldc_policy_before,ldc_policy_after,lcac_mask_before,lcac_mask_after,lcac_or_before,lcac_or_after,block_before,block_after,reserved[5],hash,commit;
};
typedef int(*vfw_reader)(void*,uint32_t,void*,unsigned);
struct vfw_ops {void*context;vfw_reader read;int(*write)(void*,uint32_t,uint32_t);int(*cache)(void*,uint32_t,unsigned);int(*quiet)(void*);};
static uint32_t vfw_hash(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int vfw_ram(uint32_t p,unsigned bytes){return !(p&3u)&&p>=0x81006000u&&(uint64_t)p+bytes<=0x94000000ULL;}
static int vfw_word(vfw_reader read,void*c,uint32_t at,uint32_t*out){return read(c,at,out,4);}
static int vfw_values(const uint32_t*p,const uint32_t*expected){for(unsigned i=0;i<6;i++)if(p[vfw_offsets[i]/4]!=expected[i])return 0;return 1;}
static int vfw_power_capture(vfw_reader read,void*c,struct vfw_snapshot*s){
 uint32_t manager[6],video[7],ops[4];
 if(!vfw_word(read,c,VFW_POWER_ROOT,&s->power_manager)||!vfw_ram(s->power_manager,sizeof manager)||!read(c,s->power_manager,manager,sizeof manager)||manager[0]!=VFW_POWER_MANAGER_VTABLE)return 0;
 s->power_video=manager[3];s->power_current=manager[5];
 if(!vfw_ram(s->power_video,sizeof video)||!vfw_ram(s->power_current,28)||!read(c,s->power_video,video,sizeof video)||video[0]!=VFW_POWER_VIDEO_VTABLE||(video[2]&255u)!=1u)return 0;
 s->power_ops=video[6];
 if(!vfw_ram(s->power_ops,sizeof ops)||!read(c,s->power_ops,ops,sizeof ops)||ops[0]!=VFW_POWER_OPS_VTABLE||(ops[1]&255u)!=1u||ops[3]!=8u)return 0;
 s->power_entries=ops[2];s->power_scene_word=s->power_entries+16u;
 if(!vfw_ram(s->power_entries,sizeof s->power_operations)||!read(c,s->power_entries,s->power_operations,sizeof s->power_operations))return 0;
 return read(c,VFW_POWER_ROWS+8u*40u,s->scene_full,sizeof s->scene_full)&&read(c,VFW_POWER_ROWS+9u*40u,s->scene_crop,sizeof s->scene_crop);
}
static int vfw_power_valid(const struct vfw_snapshot*s){
 static const uint32_t operations[24]={0x20000,0,1,0x20001,9,5,0x20002,11,5,0x20003,10,5,0x20004,22,5,0x20005,25,25,0x20006,6,2,0x20007,23,5};
 static const uint32_t full[10]={0,0x30,3,0x60,0x60,6,0x60,0x60,5,3},crop[10]={0,0x20,3,0x20,0x66,3,0x30,0x20,5,3};
 for(unsigned i=0;i<24;i++)if(i==4u?(s->power_operations[i]!=8u&&s->power_operations[i]!=9u):s->power_operations[i]!=operations[i])return 0;
 return !memcmp(s->scene_full,full,sizeof full)&&!memcmp(s->scene_crop,crop,sizeof crop)&&s->power_scene_word==s->power_entries+16u;
}
static int vfw_snapshot_once(vfw_reader read,void*c,struct vfw_snapshot*s){
 uint32_t type=0;memset(s,0,sizeof *s);
 if(!vfw_word(read,c,VFW_ROOT,&s->root)||!vfw_ram(s->root,0x3a4)||!vfw_word(read,c,s->root,&type)||type!=VFW_FRONT_VTABLE)return 0;
 if(!vfw_word(read,c,s->root+0x26c,&s->manager)||s->manager!=VFW_PROFILE_MANAGER||!vfw_word(read,c,s->root+0x270,&s->vector)||!vfw_ram(s->vector,88)||!vfw_word(read,c,s->root+0x274,&s->count)||s->count!=22)return 0;
 if(!vfw_word(read,c,s->vector+6*4,&s->profile6)||!vfw_word(read,c,s->vector+19*4,&s->profile19)||!vfw_ram(s->profile6,VFW_PROFILE_BYTES)||!vfw_ram(s->profile19,VFW_PROFILE_BYTES)||s->profile6==s->profile19||s->profile6==s->root||s->profile19==s->root)return 0;
 if(!read(c,s->profile6,s->full,sizeof s->full)||!read(c,s->profile19,s->crop,sizeof s->crop)||!vfw_word(read,c,VFW_MAP_WORD,&s->mapping)||!vfw_word(read,c,VFW_DRIVER,&type)||type!=VFW_DRIVER_VTABLE||!read(c,VFW_DRIVER+8,&s->driver_modes,4))return 0;
 return read(c,VFW_WORK_META,s->work,sizeof s->work)&&read(c,VFW_SRC_META,s->src,sizeof s->src)&&vfw_word(read,c,VFW_WORK_TABLE,&s->work_frame)&&vfw_word(read,c,VFW_SRC_TABLE,&s->src_frame)&&vfw_power_capture(read,c,s)&&vfw_word(read,c,VFW_IPC_SETUP_WORD,&s->ipc_setup)&&vfw_word(read,c,0x813ebf60u,&s->native_ipc_preset);
}
static int vfw_capture(vfw_reader read,void*c,struct vfw_snapshot*s){struct vfw_snapshot other;return vfw_snapshot_once(read,c,s)&&vfw_snapshot_once(read,c,&other)&&!memcmp(s,&other,sizeof *s);}
struct vfw_ipc_route {uint32_t init,update,backend,qhd,ldc_policy,lcac_mask,lcac_or,blocks;};
/* Every replacement is an exact whole-block original or this revision.
 * Mixed/foreign code is rejected before a transaction. The TMC data area
 * is an exclusive lease, never unproven padding or a released CAP alias. */
static int vfw_block_state(vfw_reader read,void*c){
 uint32_t words[160];int boundary=-1,timing=-1;
 for(unsigned i=0;i<VFW_BLOCK_COUNT;i++){const struct vfw_block*b=vfw_blocks+i;if(!read(c,b->address,words,b->bytes))return -1;uint32_t h=vfw_hash(words,b->bytes);int state=h==b->stock_hash&&!memcmp(words,vfw_stock(b),b->bytes)?0:h==b->full_hash&&!memcmp(words,vfw_full_code(b),b->bytes)?1:-1;
  if(state<0)return -1;if(!i)boundary=state;else if(timing<0)timing=state;else if(state!=timing)return -1;}
 /* 2 is a dormant, completely installed timing facility with stock recipe.
  * Its fixed code is retained until reboot: no return PC can outlive it. */
 return timing?(boundary?1:2):boundary?-1:0;
}
static int vfw_one_block(const struct vfw_ops*o,unsigned index,int full,unsigned*count){
 const struct vfw_block*b=vfw_blocks+index;const uint32_t*value=full?vfw_full_code(b):vfw_stock(b);int ok=1;
 for(unsigned k=0;k<b->bytes/4;k++)if(vfw_full_code(b)[k]!=vfw_stock(b)[k]){if(count)(*count)++;int written=o->write(o->context,b->address+4*k,value[k]);ok&=written;if(count&&!written)return 0;}return ok;
}
static int vfw_one_cache(const struct vfw_ops*o,unsigned index){const struct vfw_block*b=vfw_blocks+index;uint32_t at=b->address&~63u;return o->cache(o->context,at,(b->address-at+b->bytes+63u)&~63u);}
static int vfw_timing_write(const struct vfw_ops*o,unsigned before,unsigned*count,int*resident,unsigned*status){
 if(before)return 1; /* Never rewrite a published callback body. */
 for(unsigned i=1;i<VFW_ENTRY_FIRST;i++)if(!vfw_one_block(o,i,1,count))return 0;
 /* Publish only single aligned ARM B words, after code is coherent. An old
  * OEM marker already on-stack sees flag 0 and returns through original code.
  * From this point the donor is retained even if the mode transaction fails. */
 for(unsigned i=1;i<VFW_ENTRY_FIRST;i++)if(!vfw_one_cache(o,i)){*status=VFW_CACHE_FAILED;return 0;}
 *resident=1;
 for(unsigned i=VFW_ENTRY_FIRST;i<VFW_BLOCK_COUNT;i++){
  if(!vfw_one_block(o,i,1,count))return 0;
  if(!vfw_one_cache(o,i)){*status=VFW_CACHE_FAILED;return 0;}
 }
 return 1;
}
static int vfw_block_write(const struct vfw_ops*o,int full,unsigned before,unsigned*count,int*resident,unsigned*status){
 if(!vfw_one_block(o,0,full,count))return 0;
 return before||!full?1:vfw_timing_write(o,before,count,resident,status);
}
static int vfw_timing_rollback(const struct vfw_ops*o,unsigned before,int resident){
 (void)before;int ok=1;
 if(resident){
  /* Complete/repair atomic entry words, never overwrite resident code. */
  uint32_t body[160];for(unsigned i=1;i<VFW_ENTRY_FIRST;i++){const struct vfw_block*b=vfw_blocks+i;
   ok&=o->read(o->context,b->address,body,b->bytes)&&!memcmp(body,vfw_full_code(b),b->bytes);}
  for(unsigned i=VFW_ENTRY_FIRST;i<VFW_BLOCK_COUNT;i++)ok&=vfw_one_block(o,i,1,NULL);
 }else for(unsigned i=1;i<VFW_BLOCK_COUNT;i++)ok&=vfw_one_block(o,i,0,NULL);
 return ok;
}
static int vfw_blocks_rollback(const struct vfw_ops*o,unsigned before,int resident){
 int ok=vfw_one_block(o,0,before==1u,NULL);return vfw_timing_rollback(o,before,resident)&&ok;
}
static int vfw_tmc_snapshot(vfw_reader read,void*c,uint32_t*out){uint32_t second[VFW_TMC_BYTES/4];return read(c,VFW_TMC_DATA,out,VFW_TMC_BYTES)&&read(c,VFW_TMC_DATA,second,VFW_TMC_BYTES)&&!memcmp(out,second,sizeof second);}
/* Count/mask facility ends exactly before the independent native string log
 * pointer at +0x118. Never read/clear/lease the string logger as count state. */
_Static_assert(VFW_TMC_DATA+VFW_TMC_BYTES==0x817dbce0u,"native string logger excluded");
_Static_assert(VFW_TIMING_DATA+256u==VFW_TMC_DATA+VFW_TMC_BYTES,"bounded count facility");
static int vfw_tmc_allowed(const uint32_t*p,int full){
 if(!full){for(unsigned i=0;i<VFW_TMC_BYTES/4;i++)if(p[i])return 0;return 1;}
 for(unsigned i=0;i<5;i++)if(p[i])return 0;
 if(p[5]&&!vfw_ram(p[5],4))return 0;for(unsigned i=(VFW_TIMING_DATA-VFW_TMC_DATA+256u)/4;i<VFW_TMC_BYTES/4;i++)if(p[i])return 0;
 return p[(VFW_TIMING_DATA-VFW_TMC_DATA)/4]==VFW_TIMING_MAGIC&&p[(VFW_TIMING_DATA-VFW_TMC_DATA)/4+1]==4u;
}
static int vfw_tmc_write(const struct vfw_ops*o,const uint32_t*p,unsigned*count){int ok=1;for(unsigned i=0;i<VFW_TMC_BYTES/4;i++){if(count)(*count)++;int written=o->write(o->context,VFW_TMC_DATA+4*i,p[i]);ok&=written;if(count&&!written)return 0;}return ok;}
static int vfw_read_route(vfw_reader read,void*c,struct vfw_ipc_route*r){uint32_t parent,type;int blocks=vfw_block_state(read,c);r->blocks=(uint32_t)blocks;return blocks>=0&&read(c,VFW_IPC_INIT_WORD,r,8)&&vfw_word(read,c,VFW_BACKEND_WORD,&r->backend)&&vfw_word(read,c,VFW_LDC_POLICY_WORD,&r->ldc_policy)&&vfw_word(read,c,VFW_LCAC_MASK_WORD,&r->lcac_mask)&&vfw_word(read,c,VFW_LCAC_OR_WORD,&r->lcac_or)&&vfw_word(read,c,VFW_BACKEND_ROOT,&parent)&&parent==VFW_BACKEND_VTABLE&&vfw_word(read,c,VFW_BACKEND_ROOT+0xd6cu,&r->qhd)&&vfw_ram(r->qhd,0xd44u)&&vfw_word(read,c,r->qhd,&type)&&type==VFW_QHD_VTABLE;}
static int vfw_capture_route(vfw_reader read,void*c,struct vfw_snapshot*s,struct vfw_ipc_route*r){struct vfw_ipc_route first;return vfw_read_route(read,c,&first)&&vfw_capture(read,c,s)&&vfw_read_route(read,c,r)&&!memcmp(&first,r,sizeof first);}
static int vfw_mode(const struct vfw_snapshot*s){
 if(s->full[0]!=VFW_FULL_VTABLE||s->crop[0]!=VFW_CROP_VTABLE||s->full[4]!=VFW_IMAGE_VTABLE||s->crop[4]!=VFW_IMAGE_VTABLE||s->full[1]!=1||s->crop[1]!=1||!vfw_values(s->full,vfw_native_full)||!vfw_power_valid(s))return VFW_UNKNOWN;
 /* RAW12, original unpacked/packed flag bytes are equal. No conversion of
  * an existing RAW10/QHD image into a purported full-width image. */
 if(s->full[0x50/4]!=0x00010402u||s->crop[0x50/4]!=0x00010402u)return VFW_UNKNOWN;
 if(s->ipc_setup!=VFW_IPC_SETUP_CROP&&s->ipc_setup!=VFW_IPC_SETUP_SCALE)return VFW_UNKNOWN;
 if(s->mapping==VFW_MAP_CROP&&vfw_values(s->crop,vfw_crop)&&s->power_operations[4]==9u&&s->ipc_setup==VFW_IPC_SETUP_CROP)return VFW_STOCK;
 if(s->mapping==VFW_MAP_FULL&&(vfw_values(s->crop,vfw_full)||vfw_values(s->crop,vfw_native_full))){
  if(s->power_operations[4]==8u)return vfw_values(s->crop,vfw_full)?VFW_FULL:VFW_LEGACY_WIDTH;
  if(s->ipc_setup==VFW_IPC_SETUP_CROP)return VFW_LEGACY_FULL;
 }
 return VFW_UNKNOWN;
}
static int vfw_route_mode(const struct vfw_snapshot*s,const struct vfw_ipc_route*r){
 if(r->ldc_policy!=VFW_LDC_POLICY_UD&&r->ldc_policy!=VFW_LDC_POLICY_SCALE)return VFW_UNKNOWN;
 int stock_lcac=r->lcac_mask==VFW_LCAC_MASK_STOCK&&r->lcac_or==VFW_LCAC_OR_STOCK;int full_lcac=r->lcac_mask==VFW_LCAC_MASK_FULL&&r->lcac_or==VFW_LCAC_OR_FULL;if(!stock_lcac&&!full_lcac)return VFW_UNKNOWN;
 if(r->blocks>2u)return VFW_UNKNOWN;
 int mode=vfw_mode(s);if(r->blocks==1u&&mode!=VFW_FULL)return VFW_UNKNOWN;int original=r->init==VFW_IPC_INIT_CROP&&r->update==VFW_IPC_UPDATE_CROP;
 int scaled=r->init==VFW_IPC_INIT_SCALE&&r->update==VFW_IPC_UPDATE_SCALE;
 if(mode==VFW_FULL||mode==VFW_LEGACY_WIDTH){
  if(original&&r->backend==VFW_BACKEND_SCALE&&s->ipc_setup==VFW_IPC_SETUP_CROP)return r->ldc_policy==VFW_LDC_POLICY_SCALE?(full_lcac?(mode==VFW_LEGACY_WIDTH?VFW_LEGACY_WIDTH:r->blocks==1u?VFW_FULL:VFW_LEGACY_BOUNDARY):VFW_LEGACY_LCAC):(stock_lcac?VFW_LEGACY_LDC:VFW_UNKNOWN);
  if(!stock_lcac)return VFW_UNKNOWN;
  if(r->ldc_policy!=VFW_LDC_POLICY_UD)return VFW_UNKNOWN;
  if(r->backend==VFW_BACKEND_UD&&s->ipc_setup==VFW_IPC_SETUP_SCALE&&(original||scaled))return VFW_LEGACY_UPDATE;
  if(original&&r->backend==VFW_BACKEND_UD&&s->ipc_setup==VFW_IPC_SETUP_CROP)return VFW_LEGACY_SCALE;
  return VFW_UNKNOWN;
 }
 return r->blocks!=1u&&stock_lcac&&original&&r->backend==VFW_BACKEND_UD&&r->ldc_policy==VFW_LDC_POLICY_UD?mode:VFW_UNKNOWN;
}
static int vfw_budget(const struct vfw_snapshot*s){
 /* The firmware's existing per-frame movieWork capacity is identical to
  * NX1. Account conservatively for active RAW12 as 16 bits at 128B stride.
  * The global sensor blanking rectangle is not the PP write-DMA rectangle. */
 const uint64_t stride=((uint64_t)vfw_full[2]*2u+127u)&~127ULL;
 return s->work_frame==VFW_WORK_FRAME&&s->src_frame==VFW_SRC_FRAME&&stride*vfw_full[3]<=s->work_frame&&3ULL*s->work_frame+10ULL*s->src_frame<=0x20000000ULL;
}
static unsigned vfw_movie_active(const struct vfw_snapshot*s){
 unsigned requested=s->driver_modes&255u,applied=(s->driver_modes>>8)&255u;
 unsigned flags=(requested>=23u&&requested<=25u?1u:0u)|(applied>=23u&&applied<=25u?2u:0u);
 /* Original8008fbdc clears ONLY the address fields. Sizes/counts survive
  * recipe teardown and must not be mistaken for a still-active allocation.
  * Stream/driver checks and the host's stopped UI bracket are independent. */
 /* CPowerCtrlVideo also owns 0x20000 preview (scene 0). Its identity is
  * not a recording flag: real idle mode13 snapshots retain this pointer.
  * Keep pointer/type validation and transaction stability checks above. */
 return flags|(s->work[0]?4u:0u)|(s->src[0]?8u:0u);
}
static int vfw_quiet(const struct vfw_snapshot*s){return !vfw_movie_active(s);}
static int vfw_geometry_write(const struct vfw_ops*o,uint32_t profile,const uint32_t*v,unsigned*count){
 for(unsigned i=0;i<6;i++){(*count)++;if(!o->write(o->context,profile+vfw_offsets[i],v[i]))return 0;}return 1;
}
static int vfw_cohere(const struct vfw_ops*o,uint32_t profile,uint32_t power_word){
 for(unsigned i=0;i<VFW_BLOCK_COUNT;i++){const struct vfw_block*b=vfw_blocks+i;uint32_t at=b->address&~63u,n=(b->address-at+b->bytes+63u)&~63u;if(!o->cache(o->context,at,n))return 0;}
 if(!o->cache(o->context,VFW_TMC_DATA&~63u,320u))return 0;
 return o->cache(o->context,profile&~63u,256u)&&o->cache(o->context,power_word&~63u,64u)&&o->cache(o->context,VFW_IPC_SETUP_WORD&~63u,64u)&&o->cache(o->context,VFW_BACKEND_WORD&~63u,64u)&&o->cache(o->context,VFW_LDC_POLICY_WORD&~63u,64u)&&o->cache(o->context,VFW_LCAC_MASK_WORD&~63u,64u)&&o->cache(o->context,VFW_MAP_WORD&~63u,64u);}
static int vfw_transaction(const struct vfw_ops*o,unsigned operation,struct vfw_reply*r){
 struct vfw_snapshot before,after;struct vfw_ipc_route before_route,after_route;uint32_t tmc_before[VFW_TMC_BYTES/4],tmc_target[VFW_TMC_BYTES/4];int resident=0;memset(r,0,sizeof *r);r->operation=operation;
 if((operation!=VFW_APPLY&&operation!=VFW_RESTORE&&operation!=VFW_TRACE)||!o->quiet(o->context)){r->status=VFW_BAD_CONTEXT;return 0;}
 if(!vfw_capture_route(o->read,o->context,&before,&before_route)){r->status=VFW_CHANGED;return 0;}
 int mode=vfw_route_mode(&before,&before_route),target=operation==VFW_TRACE?mode:operation==VFW_APPLY?VFW_FULL:VFW_STOCK;
 resident=before_route.blocks!=0u;r->block_before=r->block_after=before_route.blocks;
 r->before=(uint32_t)mode;r->after=(uint32_t)mode;r->root=before.root;r->profile6=before.profile6;r->profile19=before.profile19;
 r->power_before=r->power_after=before.power_operations[4];r->power_scene_word=before.power_scene_word;r->power_manager=before.power_manager;r->power_video=before.power_video;r->power_ops=before.power_ops;
 r->ipc_setup_before=r->ipc_setup_after=before.ipc_setup;
 r->ipc_init_before=r->ipc_init_after=before_route.init;r->ipc_update_before=r->ipc_update_after=before_route.update;r->backend_before=r->backend_after=before_route.backend;r->ldc_policy_before=r->ldc_policy_after=before_route.ldc_policy;r->lcac_mask_before=r->lcac_mask_after=before_route.lcac_mask;r->lcac_or_before=r->lcac_or_after=before_route.lcac_or;
 for(unsigned i=0;i<6;i++)r->before_values[i]=r->after_values[i]=before.crop[vfw_offsets[i]/4u];
 r->work_frame=before.work_frame;r->src_frame=before.src_frame;r->work_total=before.work_frame*3u;r->src_total=before.src_frame*10u;r->raw_dma_max_bytes=((vfw_full[2]*2u+127u)&~127u)*vfw_full[3];
 if(!mode){r->status=VFW_BAD_PROFILE;return 0;}
 if(!vfw_budget(&before)){r->status=VFW_BAD_BUDGET;return 0;}
 if(mode==target&&(operation!=VFW_TRACE||before_route.blocks)){r->status=VFW_OK;return 1;}
 if(operation==VFW_TRACE&&mode!=VFW_FULL&&mode!=VFW_STOCK){r->status=VFW_BAD_PROFILE;return 0;}
 if(!vfw_quiet(&before)){r->status=VFW_MOVIE_ACTIVE;return 0;}
 if(!vfw_tmc_snapshot(o->read,o->context,tmc_before)){r->status=VFW_TMC_UNSTABLE;return 0;}
 if(!vfw_tmc_allowed(tmc_before,before_route.blocks!=0u)){r->status=VFW_TMC_BUSY;return 0;}
 memset(tmc_target,0,sizeof tmc_target);
 if(before_route.blocks){memcpy(tmc_target,tmc_before,sizeof tmc_target);tmc_target[5]=0;unsigned j=(VFW_TIMING_DATA-VFW_TMC_DATA)/4;for(unsigned k=3;k<8;k++)tmc_target[j+k]=0;}
 else if(target==VFW_FULL||operation==VFW_TRACE){unsigned j=(VFW_TIMING_DATA-VFW_TMC_DATA)/4;tmc_target[j]=VFW_TIMING_MAGIC;tmc_target[j+1]=4u;}
 if(operation==VFW_TRACE){
  /* Arm observation only. Never store geometry, mapping, clocks, codec or
   * the blue-boundary recipe. Published fixed bodies survive until reboot. */
  r->status=VFW_WRITE_FAILED;
  if(!o->quiet(o->context)||!vfw_tmc_write(o,tmc_target,&r->writes)||
     !vfw_timing_write(o,before_route.blocks,&r->writes,&resident,&r->status))goto trace_rollback;
  for(unsigned i=1;i<VFW_BLOCK_COUNT;i++)if(!vfw_one_cache(o,i)){r->status=VFW_CACHE_FAILED;goto trace_rollback;}
  if(!o->cache(o->context,VFW_TMC_DATA&~63u,320u)){r->status=VFW_CACHE_FAILED;goto trace_rollback;}
  if(!vfw_capture_route(o->read,o->context,&after,&after_route)||memcmp(&before,&after,sizeof before)||
     after_route.qhd!=before_route.qhd||after_route.init!=before_route.init||after_route.update!=before_route.update||
     after_route.backend!=before_route.backend||after_route.ldc_policy!=before_route.ldc_policy||
     after_route.lcac_mask!=before_route.lcac_mask||after_route.lcac_or!=before_route.lcac_or||
     vfw_route_mode(&after,&after_route)!=mode){r->status=VFW_VERIFY_FAILED;goto trace_rollback;}
  r->block_after=after_route.blocks;r->status=VFW_OK;return 1;
trace_rollback:
  {int restored=vfw_timing_rollback(o,before_route.blocks,resident);
   const uint32_t*restore_tmc=resident&&!before_route.blocks?tmc_target:tmc_before;
   restored&=vfw_tmc_write(o,restore_tmc,NULL);
   for(unsigned i=1;i<VFW_BLOCK_COUNT;i++)restored&=vfw_one_cache(o,i);
   restored&=o->cache(o->context,VFW_TMC_DATA&~63u,320u);
   if(restored&&vfw_capture_route(o->read,o->context,&after,&after_route)&&!memcmp(&before,&after,sizeof before)&&vfw_route_mode(&after,&after_route)==mode){r->rollback_verified=1;r->block_after=after_route.blocks;}
   return 0;}
 }
 const uint32_t*values=target==VFW_FULL?vfw_full:vfw_crop;
 if(!o->quiet(o->context)){r->status=VFW_CHANGED;return 0;}
 /* Publish cached image geometry first; the final aligned mapping word is
  * the commit point. Only the guarded two-instruction mask pair changes; no ownership changes. */
 if(!vfw_geometry_write(o,before.profile19,values,&r->writes)){r->status=VFW_WRITE_FAILED;goto rollback;}
 r->writes++;if(!o->write(o->context,before.power_scene_word,target==VFW_FULL?8u:9u)){r->status=VFW_WRITE_FAILED;goto rollback;}
 /* Keep the original UD class intact. The original QHD class selects
  * E005 itself after the native initialization-boundary switch below. */
 r->writes++;if(!o->write(o->context,VFW_IPC_SETUP_WORD,VFW_IPC_SETUP_CROP)){r->status=VFW_WRITE_FAILED;goto rollback;}
 /* Restore the original NX500 UD lifecycle for both full and native
  * geometries. E005 alone does not establish the random/tiled streaming
  * handshake. Never install QHD init/update into the shared UD vtable.
  * These stores also recover the exactly recognized R9w pair. */
 r->writes++;if(!o->write(o->context,VFW_IPC_INIT_WORD,VFW_IPC_INIT_CROP)){r->status=VFW_WRITE_FAILED;goto rollback;}
 r->writes++;if(!o->write(o->context,VFW_IPC_UPDATE_WORD,VFW_IPC_UPDATE_CROP)){r->status=VFW_WRITE_FAILED;goto rollback;}
 /* This literal jump-table word is consumed only by the original full
  * Ipc setParam/initialize boundary. It cannot replace the active per-frame
  * method before the matching preset, SRA and source context are rebuilt. */
 r->writes++;if(!o->write(o->context,VFW_BACKEND_WORD,target==VFW_FULL?VFW_BACKEND_SCALE:VFW_BACKEND_UD)){r->status=VFW_WRITE_FAILED;goto rollback;}
 /* Pair the downstream LDC protocol with the IPC backend. This is consumed
  * when the original recipe is recreated, never midway through a frame. */
 r->writes++;if(!o->write(o->context,VFW_LDC_POLICY_WORD,target==VFW_FULL?VFW_LDC_POLICY_SCALE:VFW_LDC_POLICY_UD)){r->status=VFW_WRITE_FAILED;goto rollback;}
 r->writes++;if(!o->write(o->context,VFW_LCAC_MASK_WORD,target==VFW_FULL?VFW_LCAC_MASK_FULL:VFW_LCAC_MASK_STOCK)){r->status=VFW_WRITE_FAILED;goto rollback;}
 r->writes++;if(!o->write(o->context,VFW_LCAC_OR_WORD,target==VFW_FULL?VFW_LCAC_OR_FULL:VFW_LCAC_OR_STOCK)){r->status=VFW_WRITE_FAILED;goto rollback;}
 r->status=VFW_WRITE_FAILED;if(!vfw_tmc_write(o,tmc_target,&r->writes)||!vfw_block_write(o,target==VFW_FULL,before_route.blocks,&r->writes,&resident,&r->status))goto rollback;
 r->writes++;if(!o->write(o->context,VFW_MAP_WORD,target==VFW_FULL?VFW_MAP_FULL:VFW_MAP_CROP)){r->status=VFW_WRITE_FAILED;goto rollback;}
 if(!vfw_cohere(o,before.profile19,before.power_scene_word)){r->status=VFW_CACHE_FAILED;goto rollback;}
 if(!vfw_capture_route(o->read,o->context,&after,&after_route)||after_route.qhd!=before_route.qhd||after.root!=before.root||after.profile6!=before.profile6||after.profile19!=before.profile19||memcmp(after.full,before.full,sizeof after.full)||vfw_route_mode(&after,&after_route)!=target||!vfw_quiet(&after)){r->status=VFW_VERIFY_FAILED;goto rollback;}
 for(unsigned i=0;i<VFW_PROFILE_BYTES/4u;i++){int changed=0;for(unsigned k=0;k<6;k++)changed|=i==vfw_offsets[k]/4u;if(!changed&&after.crop[i]!=before.crop[i]){r->status=VFW_VERIFY_FAILED;goto rollback;}}
 if(after.power_manager!=before.power_manager||after.power_video!=before.power_video||after.power_ops!=before.power_ops||after.power_entries!=before.power_entries||after.power_current!=before.power_current||after.power_scene_word!=before.power_scene_word){r->status=VFW_VERIFY_FAILED;goto rollback;}
 for(unsigned i=0;i<24;i++)if(i!=4u&&after.power_operations[i]!=before.power_operations[i]){r->status=VFW_VERIFY_FAILED;goto rollback;}
 r->block_after=after_route.blocks;
 r->power_after=after.power_operations[4];
 r->ipc_setup_after=after.ipc_setup;
 r->ipc_init_after=after_route.init;r->ipc_update_after=after_route.update;r->backend_after=after_route.backend;r->ldc_policy_after=after_route.ldc_policy;r->lcac_mask_after=after_route.lcac_mask;r->lcac_or_after=after_route.lcac_or;
 r->after=(uint32_t)target;for(unsigned i=0;i<6;i++)r->after_values[i]=after.crop[vfw_offsets[i]/4u];r->status=VFW_OK;return 1;
rollback:
 /* Restore exactly the observed known state, never an arbitrary snapshot
  * pointer or whole profile. Attempt every word even if one write failed. */
 {int restored=1;for(unsigned i=0;i<6;i++)restored&=o->write(o->context,before.profile19+vfw_offsets[i],r->before_values[i]);restored&=o->write(o->context,before.power_scene_word,r->power_before);restored&=o->write(o->context,VFW_IPC_SETUP_WORD,before.ipc_setup);restored&=o->write(o->context,VFW_IPC_INIT_WORD,before_route.init);restored&=o->write(o->context,VFW_IPC_UPDATE_WORD,before_route.update);restored&=o->write(o->context,VFW_BACKEND_WORD,before_route.backend);restored&=o->write(o->context,VFW_LDC_POLICY_WORD,before_route.ldc_policy);restored&=o->write(o->context,VFW_LCAC_MASK_WORD,before_route.lcac_mask);restored&=o->write(o->context,VFW_LCAC_OR_WORD,before_route.lcac_or);restored&=vfw_blocks_rollback(o,before_route.blocks,resident);restored&=vfw_tmc_write(o,resident&&!before_route.blocks?tmc_target:tmc_before,NULL);restored&=o->write(o->context,VFW_MAP_WORD,before.mapping);restored&=vfw_cohere(o,before.profile19,before.power_scene_word);
  struct vfw_ipc_route expected_route=before_route;if(resident&&!expected_route.blocks)expected_route.blocks=2u;
  if(restored&&vfw_capture_route(o->read,o->context,&after,&after_route)&&!memcmp(&expected_route,&after_route,sizeof expected_route)&&after.root==before.root&&after.profile6==before.profile6&&after.profile19==before.profile19&&!memcmp(before.full,after.full,sizeof before.full)&&!memcmp(before.crop,after.crop,sizeof before.crop)&&after.mapping==before.mapping&&after.ipc_setup==before.ipc_setup&&after.power_manager==before.power_manager&&after.power_video==before.power_video&&after.power_ops==before.power_ops&&!memcmp(after.power_operations,before.power_operations,sizeof before.power_operations)){uint32_t restored_tmc[VFW_TMC_BYTES/4];if(!vfw_tmc_snapshot(o->read,o->context,restored_tmc)||memcmp(resident&&!before_route.blocks?tmc_target:tmc_before,restored_tmc,sizeof tmc_before))return 0;r->block_after=expected_route.blocks;r->rollback_verified=1;r->after=(uint32_t)mode;r->power_after=r->power_before;r->ipc_setup_after=r->ipc_setup_before;r->ipc_init_after=r->ipc_init_before;r->ipc_update_after=r->ipc_update_before;r->backend_after=r->backend_before;r->ldc_policy_after=r->ldc_policy_before;r->lcac_mask_after=r->lcac_mask_before;r->lcac_or_after=r->lcac_or_before;}}
 return 0;
}
static int vfw_reply_valid(const struct vfw_reply*r,uint32_t nonce){return r->magic==VFW_MAGIC&&r->schema==VFW_SCHEMA&&r->nonce==nonce&&r->hash==vfw_hash(r,offsetof(struct vfw_reply,hash))&&r->commit==(VFW_COMMIT_XOR^r->hash^nonce);}
static int vfw_returned(const uint32_t*t,unsigned blocks,unsigned reported,unsigned index){
 if(!t||blocks!=45056u||index>blocks-VFW_CAP_BLOCKS)return 0;unsigned free_seen=0;
 for(unsigned i=0;i<blocks;){unsigned kind=t[4*i]&255u,span=t[4*i+1];
  if(!kind){if(span)return 0;free_seen++;i++;continue;}
  if(!span||span>blocks-i||((i<index+VFW_CAP_BLOCKS)&&(i+span>index)))return 0;
  for(unsigned j=1;j<span;j++){unsigned k=t[4*(i+j)]&255u,n=t[4*(i+j)+1];if(k?(k!=kind||n!=span-j):n!=0)return 0;}
  i+=span;
 }return free_seen==reported;
}
_Static_assert(sizeof(struct vfw_context)==64,"fixed full-width command context");
_Static_assert(sizeof(struct vfw_reply)==256,"fixed full-width transaction receipt");
_Static_assert(sizeof(struct vfw_snapshot)==672,"full-width profile, power and native scaler snapshot");
#endif
