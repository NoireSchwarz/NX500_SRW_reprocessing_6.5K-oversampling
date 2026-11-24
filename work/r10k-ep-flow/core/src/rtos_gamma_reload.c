/* SPDX-License-Identifier: AGPL-3.0-only.
 * Only original Gamma callbacks write Gamma DDR. Original parent update owns
 * parameter queueing; no new SRAM load or frame/DMA-start operation is added.
 */
#include "../../common/video_fullwidth_contract.h"
#include "../../common/gamma_reload.h"
#include "../../common/vlog8_contract.h"
static volatile struct vfw_context ctx __attribute__((section(".context"),used));
typedef int32_t(*native3)(uint32_t,uint32_t,uint32_t);
static int32_t native(uint32_t fn,uint32_t a,uint32_t b,uint32_t c){return((native3)fn)(a,b,c);}
static __attribute__((noinline)) void barrier(void){__asm__ volatile("dsb sy\n\tisb sy":::"memory");}
static inline volatile uint32_t*ptr(uint32_t at){return(volatile uint32_t*)(uintptr_t)at;}
void*memset(void*p,int c,size_t n){unsigned char*b=p;while(n--)*b++=(unsigned char)c;return p;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;while(n--){if(*x!=*y)return*x<*y?-1:1;x++;y++;}return 0;}
void*memcpy(void*out,const void*in,size_t n){unsigned char*d=out;const unsigned char*s=in;while(n--)*d++=*s++;return out;}
/* These hashes cover Normal-NC CAP/DDR RAM, never device registers.
 * Same little-endian byte stream, with one compact shared FNV loop. */
static uint32_t hash_words(uint32_t at,unsigned words){uint32_t h=2166136261u;const volatile unsigned char*p=(const volatile unsigned char*)(uintptr_t)at;for(unsigned i=0;i<words*4u;i++)h=(h^p[i])*16777619u;return h;}
static volatile struct ng_state*state_at(uint32_t cap){return(volatile struct ng_state*)(uintptr_t)(cap+NG_STATE);}
static volatile struct ng_state*own_state(void){return state_at(ctx.cap);}
static int cap_ok(uint32_t cap){return cap>=0x94000000u&&!(cap&16383u)&&(uint64_t)cap+VFW_CAP_BYTES<=0xbb6cc000ULL;}
static int ng_owner(uint32_t cap,uint32_t out[4]){if(!cap_ok(cap))return 0;uint32_t at=0x81dfc908u+((cap-0x94000000u)/16384u)*16u;for(unsigned i=0;i<4;i++)out[i]=ptr(at)[i];barrier();for(unsigned i=0;i<4;i++)if(out[i]!=ptr(at)[i])return 0;return(out[0]&255u)==0x23u&&out[1]==VFW_CAP_BLOCKS;}
static int mappings(void){return *ptr(0x81002edcu)==0xbb701402u&&*ptr(0x813ebf50u)==0xbb71b300u&&*ptr(0x813ebf54u)==0xbb71d300u;}
/* Original registration chain; bounded, owner-checked and cycle-checked. */
static uint32_t registered_node(uint32_t parent,uint32_t vptr){
 if(!vfw_ram(parent,0x18u)||(*ptr(parent)!=NG_PARENT_VPTR0&&*ptr(parent)!=NG_PARENT_VPTR1))return 0;
 uint32_t child=ptr(parent)[1],found=0,visited[64];unsigned count=0;
 while(child){
  if(count>=64u||!vfw_ram(child,0x20u)||ptr(child)[3]!=parent)return 0;
  for(unsigned i=0;i<count;i++)if(visited[i]==child)return 0;visited[count++]=child;
  if(*ptr(child)==vptr){if(found)return 0;found=child;}child=ptr(child)[5];
 }return found;
}
static uint32_t registered_gamma(uint32_t parent){return registered_node(parent,0x8062f6b0u);}
static int shape(uint32_t object,uint32_t type,const uint32_t widths[5],unsigned bytes){
 if(!vfw_ram(object,bytes)||*ptr(object)!=type||ptr(object)[7]!=5||!vfw_ram(ptr(object)[6],20u))return 0;
 for(unsigned i=0;i<5;i++)if(ptr(ptr(object)[6])[i]!=widths[i])return 0;
 return 1;
}
static uint32_t qualify(uint32_t object,uint32_t*parent,uint32_t*context){
 if(!vfw_ram(object,0xf74u)||*ptr(object)!=0x8062f6b0u)return 1;
 *parent=ptr(object)[3];*context=ptr(object)[2];
 if(!vfw_ram(*parent,0x18u)||(*ptr(*parent)!=NG_PARENT_VPTR0&&*ptr(*parent)!=NG_PARENT_VPTR1))return 2;
 unsigned bank=(ptr(*parent)[3]>>8)&255u;if(bank==1)return 8;if(bank)return 3;
 if(registered_gamma(*parent)!=object)return 4;
 const uint32_t widths[5]={3,5,4,13,2};
 if(!vfw_ram(*context,0xb0u))return 5;
 if(!shape(object,0x8062f6b0u,widths,0xf74u))return 6;
 if(!mappings())return 7;return 0;
}
static int same_words(const volatile uint32_t*a,const volatile uint32_t*b,unsigned n){for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static void barrier(void);
/* Invalidate ONLY the first RGB source-cache array. Original encoder
 * compares it with the live calibrated matrix, recopies the real array and
 * toggles field0. Preserve the cached R2Y/SE fields and key: the full native
 * invalidator would unnecessarily re-run its direct SE register setter. */
static void invalidate_color(uint32_t object){
 for(unsigned i=0;i<9;i++)*(volatile uint16_t*)(uintptr_t)(object+0xe3cu+i*2u)=0xffffu;
 barrier();
}
/* Original parent update accepts NULL and obtains its default context via
 * vtable+0x14. Resolve it ONCE, then supply that exact pointer to original
 * child traversal, preserving the factory getter's state queries. */
static __attribute__((noinline)) uint32_t transaction_nodes(volatile struct ng_state*g,uint32_t parent,uint32_t raw,uint32_t gamma,uint32_t*color,uint32_t*context){
 const uint32_t gw[5]={3,5,4,13,2},cw[5]={2,2,2,2,7};
 g->binding_parent=parent;g->binding_parent_vptr=*ptr(parent);g->binding_input_raw=raw;
 g->binding_gamma=gamma;*color=registered_node(parent,NG_COLOR_VPTR);g->binding_color=*color;
 g->binding_gamma_context_before=ptr(gamma)[2];g->binding_color_context_before=*color?ptr(*color)[2]:0;
 g->binding_input_resolved=0;g->binding_getter=0;
 if(!*color)return 2;
 if(!shape(gamma,0x8062f6b0u,gw,0xf74u))return 3;
 if(!shape(*color,NG_COLOR_VPTR,cw,0xeb8u))return 4;
 if(!mappings())return 5;
 *context=raw;
 if(!*context){
  uint32_t getter=*ptr(*ptr(parent)+0x14u);g->binding_getter=getter;
  if(getter!=0x8038d024u&&getter!=0x803c56f4u)return 6;
  g->binding_getter_calls++;*context=(uint32_t)native(getter,parent,0,0);
 }
 g->binding_input_resolved=*context;
 return vfw_ram(*context,0xc4u)?0u:7u;
}
/* These fields have one RTOS-core writer domain; Linux only reads them.
 * Use bounded local IRQ/FIQ masking instead of relying on an exclusive
 * monitor for a Normal-NC CAP mapping. Never call native code while masked. */
static uint32_t irq_save(void){uint32_t saved;__asm__ volatile("mrs %0, cpsr\n\tcpsid if":"=r"(saved)::"memory","cc");return saved;}
static void irq_restore(uint32_t saved){__asm__ volatile("dmb sy\n\tmsr cpsr_c, %0"::"r"(saved):"memory","cc");}
static __attribute__((noinline)) int take(volatile uint32_t*p,uint32_t value){uint32_t saved=irq_save();int ok=*p==0;if(ok)*p=value;irq_restore(saved);return ok;}
static __attribute__((noinline)) void release(volatile uint32_t*p){uint32_t saved=irq_save();*p=0;irq_restore(saved);}
static __attribute__((noinline)) void count_one(volatile uint32_t*p){uint32_t saved=irq_save();*p=*p+1u;irq_restore(saved);}
static const uint32_t log_x[19]={0u,24u,48u,65u,95u,148u,209u,296u,516u,953u,1804u,2965u,4578u,5995u,7991u,10461u,13464u,15000u,16383u};
static const uint32_t log_y[19]={126u,138u,149u,157u,171u,195u,218u,243u,288u,343u,404u,453u,497u,524u,554u,581u,607u,618u,627u};
__attribute__((used,visibility("default")))
int32_t nxl_gamma_compute(uint32_t object,uint32_t input,uint32_t unused){
 volatile struct ng_state*g=own_state();
 if(g->magic!=NG_MAGIC||g->schema!=NG_SCHEMA||g->cap!=ctx.cap)return native(NG_CALCULATE,object,input,unused);
 count_one(&g->calls);uint32_t parent=0,context=0,q=qualify(object,&parent,&context);
 if(q==8){count_one(&g->bank1_calls);return native(NG_CALCULATE,object,input,unused);}
 if(!g->parent_active||g->suspended||parent!=g->active_parent||context!=g->active_context){count_one(&g->bypasses);return native(NG_CALCULATE,object,input,unused);}
 if(q){count_one(q==8?&g->bank1_calls:&g->bypasses);g->qualification_reason=q;g->last_object=object;g->last_parent=parent;return native(NG_CALCULATE,object,input,unused);}
 if(!take(&g->busy,1)){count_one(&g->reentrant);return native(NG_CALCULATE,object,input,unused);}
 g->sequence++;barrier();g->qualified++;g->last_object=object;g->last_parent=parent;g->last_context=context;g->last_input=input;g->qualification_reason=0;
 const uint32_t mode=(g->force_factory?NG_FACTORY:g->mode),epoch=g->epoch;int32_t original=native(NG_CALCULATE,object,input,unused);g->original_return=(uint32_t)original;
 if(original!=1){g->transaction_fault=g->fault=7;g->failures++;goto done;}
 for(unsigned i=0;i<19;i++){g->factory_x[i]=ptr(object+0xe40u)[i];g->factory_y[i]=ptr(object+0xe8cu)[i];}
 if(mode==NG_FACTORY||mode==NG_OFF){
  for(unsigned i=0;i<NG_WORDS;i++)g->factory[i]=g->target[i]=ptr(NG_DDR0)[i];
  for(unsigned i=0;i<19;i++){g->effective_x[i]=ptr(object+0xe40u)[i];g->effective_y[i]=ptr(object+0xe8cu)[i];}
  g->factory_hash=hash_words(NG_DDR0,NG_WORDS);g->target_hash=g->factory_hash;g->applied_epoch=epoch;g->applied_object=object;g->calculator_return=(uint32_t)original;g->writer_return=0;g->fault=0;goto done;
 }
 if(mode>NG_LOG||qualify(object,&parent,&context)){g->transaction_fault=g->fault=1;g->failures++;goto done;}
 g->factory_hash=hash_words(NG_DDR0,NG_WORDS);g->bank1_hash=hash_words(NG_DDR1,NG_WORDS);
 for(unsigned i=0;i<NG_WORDS;i++)g->factory[i]=ptr(NG_DDR0)[i];
 const uint32_t obj=ctx.cap+NG_OBJECT,meta=ctx.cap+NG_META,width=ctx.cap+NG_WIDTHS,par=ctx.cap+NG_PARENT;
 volatile uint32_t*o=ptr(obj);o[0]=0x8062f6b0u;o[2]=meta;o[3]=par;o[6]=width;o[7]=5;
 *ptr(meta)=2;*ptr(par+12)=0;const uint32_t widths[5]={3,5,4,13,2};for(unsigned i=0;i<5;i++)ptr(width)[i]=widths[i];
 uint32_t previous_x=0;int32_t previous_y=-1024;int valid=1;
 for(unsigned i=0;i<19;i++){
  uint32_t x=mode==NG_LOG?log_x[i]:ptr(object+0xe40u)[i];int32_t y=(int32_t)ptr(object+0xe8cu)[i];
  if(mode==NG_LOG)y=(int32_t)log_y[i];else if(y>0&&y<1023)y+=(int32_t)(((uint64_t)(uint32_t)(y*(1023-y))*1049602u)>>32);
  if(x>16383u||(i&&x<=previous_x)||y < -1024||y>1023||(i&&y<previous_y)||(i&&x-previous_x>4095u))valid=0;
  previous_x=x;previous_y=y;g->effective_x[i]=x;g->effective_y[i]=(uint32_t)y;ptr(obj+0xedcu)[i]=x;ptr(obj+0xf28u)[i]=(uint32_t)y;
 }
 if(!valid||g->effective_x[0]||g->effective_x[18]!=16383u){g->transaction_fault=g->fault=2;g->failures++;goto done;}
 g->calculator_return=(uint32_t)native(NG_CALCULATE,obj,0xaa000000u,0);
 valid=g->calculator_return==1;
 for(unsigned i=0;i<19;i++)valid&=ptr(obj+0xe40u)[i]==g->effective_x[i]&&ptr(obj+0xe8cu)[i]==g->effective_y[i];
 if(valid){for(unsigned i=0;i<NG_WORDS;i++)g->target[i]=(ptr(NG_DDR0)[i]&ng_mask(i))|(g->factory[i]&~ng_mask(i));
  /* Both native TC branches use the published function. Do not retain the
   * factory low-branch look remap on one branch of a technical Log mode.
   * Copy its complete assigned node/derivative fields, preserve opaque bits. */
  if(mode==NG_LOG)for(unsigned i=0;i<54;i++)g->target[i]=(g->target[i+54]&ng_mask(i))|(g->factory[i]&~ng_mask(i));
  g->writer_return=(uint32_t)native(NG_WRITER,0,(uint32_t)(uintptr_t)g->target,0);valid=!g->writer_return;}
 if(valid){for(unsigned i=0;i<NG_WORDS;i++)valid&=ptr(NG_DDR0)[i]==g->target[i];}
 g->bank1_same=hash_words(NG_DDR1,NG_WORDS)==g->bank1_hash;valid&=g->bank1_same;
 if(valid){g->applies++;g->applied_epoch=epoch;g->applied_object=object;g->target_hash=hash_words(NG_DDR0,NG_WORDS);g->fault=0;}
 else{
  g->failures++;g->transaction_fault=g->fault=3;int32_t rc=native(NG_WRITER,0,(uint32_t)(uintptr_t)g->factory,0);
  int restored=!rc;for(unsigned i=0;i<NG_WORDS;i++)restored&=ptr(NG_DDR0)[i]==g->factory[i];if(!restored)g->fault=4;
 }
 done:barrier();g->sequence++;release(&g->busy);return original;
}

__attribute__((used,visibility("default")))
int32_t nxl_vlog8_color(uint32_t object,uint32_t input,uint32_t extra){
 volatile struct ng_state*g=own_state();
 if(g->magic!=NG_MAGIC||g->schema!=NG_SCHEMA||g->cap!=ctx.cap||!g->parent_active||g->suspended||
    !vfw_ram(object,0xeb8u)||*ptr(object)!=NG_COLOR_VPTR||ptr(object)[3]!=g->active_parent||ptr(object)[2]!=g->active_context||
    registered_node(g->active_parent,NG_COLOR_VPTR)!=object)return native(NG_COLOR_CALCULATE,object,input,extra);
 count_one(&g->color_calls);
 if(!take(&g->busy,3)){g->transaction_fault=21;return native(NG_COLOR_CALCULATE,object,input,extra);}
 g->sequence++;barrier();g->color_input=input;
 uint32_t previous=ptr(object)[1];
 int changed=native(NG_PARAM_FIELD,object,input,0)!=native(NG_PARAM_FIELD,object,previous,0)||
             native(NG_PARAM_FIELD,object,input,1)!=native(NG_PARAM_FIELD,object,previous,1);
 int32_t rc=native(NG_COLOR_CALCULATE,object,input,extra);g->color_original_return=(uint32_t)rc;
 if(rc!=1){g->transaction_fault=22;g->color_failures++;goto done;}
 const unsigned mode=g->force_factory?NG_FACTORY:g->mode;
 if(mode!=NG_LOG){
  for(unsigned i=0;i<10;i++)g->color_factory[i]=g->color_target[i]=ptr(NG_COLOR_DDR0)[i];
  g->color_object=object;g->color_parent=g->active_parent;g->color_epoch=g->epoch;g->color_target_hash=hash_words(NG_COLOR_DDR0,10);goto done;
 }
 if(!changed){
  if(g->color_object!=object||g->color_parent!=g->active_parent||g->color_epoch!=g->epoch||!same_words(ptr(NG_COLOR_DDR0),g->color_target,10)){
   g->transaction_fault=23;g->color_failures++;goto done;
  }g->color_reuses++;goto done;
 }
 uint32_t source[10],target[10];
 for(unsigned i=0;i<10;i++)source[i]=ptr(NG_COLOR_DDR0)[i];
 if(!vlog8_compose(target,source)){g->transaction_fault=24;g->color_failures++;goto done;}
 uint32_t other=hash_words(NG_COLOR_DDR1,10);
 g->color_writer_return=(uint32_t)native(NG_COLOR_WRITER,0,(uint32_t)(uintptr_t)target,0);
 g->color_bank1_same=other==hash_words(NG_COLOR_DDR1,10);
 if(g->color_writer_return||!g->color_bank1_same||!same_words(ptr(NG_COLOR_DDR0),target,10)){
  g->transaction_fault=25;g->color_failures++;goto done;
 }
 for(unsigned i=0;i<10;i++){g->color_factory[i]=source[i];g->color_target[i]=target[i];}
 for(unsigned i=0;i<9;i++){
  g->color_source_low[i]=(uint32_t)(int32_t)*(volatile int16_t*)(uintptr_t)(object+0xe3cu+i*2u);
  g->color_source_high[i]=(uint32_t)(int32_t)*(volatile int16_t*)(uintptr_t)(object+0xe4eu+i*2u);
 }
 g->color_applies++;g->color_object=object;g->color_parent=g->active_parent;g->color_epoch=g->epoch;
 g->color_source_key=input;g->color_target_hash=hash_words(NG_COLOR_DDR0,10);
 done:barrier();g->sequence++;release(&g->busy);return rc;
}
static int maintain(uint32_t at,unsigned n,int code);
static int restore_slots(volatile struct ng_state*g){
 const uint32_t slots[4]={NG_GAMMA_SLOT,NG_PARENT_SLOT0,NG_PARENT_SLOT1,NG_COLOR_SLOT};
 const uint32_t stock[4]={NG_CALCULATE,NG_PARENT_UPDATE,NG_PARENT_UPDATE,NG_COLOR_CALCULATE};
 const unsigned order[4]={1,2,0,3};int ok=1;
 for(unsigned j=0;j<4;j++){unsigned i=order[j];*ptr(slots[i])=stock[i];barrier();ok&=maintain(slots[i]&~63u,64u,0);}
 for(unsigned i=0;i<4;i++)ok&=*ptr(slots[i])==stock[i];
 g->live_gamma_callback=*ptr(slots[0]);g->live_parent_callback0=*ptr(slots[1]);g->live_parent_callback1=*ptr(slots[2]);g->live_color_callback=*ptr(slots[3]);
 return ok;
}
__attribute__((used,visibility("default")))
int32_t nxl_gamma_parent(uint32_t parent,uint32_t inputs,uint32_t extra){
 volatile struct ng_state*g=own_state();
 int ours=g->magic==NG_MAGIC&&g->schema==NG_SCHEMA&&vfw_ram(parent,0x18u)&&
  (*ptr(parent)==NG_PARENT_VPTR0||*ptr(parent)==NG_PARENT_VPTR1)&&((ptr(parent)[3]>>8)&255u)==0;
 if(!ours)return native(NG_PARENT_UPDATE,parent,inputs,extra);
 count_one(&g->parent_calls);uint32_t object=registered_gamma(parent),color=0,context=0;
 /* The shared vtable also serves unrelated/empty parents. Their native
  * updates are passthrough, never a failure of this Gamma transaction. */
 if(!object){count_one(&g->parent_ignored);g->ignored_parent_last=parent;g->ignored_parent_input_raw=inputs;return native(NG_PARENT_UPDATE,parent,inputs,extra);}
 if(!take(&g->parent_active,1)){
  g->parent_reentrant++;g->transaction_fault=31;g->suspended++;barrier();
  int32_t rc=native(NG_PARENT_UPDATE,parent,inputs,extra);g->suspended--;barrier();return rc;
 }
 g->binding_reason=transaction_nodes(g,parent,inputs,object,&color,&context);
 if(g->binding_reason){g->fault=32;g->queued_epoch=0;g->video_epoch=0;release(&g->parent_active);return native(NG_PARENT_UPDATE,parent,inputs,extra);}
 g->active_parent=parent;g->active_context=context;g->transaction_fault=0;
 unsigned epoch=g->epoch;
 if(g->refresh_epoch!=epoch||g->refresh_object!=object||g->refresh_parent!=parent||
    g->applied_epoch!=epoch||g->target_hash!=hash_words(NG_DDR0,NG_WORDS)||
    g->color_epoch!=epoch||g->color_object!=color||g->color_target_hash!=hash_words(NG_COLOR_DDR0,10)){
  (void)native(NG_INVALIDATE,object,0,0);invalidate_color(color);
  g->invalidations++;g->refresh_epoch=epoch;g->refresh_object=object;g->refresh_parent=parent;
 }
 /* Exact original 0x803c595c decomposition: child updates, then native
  * parameter queue #28 with parent.bank!=0. This boundary permits a factory
  * rollback BEFORE a mixed colour/Gamma parameter bank can be queued. */
 (void)native(NG_UPDATE_CHILDREN,parent,context,extra);
 unsigned failed=g->transaction_fault;
 int matching=!g->busy&&g->applied_epoch==epoch&&g->applied_object==object&&
  g->color_epoch==epoch&&g->color_object==color&&same_words(ptr(NG_DDR0),g->target,NG_WORDS)&&same_words(ptr(NG_COLOR_DDR0),g->color_target,10);
 if(failed||!matching){
  if(!failed)failed=33;g->rollback_count++;g->force_factory=1;
  (void)native(NG_INVALIDATE,object,0,0);invalidate_color(color);
  (void)native(NG_UPDATE_CHILDREN,parent,context,extra);g->force_factory=0;
  int restored=!g->busy&&g->applied_epoch==epoch&&g->color_epoch==epoch&&
   same_words(ptr(NG_DDR0),g->factory,NG_WORDS)&&same_words(ptr(NG_COLOR_DDR0),g->color_factory,10);
  if(!restored){g->rollback_failed++;g->fault=34;g->queued_epoch=0;g->video_epoch=0;release(&g->parent_active);return -1;}
 }
 int32_t rc=native(NG_QUEUE,28,0,0);
 g->sequence++;barrier();g->queue_return=(uint32_t)rc;
 g->fault=failed;g->transaction_fault=failed;
 if(!rc&&!failed&&matching){
  g->queues++;g->queued_epoch=epoch;g->queued_object=object;
  for(unsigned i=0;i<49;i++)g->native_context[i]=ptr(context)[i];
  uint32_t driver=*ptr(VFW_DRIVER+8u);unsigned a=driver&255u,b=(driver>>8)&255u;
  if((a>=22u&&a<=25u)||(b>=22u&&b<=25u)){
   g->video_queues++;g->video_epoch=epoch;g->video_object=object;g->video_parent=parent;g->video_context=context;g->video_driver_modes=driver;
   g->video_hash=hash_words(NG_DDR0,NG_WORDS);g->video_color_epoch=epoch;g->video_color_object=color;g->video_color_hash=g->color_target_hash;
   for(unsigned i=0;i<8;i++)g->video_head[i]=ptr(object)[i];
   for(unsigned i=0;i<NG_WORDS;i++)g->video_target[i]=ptr(NG_DDR0)[i];
   for(unsigned i=0;i<10;i++){g->video_color_factory[i]=g->color_factory[i];g->video_color_target[i]=g->color_target[i];}
   for(unsigned i=0;i<49;i++)g->video_native_context[i]=ptr(context)[i];
  }
  if(g->detach_requested){g->detached=restore_slots(g);g->slots_installed=!g->detached;if(!g->detached)g->fault=6;}
 }else{g->queue_without_apply++;g->queued_epoch=0;g->video_epoch=0;if(failed){g->applied_epoch=0;g->color_epoch=0;}}
 barrier();g->sequence++;g->active_parent=g->active_context=0;release(&g->parent_active);return rc;
}
static int maintain(uint32_t at,unsigned n,int code){int32_t rc=native(VFW_CACHE,VFW_ALLOCATOR,at,n);barrier();if(rc>=0&&code)rc=native(0x800245b4u,at,n,1);barrier();return rc>=0;}
__attribute__((used,visibility("default")))
int32_t nxl_gamma_control(uint32_t cap,uint32_t mode,uint32_t nonce){
 if(!cap_ok(cap)||mode>NG_OFF||!nonce||!mappings())return -10;
 volatile struct ng_state*g=state_at(cap);uint32_t owner[4];if(!ng_owner(cap,owner))return -11;
 uint32_t offset_compute=(uint32_t)(uintptr_t)nxl_gamma_compute-ctx.cap,offset_parent=(uint32_t)(uintptr_t)nxl_gamma_parent-ctx.cap,offset_color=(uint32_t)(uintptr_t)nxl_vlog8_color-ctx.cap;
 if(offset_compute>=0x4000u||offset_parent>=0x4000u||offset_color>=0x4000u)return -12;
 const uint32_t slots[4]={NG_GAMMA_SLOT,NG_PARENT_SLOT0,NG_PARENT_SLOT1,NG_COLOR_SLOT};const uint32_t stock[4]={NG_CALCULATE,NG_PARENT_UPDATE,NG_PARENT_UPDATE,NG_COLOR_CALCULATE};
 const uint32_t hook[4]={cap+offset_compute,cap+offset_parent,cap+offset_parent,cap+offset_color};
 if(!g->magic){
  for(unsigned i=0;i<4;i++)if(*ptr(slots[i])!=stock[i])return -13;
  memset((void*)g,0,sizeof*g);g->magic=NG_MAGIC;g->schema=NG_SCHEMA;g->cap=cap;g->bytes=VFW_CAP_BYTES;g->nonce=nonce;g->code_hash=hash_words(cap,0x1000u);g->profile=VLOG8_PROFILE;g->scene_white_q16=VLOG8_SCENE_WHITE_Q16;g->native_black=VLOG8_NATIVE_BLACK;g->native_white=VLOG8_NATIVE_WHITE;for(unsigned i=0;i<4;i++)g->owner[i]=owner[i];
 }
 if(g->magic!=NG_MAGIC||g->schema!=NG_SCHEMA||g->cap!=cap||g->bytes!=VFW_CAP_BYTES||g->code_hash!=hash_words(ctx.cap,0x1000u)||g->code_hash!=hash_words(cap,0x1000u))return -14;
 for(unsigned i=0;i<4;i++)if(g->owner[i]!=owner[i])return -15;
 for(unsigned i=0;i<4;i++)if(*ptr(slots[i])!=stock[i]&&*ptr(slots[i])!=hook[i])return -16;
 if(!take(&g->busy,2))return -17;
 if(g->parent_active){release(&g->busy);return -19;}
 g->sequence++;barrier();g->mode=mode==NG_OFF?NG_FACTORY:mode;g->detach_requested=mode==NG_OFF;g->detached=0;g->fault=0;g->epoch++;g->queued_epoch=0;g->video_epoch=0;g->video_color_epoch=0;g->video_queues=0;g->applied_epoch=0;g->color_epoch=0;g->transaction_fault=0;
 int ok=maintain(cap,0x4000u,1);
 if(ok){
  /* All code/data are prepared before publishing even the first callback. */
  g->ever_published=1;
  barrier();const unsigned order[4]={3,0,1,2};for(unsigned j=0;j<4;j++){unsigned i=order[j];*ptr(slots[i])=hook[i];barrier();ok&=maintain(slots[i]&~63u,64,0);}
  for(unsigned i=0;i<4;i++)ok&=*ptr(slots[i])==hook[i];
 }
 if(!ok){
  g->mode=NG_FACTORY;for(unsigned i=0;i<4;i++){if(*ptr(slots[i])==hook[i])*ptr(slots[i])=stock[i];barrier();(void)maintain(slots[i]&~63u,64,0);}g->fault=5;
 }
 g->slots_installed=ok;g->live_gamma_callback=*ptr(slots[0]);g->live_parent_callback0=*ptr(slots[1]);g->live_parent_callback1=*ptr(slots[2]);g->live_color_callback=*ptr(slots[3]);barrier();g->sequence++;release(&g->busy);return ok?0:-18;
}
static int read_ram(void*c,uint32_t at,void*out,unsigned n){(void)c;if(!n||n>sizeof(struct vfw_snapshot)||(at&3u)||(n&3u)||at<0x80000000u||(uint64_t)at+n>0x94000000ULL)return 0;uint32_t*b=out;for(unsigned i=0;i<n/4;i++)b[i]=ptr(at)[i];barrier();return 1;}
static void publish(struct vfw_reply*r){r->magic=VFW_MAGIC;r->schema=VFW_SCHEMA;r->nonce=ctx.nonce;r->hash=vfw_hash(r,offsetof(struct vfw_reply,hash));volatile uint32_t*out=ptr(ctx.cap+VFW_REPLY);out[63]=0;barrier();for(unsigned i=0;i<63;i++)out[i]=((uint32_t*)r)[i];barrier();out[63]=VFW_COMMIT_XOR^r->hash^ctx.nonce;barrier();}
__attribute__((used,visibility("default"),section(".text.entry")))
int32_t nxl_video_entry(uint32_t packet){
 volatile uint32_t*p=ptr(packet);if(!packet||p[1]!=12||!p[4])return native(VFW_ORIGINAL,packet,0,0);volatile uint32_t*payload=ptr(p[4]);
 if(payload[0]!=ctx.nonce||payload[1]!=ctx.cap+VFW_CONTEXT||payload[2]!=ctx.operation)return native(VFW_ORIGINAL,packet,0,0);
 if(ctx.magic!=VFW_MAGIC||ctx.schema!=VFW_SCHEMA||ctx.bytes!=VFW_CAP_BYTES||ctx.operation!=VFW_TRACE||!cap_ok(ctx.cap)||*ptr(VFW_SLOT)!=ctx.cap||ptr(VFW_SLOT)[1])return -1;
 *ptr(VFW_SLOT)=VFW_ORIGINAL;barrier();struct vfw_reply result;memset(&result,0,sizeof result);result.operation=ctx.operation;result.slot_restored=1;
 if(!maintain(VFW_SLOT&~4095u,4096u,0)){result.status=VFW_CACHE_FAILED;publish(&result);return -1;}
 result.status=VFW_WAITING;result.phase=1;publish(&result);unsigned spins=0;
 while(*ptr(ctx.cap+VFW_CONTROL)!=(ctx.nonce^VFW_GO_XOR)){if(ptr(ctx.cap+VFW_CONTROL)[1]==~ctx.nonce||++spins>=50000000u){result.status=VFW_ABORTED;result.phase=2;publish(&result);return -1;}}
 struct vfw_snapshot current;struct vfw_ipc_route route;
 if(!vfw_capture_route(read_ram,NULL,&current,&route)||current.root!=ctx.root||current.profile6!=ctx.profile6||current.profile19!=ctx.profile19||vfw_movie_active(&current)){result.status=VFW_CHANGED;result.phase=2;publish(&result);return -1;}
 result.before=result.after=vfw_route_mode(&current,&route);result.target_mode=result.before==VFW_FULL?23u:25u;result.native_power_scene=result.before==VFW_FULL?8u:9u;
 uint32_t cap=ctx.reserved[0]?ctx.reserved[0]:ctx.cap;int32_t rc=nxl_gamma_control(cap,ctx.reserved[1],ctx.nonce);result.reserved[0]=(uint32_t)rc;result.reserved[1]=cap;result.reserved[2]=state_at(cap)->epoch;result.reserved[3]=state_at(cap)->ever_published;
 result.writes=rc?0u:4u;result.status=rc?VFW_VERIFY_FAILED:0;result.phase=2;publish(&result);return rc;
}
