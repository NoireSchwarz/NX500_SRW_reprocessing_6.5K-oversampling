/* SPDX-License-Identifier: AGPL-3.0-only. Transient OEM attribute handler.
 * Runs on the RTOS core so data stores and native cache maintenance agree.
 * Restores the dispatch slot before the bounded native-backend transaction.
 * The block is retired by a later original console command before release.
 */
#include "../../common/video_fullwidth_contract.h"
static volatile struct vfw_context ctx __attribute__((section(".context"),used));
typedef int32_t(*native3)(uint32_t,uint32_t,uint32_t);
static int32_t native(uint32_t fn,uint32_t a,uint32_t b,uint32_t c){return((native3)fn)(a,b,c);}
static inline void barrier(void){__asm__ volatile("dsb sy\n\tisb sy":::"memory");}
static inline volatile uint32_t*ptr(uint32_t at){return(volatile uint32_t*)(uintptr_t)at;}
static int read_ram(void*c,uint32_t at,void*out,unsigned n){(void)c;if(!n||n>sizeof(struct vfw_snapshot)||(at&3u)||(n&3u)||at<0x80000000u||(uint64_t)at+n>0x94000000ULL)return 0;uint32_t*b=out;for(unsigned i=0;i<n/4;i++)b[i]=ptr(at)[i];barrier();return 1;}
static int write_config(void*c,uint32_t at,uint32_t value){(void)c;int allowed=at==VFW_MAP_WORD||at==ctx.power_scene_word||at==VFW_IPC_SETUP_WORD||at==VFW_IPC_INIT_WORD||at==VFW_IPC_UPDATE_WORD||at==VFW_BACKEND_WORD||at==VFW_LDC_POLICY_WORD||at==VFW_LCAC_MASK_WORD||at==VFW_LCAC_OR_WORD;for(unsigned i=0;i<VFW_BLOCK_COUNT;i++){const struct vfw_block*b=vfw_blocks+i;allowed|=at>=b->address&&at<b->address+b->bytes;}allowed|=at>=VFW_TMC_DATA&&at<VFW_TMC_DATA+VFW_TMC_BYTES;for(unsigned i=0;i<6;i++)allowed|=at==ctx.profile19+vfw_offsets[i];if(!allowed)return 0;*ptr(at)=value;barrier();return *ptr(at)==value;}
static int cohere(void*c,uint32_t at,unsigned n){(void)c;int32_t rc=native(VFW_CACHE,VFW_ALLOCATOR,at,n);barrier();int code=at==(VFW_LCAC_MASK_WORD&~63u);for(unsigned i=0;i<VFW_BLOCK_COUNT;i++)code|=at==(vfw_blocks[i].address&~63u);if(rc>=0&&code){/* Original NX500 cache routine: clean D lines, invalidate I lines and branch predictor. */rc=native(0x800245b4u,at,n,1u);barrier();}return rc>=0;}
static int quiet(void*c){(void)c;return ctx.magic==VFW_MAGIC&&ctx.schema==VFW_SCHEMA&&ctx.cap>=0x94000000u&&!(ctx.cap&16383u)&&ctx.bytes==VFW_CAP_BYTES&&(uint64_t)ctx.cap+ctx.bytes<=0xbb6cc000ULL&&ctx.nonce&&*ptr(VFW_ROOT)==ctx.root&&vfw_ram(ctx.root,0x3a4u)&&*ptr(ctx.root)==VFW_FRONT_VTABLE&&vfw_ram(ctx.profile6,VFW_PROFILE_BYTES)&&vfw_ram(ctx.profile19,VFW_PROFILE_BYTES)&&vfw_ram(ctx.power_manager,24)&&vfw_ram(ctx.power_video,28)&&vfw_ram(ctx.power_ops,16)&&vfw_ram(ctx.power_entries,96)&&ctx.power_scene_word==ctx.power_entries+16u&&*ptr(VFW_POWER_ROOT)==ctx.power_manager&&ptr(ctx.power_manager)[3]==ctx.power_video&&ptr(ctx.power_video)[6]==ctx.power_ops&&ptr(ctx.power_ops)[2]==ctx.power_entries;}
void*memset(void*p,int c,size_t n){unsigned char*b=p;while(n--)*b++=(unsigned char)c;return p;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;while(n--){if(*x!=*y)return*x<*y?-1:1;x++;y++;}return 0;}
void*memcpy(void*out,const void*in,size_t n){unsigned char*d=out;const unsigned char*s=in;while(n--)*d++=*s++;return out;}
static void publish(struct vfw_reply*r){
 r->magic=VFW_MAGIC;r->schema=VFW_SCHEMA;r->nonce=ctx.nonce;r->hash=vfw_hash(r,offsetof(struct vfw_reply,hash));r->commit=0;
 volatile uint32_t*out=ptr(ctx.cap+VFW_REPLY);const uint32_t*in=(const uint32_t*)r;out[63]=0;barrier();for(unsigned i=0;i<63;i++)out[i]=in[i];barrier();out[63]=VFW_COMMIT_XOR^r->hash^ctx.nonce;barrier();
}
__attribute__((used,visibility("default"),section(".text.entry")))
int32_t nxl_video_entry(uint32_t packet){
 volatile uint32_t*p=ptr(packet);uint32_t payload[3];
 if(!packet||p[1]!=12||!p[4])return native(VFW_ORIGINAL,packet,0,0);
 for(unsigned i=0;i<3;i++)payload[i]=ptr(p[4])[i];
 if(payload[0]!=ctx.nonce||payload[1]!=ctx.cap+VFW_CONTEXT||payload[2]!=ctx.operation)return native(VFW_ORIGINAL,packet,0,0);
 if(!quiet(NULL)||*ptr(VFW_SLOT)!=ctx.cap||ptr(VFW_SLOT)[1])return -1;
 /* No other future message can enter our code after this cache operation. */
 *ptr(VFW_SLOT)=VFW_ORIGINAL;barrier();
 int slot_ok=cohere(NULL,VFW_SLOT&~4095u,4096u);
 struct vfw_reply result;memset(&result,0,sizeof result);result.operation=ctx.operation;
 if(!slot_ok){result.status=VFW_CACHE_FAILED;publish(&result);return -1;}
 result.slot_restored=1;result.status=VFW_WAITING;result.phase=1;publish(&result);
 volatile uint32_t*control=ptr(ctx.cap+VFW_CONTROL);unsigned spins=0;
 while(control[0]!=(ctx.nonce^VFW_GO_XOR)){
  if(control[1]==~ctx.nonce||++spins>=50000000u){result.status=VFW_ABORTED;result.phase=2;publish(&result);return -1;}
 }
 struct vfw_snapshot current;
 if(!vfw_capture(read_ram,NULL,&current)||current.root!=ctx.root||current.profile6!=ctx.profile6||current.profile19!=ctx.profile19||current.power_manager!=ctx.power_manager||current.power_video!=ctx.power_video||current.power_ops!=ctx.power_ops||current.power_entries!=ctx.power_entries||current.power_scene_word!=ctx.power_scene_word){result.status=VFW_CHANGED;result.slot_restored=1;result.phase=2;publish(&result);return -1;}
 struct vfw_ops ops;volatile struct vfw_ops*setup=&ops;
 setup->context=NULL;setup->read=read_ram;setup->write=write_config;setup->cache=cohere;setup->quiet=quiet;
 int ok=vfw_transaction(&ops,ctx.operation,&result);result.slot_restored=1;
 /* Read through the ORIGINAL function and its literal table on the RTOS
  * core, after cache maintenance; Linux readback alone is not evidence. */
 result.source_mode=(result.before==VFW_FULL||result.before==VFW_LEGACY_FULL||result.before==VFW_LEGACY_SCALE||result.before==VFW_LEGACY_UPDATE||result.before==VFW_LEGACY_LDC||result.before==VFW_LEGACY_LCAC||result.before==VFW_LEGACY_WIDTH||result.before==VFW_LEGACY_BOUNDARY)?23u:result.before==VFW_STOCK?25u:0u;
 if(result.after==VFW_FULL||result.after==VFW_STOCK)result.target_mode=(uint32_t)native(VFW_MAP_FUNCTION,ctx.root,19,0);
 if(ok&&result.target_mode!=(result.after==VFW_FULL?23u:25u)){result.status=VFW_VERIFY_FAILED;ok=0;}
 typedef int32_t(*lookup4)(uint32_t,uint32_t,uint32_t,uint32_t);
 if(ok&&(((lookup4)VFW_POWER_LOOKUP)(current.power_ops,0x20001u,(uint32_t)(uintptr_t)&result.native_power_scene,0)!=1||result.native_power_scene!=(result.after==VFW_FULL?8u:9u))){result.status=VFW_VERIFY_FAILED;ok=0;}
 result.phase=2;publish(&result);return ok?0:-1;
}
