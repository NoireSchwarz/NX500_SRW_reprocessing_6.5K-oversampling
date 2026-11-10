/* SPDX-License-Identifier: AGPL-3.0-only.
 * Requires existing P43 ctx.cap/nonce,ptr(),native(),barrier(),P39_TASK.
 * Prepare/apply ONLY after successful801d9f84 and verified configured state2.
 * The native setter itself reads20810710 for index1; no new MMIO access is
 * performed by this module. It submits only index1, a strictly pinned DDR
 * shadow. Index3 (physical20811f00) is never used.
 */
#define P49_PD_CONTRACT_FN __attribute__((noinline,section(".text.param")))
#include "cc_contract.h"
#include "param_dma_contract.h"
#define P48_CC_FN __attribute__((noinline,section(".text.cc")))
#define P49_PD_FN __attribute__((noinline,unused,section(".text.param")))
static volatile uint32_t *cc(void){return ptr(ctx.cap+P48_CC_OFFSET);}
static volatile struct p48_cc_snapshot *cc_stage(unsigned stage){
 return (volatile struct p48_cc_snapshot*)(cc()+P48_CC_SNAPSHOT_START+stage*P48_CC_SNAPSHOT_WORDS);
}
static int cc_words_equal(const volatile uint32_t *a,const volatile uint32_t *b,unsigned n){for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static void cc_read_roots(volatile uint32_t *out){for(unsigned i=0;i<13;i++)out[i]=*ptr(P48_CC_ROOT+4u*i);}
static int cc_roots_target(const volatile uint32_t *r){uint32_t a[13];for(unsigned i=0;i<13;i++)a[i]=r[i];return p48_cc_target_roots(a);}
static int cc_roots_observer(const volatile uint32_t *r){uint32_t a[13];for(unsigned i=0;i<13;i++)a[i]=r[i];return p48_cc_observer_roots(a);}
static int cc_configured(void){return *ptr(P39_TASK)==0x80592640u&&(*ptr(P39_TASK+3364u)&255u)==2u&&*ptr(P39_TASK+3388u)==0x04000001u;}
static int cc_request(void){uint32_t a[64];for(unsigned i=0;i<64;i++)a[i]=cc()[i];return p48_cc_request_valid(a,ctx.nonce);}
static void cc_busy(unsigned value){cc()[71]=value?1u:0u;barrier();}

/* Snapshot0/1 statuses do not stand in for task adoption. During processing
 * snapshots are observations only: a mismatch must not initiate a rewrite. */
static P48_CC_FN int cc_snapshot(unsigned stage){
 if(stage>=P48_CC_SNAPSHOTS)return 0;
 volatile uint32_t *w=cc();volatile struct p48_cc_snapshot *s=cc_stage(stage);
 volatile uint32_t *clear=(volatile uint32_t*)s;for(unsigned i=0;i<P48_CC_SNAPSHOT_WORDS;i++)clear[i]=0;
 cc_read_roots(s->roots_before);
 if(!cc_roots_target(s->roots_before)){w[73]=P48_CC_BAD_ROOTS;w[78]|=1u<<stage;barrier();return 0;}
 int observe0=cc_roots_observer(s->roots_before);
 for(unsigned pass=0;pass<2;pass++){
  volatile uint32_t *b1=pass?s->bank1_second:s->bank1_first;
  for(unsigned i=0;i<10;i++)b1[i]=*ptr(P48_CC_TARGET1+4u*i);
  if(observe0){volatile uint32_t *b0=pass?s->bank0_second:s->bank0_first;for(unsigned i=0;i<10;i++)b0[i]=*ptr(P48_CC_TARGET0+4u*i);}
 }
 cc_read_roots(s->roots_after);s->roots_valid=cc_roots_target(s->roots_after)?1u:0u;
 s->roots_equal=cc_words_equal(s->roots_before,s->roots_after,13);
 s->bank1_valid=s->roots_valid;s->bank1_equal=cc_words_equal(s->bank1_first,s->bank1_second,10);
 s->bank0_valid=observe0&&cc_roots_observer(s->roots_after);s->bank0_equal=s->bank0_valid&&cc_words_equal(s->bank0_first,s->bank0_second,10);
 s->target_equal=s->bank1_valid&&s->bank1_equal&&cc_words_equal(s->bank1_second,w+P48_CC_TARGET_WORD,10);
 s->valid=s->roots_valid&&s->bank1_equal;w[78]|=1u<<stage;barrier();return s->valid?1:0;
}
static P48_CC_FN int cc_prepare_fixed(const volatile uint32_t *fixed){
 volatile uint32_t *w=cc();w[74]=P48_CC_MAGIC;
 if(!cc_request()){w[73]=P48_CC_BAD_REQUEST;barrier();return 0;}
 if(!cc_configured()||w[71]||w[68]||w[75]){w[73]=P48_CC_BAD_PHASE;barrier();return 0;}
 if(!cc_snapshot(P48_CC_TEMPLATE)){w[73]=P48_CC_BAD_ROOTS;return 0;}
 volatile struct p48_cc_snapshot *s=cc_stage(P48_CC_TEMPLATE);
 if(!s->roots_equal){w[73]=P48_CC_TEMPLATE_RACED;barrier();return 0;}
 uint32_t original[10],target[10];int32_t fields[18];
 for(unsigned i=0;i<10;i++)original[i]=fixed?fixed[i]:s->bank1_second[i];
 for(unsigned i=0;i<18;i++)fields[i]=(int32_t)w[9+i];
 if(w[3])p48_cc_patch(target,original,fields);else for(unsigned i=0;i<10;i++)target[i]=original[i];
 if(!p48_cc_unassigned_preserved(original,target)){w[73]=P48_CC_BAD_REQUEST;barrier();return 0;}
 unsigned changed=0;for(unsigned i=0;i<10;i++){w[P48_CC_TEMPLATE_WORD+i]=original[i];w[P48_CC_TARGET_WORD+i]=target[i];changed|=original[i]!=target[i];}
 w[79]=changed?1u:0u;w[65]=1;w[75]=1;w[64]=1;barrier();return 1;
}
static __attribute__((unused)) int cc_prepare(void){return cc_prepare_fixed((const volatile uint32_t*)0);}
static P48_CC_FN int cc_apply(void){
 volatile uint32_t *w=cc();
 if(!w[75]||!cc_request()){w[73]=P48_CC_BAD_REQUEST;barrier();return 0;}
 if(!cc_configured()||w[71]||w[68]){w[73]=P48_CC_BAD_PHASE;barrier();return 0;}
 if(!w[65]){w[73]=P48_CC_BAD_REQUEST;barrier();return 0;}
 uint32_t roots[13];for(unsigned i=0;i<13;i++)roots[i]=*ptr(P48_CC_ROOT+4u*i);
 if(!p48_cc_target_roots(roots)){w[73]=P48_CC_BAD_ROOTS;barrier();return 0;}
 w[68]=1;w[64]=2;barrier();
 w[67]=(uint32_t)native(P48_CC_ENTRY,P48_CC_BANK,(uint32_t)(uintptr_t)(w+P48_CC_TARGET_WORD),0);
 int snap=cc_snapshot(P48_CC_POSTSETTER);w[76]=snap&&cc_stage(P48_CC_POSTSETTER)->target_equal;
 volatile struct p48_cc_snapshot *before=cc_stage(P48_CC_TEMPLATE),*after=cc_stage(P48_CC_POSTSETTER);
 w[77]=!before->bank0_valid?0u:(after->bank0_valid&&after->bank0_equal&&cc_words_equal(before->bank0_second,after->bank0_second,10));
 if(w[67]){w[73]=P48_CC_SETTER_FAILED;barrier();return 0;}
 if(!w[76]){w[73]=P48_CC_TARGET_MISMATCH;barrier();return 0;}
 if(before->bank0_valid&&!w[77]){w[73]=P48_CC_BANK0_CHANGED;barrier();return 0;}
 barrier();return 1;
}
static P48_CC_FN int cc_restore(void){
 volatile uint32_t *w=cc();
 if(!w[68]){if(w[75])cc_snapshot(P48_CC_RESTORED);return 1;}
 /* Cleanup may call this again after native cleanup has closed clocks. A
  * completed restoration is idempotent and never re-enters the setter. */
 if(w[70])return !w[69]&&w[66]?1:0;
 if(w[71]){w[72]=1;w[73]=P48_CC_RESTORE_WHILE_BUSY;barrier();return 0;}
 if(!w[65]){w[73]=P48_CC_RESTORE_FAILED;barrier();return 0;}
 uint32_t roots[13];for(unsigned i=0;i<13;i++)roots[i]=*ptr(P48_CC_ROOT+4u*i);
 if(!p48_cc_target_roots(roots)){w[73]=P48_CC_BAD_ROOTS;barrier();return 0;}
 w[70]=1;barrier();w[69]=(uint32_t)native(P48_CC_ENTRY,P48_CC_BANK,(uint32_t)(uintptr_t)(w+P48_CC_TEMPLATE_WORD),0);
 int snap=cc_snapshot(P48_CC_RESTORED);
 w[66]=snap&&cc_words_equal(cc_stage(P48_CC_RESTORED)->bank1_second,w+P48_CC_TEMPLATE_WORD,10);
 if(w[69]||!w[66]){w[73]=P48_CC_RESTORE_FAILED;barrier();return 0;}
 w[64]=3;barrier();return 1;
}

/* P49 fresh parameter-load completion. Arm before the original config, so
 * a late config completion cannot certify the later CC reload. No shared
 * event object, new RTOS task, direct MMIO write or fixed-delay completion.
 */
static volatile uint32_t *cc_dma(void){return ptr(ctx.cap+P49_PD_OFFSET);}
static P48_CC_FN int cc_dma_error(unsigned error){cc_dma()[P49_PD_W_ERROR]=error;barrier();return 0;}
static P48_CC_FN int cc_dma_roots(void){
 uint32_t roots[13];for(unsigned i=0;i<13;i++)roots[i]=*ptr(P48_CC_ROOT+4u*i);
 return p48_cc_target_roots(roots)&&p48_cc_observer_roots(roots);
}
static P48_CC_FN int cc_dma_configured_idle(void){
 uint32_t normal=*ptr(P39_TASK+3428u),active=*ptr(P39_TASK+3436u);
 return cc_configured()&&!(*ptr(P39_TASK+3204u)|*ptr(P39_TASK+3208u)|
  *ptr(P39_TASK+3284u)|*ptr(P39_TASK+3288u))&&normal==active&&
  normal>=0x81000000u&&normal<0x93fff000u&&!(normal&3u)&&*ptr(normal)==0x805926c8u;
}
static P48_CC_FN int cc_dma_owner(uint32_t owner){
 return owner>=0x81000000u&&owner<0x93fff000u&&!(owner&3u)&&
  (*(volatile unsigned char*)owner)==1u;
}
static P48_CC_FN int cc_dma_visible(void){
 volatile uint32_t *d=cc_dma();uint32_t pte=*ptr(P49_PD_PTE_ADDRESS);barrier();
 if(!p49_pd_parameter_uncached(pte)||pte!=*ptr(P49_PD_PTE_ADDRESS))return 0;
 if(d[P49_PD_W_PTE_BEFORE]&&d[P49_PD_W_PTE_BEFORE]!=pte)return 0;
 d[P49_PD_W_PTE_BEFORE]=pte;d[P49_PD_W_VISIBILITY]=1;barrier();return 1;
}
__attribute__((used,visibility("hidden"),section(".text.cc")))
void cc_dma_irq_body(uint32_t entry_sp,uint32_t return_lr){
 volatile uint32_t *d=cc_dma();uint32_t bit=UINT32_MAX,pending=0;
 if(entry_sp>=0x81000000u&&entry_sp<0x93fff000u&&!(entry_sp&7u)){
  bit=*ptr(entry_sp);pending=*ptr(entry_sp+4u);
 }
 d[P49_PD_W_IRQ_SP]=entry_sp;d[P49_PD_W_IRQ_LR]=return_lr;
 d[P49_PD_W_IRQ_BIT]=bit;d[P49_PD_W_IRQ_PENDING]=pending;
 if(d[0]!=P49_PD_MAGIC||d[1]!=P49_PD_SCHEMA||d[2]!=ctx.nonce||!d[P49_PD_W_ACTIVE]||
  !d[P49_PD_W_INFLIGHT]||!p49_pd_irq_boundary(entry_sp,return_lr,bit,pending)){
  d[P49_PD_W_FOREIGN_IRQ]++;d[P49_PD_W_ERROR]=P49_PD_FOREIGN_IRQ;barrier();return;
 }
 unsigned gen=d[P49_PD_W_ARMED_GENERATION],count=d[P49_PD_W_IRQ_COUNT];
 if(gen==1u&&d[P49_PD_W_PHASE]==P49_PD_ARMED_CONFIG&&!count){
  d[P49_PD_W_CONFIG_DONE]=1;d[P49_PD_W_SEEN_GENERATION]=1;
  d[P49_PD_W_PHASE]=P49_PD_CONFIG_DONE;barrier();d[P49_PD_W_IRQ_COUNT]=1;
 }else if(gen==2u&&d[P49_PD_W_PHASE]==P49_PD_ARMED_RELOAD&&count==1u&&d[P49_PD_W_CONFIG_DONE]){
  d[P49_PD_W_RELOAD_DONE]=1;d[P49_PD_W_SEEN_GENERATION]=2;
  d[P49_PD_W_PHASE]=P49_PD_RELOAD_DONE;barrier();d[P49_PD_W_IRQ_COUNT]=2;
 }else{d[P49_PD_W_FOREIGN_IRQ]++;d[P49_PD_W_ERROR]=P49_PD_FOREIGN_IRQ;}
 barrier();
}
__attribute__((used,naked,visibility("hidden"),section(".text.cc")))
void cc_dma_irq(void){__asm__ volatile("mov r0, sp\n\tmov r1, lr\n\tb cc_dma_irq_body");}
/* Call after the verified original stop, before native801d9f84 config. */
static P49_PD_FN int cc_dma_arm_config(void){
 volatile uint32_t *d=cc_dma();
 if(d[P49_PD_W_ACTIVE]||d[P49_PD_W_INFLIGHT]||cc()[71]||!idle())return cc_dma_error(P49_PD_BAD_PHASE);
 for(unsigned i=0;i<64;i++)d[i]=0;
 d[0]=P49_PD_MAGIC;d[1]=P49_PD_SCHEMA;d[2]=ctx.nonce;
 /* event67 has no bank/generation. Original stop ACK has not been shown
  * to drain this separate channel; do not activate the experiment. */
 if(!P49_PD_INITIAL_DRAIN_PROVEN)return cc_dma_error(P49_PD_INITIAL_DRAIN_UNPROVEN);
 d[P49_PD_W_BASE]=P48_CC_FIXED_BASE1;d[P49_PD_W_SOURCE]=P49_PD_SOURCE;
 d[P49_PD_W_SIZE]=P49_PD_SIZE;d[P49_PD_W_BANK]=1;
 if(!p39_stop_record_valid((const uint32_t*)(ctx.cap+P48_INITIAL_STOP_OFFSET),ctx.nonce,ctx.cap)||!cc_dma_roots())return cc_dma_error(P49_PD_BAD_ROOTS);
 if(!cc_dma_visible())return cc_dma_error(P49_PD_VISIBILITY_UNPROVEN);
 d[P49_PD_W_SAVED_CB]=*ptr(P49_PD_SLOT);d[P49_PD_W_SAVED_ENABLE]=*ptr(P49_PD_ENABLE);
 d[P49_PD_W_SAVED_PENDING]=*ptr(P49_PD_PENDING);d[P49_PD_W_SAVED_COMMAND]=*ptr(P49_PD_COMMAND);
 if(d[P49_PD_W_SAVED_CB]||(d[P49_PD_W_SAVED_ENABLE]&P49_PD_BIT))return cc_dma_error(P49_PD_FOREIGN_HANDLER);
 if((d[P49_PD_W_SAVED_PENDING]&P49_PD_BIT)||(d[P49_PD_W_SAVED_COMMAND]&16u))return cc_dma_error(P49_PD_STALE_PENDING);
 d[P49_PD_W_CALLBACK]=(uint32_t)(uintptr_t)cc_dma_irq;
 d[P49_PD_W_REGISTER_STATUS]=(uint32_t)native(P49_PD_REGISTER,P49_PD_EVENT,d[P49_PD_W_CALLBACK],0);
 if(*ptr(P49_PD_SLOT)==d[P49_PD_W_CALLBACK])d[P49_PD_W_ACTIVE]=1;
 if(d[P49_PD_W_REGISTER_STATUS]||*ptr(P49_PD_SLOT)!=d[P49_PD_W_CALLBACK]||!(*ptr(P49_PD_ENABLE)&P49_PD_BIT))return cc_dma_error(P49_PD_REGISTER_FAILED);
 d[P49_PD_W_ACTIVE]=1;d[P49_PD_W_INFLIGHT]=1;d[P49_PD_W_ARMED_GENERATION]=1;
 cc_busy(1);d[P49_PD_W_PHASE]=P49_PD_ARMED_CONFIG;barrier();return 1;
}
static P49_PD_FN int cc_dma_wait(unsigned generation){
 volatile uint32_t *d=cc_dma();unsigned spins=0,want=generation==1u?P49_PD_W_CONFIG_DONE:P49_PD_W_RELOAD_DONE;
 while(!d[want]){
  if(d[P49_PD_W_ERROR]||d[P49_PD_W_FOREIGN_IRQ])return 0;
  if(++spins>=P49_PD_MAX_POLLS){d[generation==1u?P49_PD_W_CONFIG_POLLS:P49_PD_W_RELOAD_POLLS]=spins;
   return cc_dma_error(generation==1u?P49_PD_CONFIG_TIMEOUT:P49_PD_RELOAD_TIMEOUT);}
 }
 d[generation==1u?P49_PD_W_CONFIG_POLLS:P49_PD_W_RELOAD_POLLS]=spins;
 if(d[P49_PD_W_ERROR]||d[P49_PD_W_FOREIGN_IRQ]||d[P49_PD_W_IRQ_COUNT]!=generation||
  d[P49_PD_W_ARMED_GENERATION]!=generation||d[P49_PD_W_SEEN_GENERATION]!=generation)return cc_dma_error(P49_PD_FOREIGN_IRQ);
 d[P49_PD_W_INFLIGHT]=0;cc_busy(0);barrier();return 1;
}
/* Call immediately after successful config, before preparing/applying CC. */
static P49_PD_FN int cc_dma_wait_config(void){
 volatile uint32_t *d=cc_dma();
 if(!d[P49_PD_W_ACTIVE]||d[P49_PD_W_ARMED_GENERATION]!=1u)return cc_dma_error(P49_PD_BAD_PHASE);
 if(!cc_dma_wait(1u))return 0;
 uint32_t owner=*ptr(P49_PD_OWNER_ROOT);d[P49_PD_W_OWNER]=owner;
 d[P49_PD_W_CONFIG_SOURCE]=*ptr(0x20810340u);d[P49_PD_W_CONFIG_SIZE]=*ptr(0x20810344u);d[P49_PD_W_CONFIG_BANK]=*ptr(0x20810348u);
 if(!cc_dma_configured_idle()||!cc_dma_owner(owner)||!cc_dma_roots())return cc_dma_error(P49_PD_BAD_ROOTS);
 if(d[P49_PD_W_CONFIG_SOURCE]!=P49_PD_SOURCE||d[P49_PD_W_CONFIG_SIZE]!=P49_PD_SIZE||d[P49_PD_W_CONFIG_BANK]!=1u)return cc_dma_error(P49_PD_SOURCE_CHANGED);
 return 1;
}
static P49_PD_FN int cc_dma_cleanup(void){
 volatile uint32_t *d=cc_dma();
 if(!d[P49_PD_W_ACTIVE])return d[P49_PD_W_ERROR]?0:1;
 if(d[P49_PD_W_INFLIGHT]||cc()[71])return cc_dma_error(P49_PD_BAD_PHASE);
 if(*ptr(P49_PD_SLOT)!=d[P49_PD_W_CALLBACK]){cc_busy(1);return cc_dma_error(P49_PD_FOREIGN_HANDLER);}
 if((*ptr(P49_PD_PENDING)&P49_PD_BIT)||(*ptr(P49_PD_COMMAND)&16u)){
  cc_busy(1);return cc_dma_error(P49_PD_BAD_PHASE);
 }
 d[P49_PD_W_DISABLE_STATUS]=(uint32_t)native(P49_PD_DISABLE,P49_PD_EVENT,0,0);
 d[P49_PD_W_SLOT_AFTER]=*ptr(P49_PD_SLOT);d[P49_PD_W_ENABLE_AFTER]=*ptr(P49_PD_ENABLE);d[P49_PD_W_PENDING_AFTER]=*ptr(P49_PD_PENDING);
 if(d[P49_PD_W_DISABLE_STATUS]||d[P49_PD_W_SLOT_AFTER]||(d[P49_PD_W_ENABLE_AFTER]&P49_PD_BIT)||(d[P49_PD_W_PENDING_AFTER]&P49_PD_BIT)){
  cc_busy(1);return cc_dma_error(P49_PD_CLEANUP_FAILED);
 }
 d[P49_PD_W_ACTIVE]=0;barrier();return 1;
}
/* After cc_prepare_fixed: perform setter, then fresh OEM whole-bank reload.
 * Place this BEFORE final WB submission, since reloading the whole native
 * parameter bank can overwrite other hardware parameters. */
static P49_PD_FN int cc_commit(void){
 volatile uint32_t *d=cc_dma();
 if(!d[P49_PD_W_ACTIVE]||d[P49_PD_W_PHASE]!=P49_PD_CONFIG_DONE||d[P49_PD_W_INFLIGHT]||
  d[P49_PD_W_ERROR]||d[P49_PD_W_IRQ_COUNT]!=1u||!cc_dma_configured_idle())return cc_dma_error(P49_PD_BAD_PHASE);
 if(!cc_dma_roots()||!cc_dma_owner(d[P49_PD_W_OWNER])||*ptr(P49_PD_OWNER_ROOT)!=d[P49_PD_W_OWNER])return cc_dma_error(P49_PD_BAD_ROOTS);
 if(!cc_dma_visible())return cc_dma_error(P49_PD_VISIBILITY_UNPROVEN);
 if(!cc_apply())return cc_dma_error(P49_PD_SETTER_FAILED);
 d[P49_PD_W_PHASE]=P49_PD_CC_WRITTEN;
 if(!cc_dma_visible()||*ptr(P49_PD_SLOT)!=d[P49_PD_W_CALLBACK]||
  !(*ptr(P49_PD_ENABLE)&P49_PD_BIT)||(*ptr(P49_PD_PENDING)&P49_PD_BIT))return cc_dma_error(P49_PD_FOREIGN_HANDLER);
 d[P49_PD_W_INFLIGHT]=1;d[P49_PD_W_ARMED_GENERATION]=2;cc_busy(1);
 d[P49_PD_W_PHASE]=P49_PD_ARMED_RELOAD;barrier();
 d[P49_PD_W_LOAD_STATUS]=(uint32_t)native(P49_PD_LOADER,d[P49_PD_W_OWNER],0,0);
 if(d[P49_PD_W_LOAD_STATUS])return cc_dma_error(P49_PD_LOADER_FAILED);
 if(!cc_dma_wait(2u))return 0;
 d[P49_PD_W_DMA_SOURCE]=*ptr(0x20810340u);d[P49_PD_W_DMA_SIZE]=*ptr(0x20810344u);d[P49_PD_W_DMA_BANK]=*ptr(0x20810348u);
 d[P49_PD_W_COMMAND_AFTER]=*ptr(P49_PD_COMMAND);d[P49_PD_W_MODE_AFTER]=*ptr(0x20810308u);
 d[P49_PD_W_PTE_AFTER]=*ptr(P49_PD_PTE_ADDRESS);d[P49_PD_W_ROOTS_EQUAL]=cc_dma_roots()?1u:0u;
 uint32_t same=1;for(unsigned i=0;i<10;i++)if(*ptr(P48_CC_TARGET1+4u*i)!=cc()[P48_CC_TARGET_WORD+i])same=0;
 d[P49_PD_W_DDR_TARGET_EQUAL]=same;
 if(d[P49_PD_W_DMA_SOURCE]!=P49_PD_SOURCE||d[P49_PD_W_DMA_SIZE]!=P49_PD_SIZE||d[P49_PD_W_DMA_BANK]!=1u||
  !same||!d[P49_PD_W_ROOTS_EQUAL]||d[P49_PD_W_PTE_AFTER]!=d[P49_PD_W_PTE_BEFORE])return cc_dma_error(P49_PD_SOURCE_CHANGED);
 if(!cc_dma_cleanup())return 0;d[P49_PD_W_PHASE]=P49_PD_CLOSED;barrier();
 uint32_t copy[64];for(unsigned i=0;i<64;i++)copy[i]=d[i];return p49_pd_audit_valid(copy,ctx.nonce);
}
/* Default first-demo profile. Native config retains its factory CC; this
 * function performs no callback registration, loader call or MMIO write. */
static P49_PD_FN int p49_pd_record_factory(void){
 volatile uint32_t *d=cc_dma();
 if(d[P49_PD_W_ACTIVE]||d[P49_PD_W_INFLIGHT]||cc()[71])return cc_dma_error(P49_PD_BAD_PHASE);
 for(unsigned i=0;i<64;i++)d[i]=0;
 d[0]=P49_PD_MAGIC;d[1]=P49_PD_SCHEMA;d[2]=ctx.nonce;
 d[P49_PD_W_PHASE]=P49_PD_SKIPPED_FACTORY_CC;
 d[P49_PD_W_PROFILE_MODE]=P49_PD_MODE_FACTORY_CC;
 if(!cc_request()||cc()[3]||!cc_configured()||!cc_dma_roots())return cc_dma_error(P49_PD_BAD_PHASE);
 barrier();uint32_t copy[64];for(unsigned i=0;i<64;i++)copy[i]=d[i];
 return p49_pd_factory_audit_valid(copy,ctx.nonce);
}
