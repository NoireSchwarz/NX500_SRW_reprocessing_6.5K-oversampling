/* SPDX-License-Identifier: AGPL-3.0-only.
 * P49 SRAM parameter DMA completion: OEM event67, IRQ44 CS1 done bit3.
 * Experimental reload needs an independently proven initial channel drain.
 * Factory fallback records explicitly contain no synthetic DMA completion.
 */
#ifndef P49_PARAM_DMA_CONTRACT_H
#define P49_PARAM_DMA_CONTRACT_H
#include <stdint.h>
#ifndef P49_PD_CONTRACT_FN
#define P49_PD_CONTRACT_FN inline
#endif
#define P49_PD_OFFSET 0xbc00u
#define P49_PD_BYTES 256u
#define P49_PD_MAGIC 0x444d4139u
#define P49_PD_SCHEMA 1u
#define P49_PD_EVENT 67u
#define P49_PD_BIT 8u
#define P49_PD_SLOT 0x813f09c0u
#define P49_PD_PENDING 0x20810418u
#define P49_PD_ENABLE 0x20810468u
#define P49_PD_COMMAND 0x20810304u
#define P49_PD_REGISTER 0x8017c59cu
#define P49_PD_DISABLE 0x8017c5e4u
#define P49_PD_LOADER 0x8021b314u
#define P49_PD_ISR_RETURN 0x80036c28u
#define P49_PD_OWNER_ROOT 0x81543d40u
#define P49_PD_SOURCE 0xbb71e000u
#define P49_PD_SIZE 0x1700u
#define P49_PD_PTE_ADDRESS 0x81002edcu
#define P49_PD_MAX_POLLS 50000000u
#define P49_PD_INITIAL_DRAIN_PROVEN 0u
#define P49_PD_MODE_FACTORY_CC 2u
#define P49_PD_MODE_SINGLE_CONFIG_IQ 3u
enum p49_pd_phase {P49_PD_INITIAL=0,P49_PD_ARMED_CONFIG=1,P49_PD_CONFIG_DONE=2,
 P49_PD_CC_WRITTEN=3,P49_PD_ARMED_RELOAD=4,P49_PD_RELOAD_DONE=5,P49_PD_CLOSED=6,
 P49_PD_SKIPPED_FACTORY_CC=7};
enum p49_pd_error {P49_PD_OK=0,P49_PD_BAD_PHASE=1,P49_PD_BAD_ROOTS=2,
 P49_PD_VISIBILITY_UNPROVEN=3,P49_PD_FOREIGN_HANDLER=4,P49_PD_STALE_PENDING=5,
 P49_PD_REGISTER_FAILED=6,P49_PD_CONFIG_TIMEOUT=7,P49_PD_FOREIGN_IRQ=8,
 P49_PD_SETTER_FAILED=9,P49_PD_LOADER_FAILED=10,P49_PD_RELOAD_TIMEOUT=11,
 P49_PD_CLEANUP_FAILED=12,P49_PD_SOURCE_CHANGED=13,P49_PD_INITIAL_DRAIN_UNPROVEN=14};
enum p49_pd_word {P49_PD_W_MAGIC=0,P49_PD_W_SCHEMA=1,P49_PD_W_NONCE=2,
 P49_PD_W_PHASE=3,P49_PD_W_ERROR=4,P49_PD_W_OWNER=5,P49_PD_W_BASE=6,
 P49_PD_W_SOURCE=7,P49_PD_W_SIZE=8,P49_PD_W_BANK=9,P49_PD_W_SAVED_CB=10,
 P49_PD_W_SAVED_ENABLE=11,P49_PD_W_SAVED_PENDING=12,P49_PD_W_SAVED_COMMAND=13,
 P49_PD_W_REGISTER_STATUS=14,P49_PD_W_LOAD_STATUS=15,P49_PD_W_DISABLE_STATUS=16,
 P49_PD_W_PTE_BEFORE=17,P49_PD_W_VISIBILITY=18,P49_PD_W_CONFIG_DONE=19,
 P49_PD_W_RELOAD_DONE=20,P49_PD_W_IRQ_COUNT=21,P49_PD_W_FOREIGN_IRQ=22,
 P49_PD_W_ARMED_GENERATION=23,P49_PD_W_SEEN_GENERATION=24,
 P49_PD_W_CONFIG_POLLS=25,P49_PD_W_RELOAD_POLLS=26,P49_PD_W_IRQ_SP=27,
 P49_PD_W_IRQ_LR=28,P49_PD_W_IRQ_BIT=29,P49_PD_W_IRQ_PENDING=30,
 P49_PD_W_PTE_AFTER=31,P49_PD_W_DMA_SOURCE=32,P49_PD_W_DMA_SIZE=33,
 P49_PD_W_DMA_BANK=34,P49_PD_W_COMMAND_AFTER=35,P49_PD_W_MODE_AFTER=36,
 P49_PD_W_CALLBACK=37,P49_PD_W_DDR_TARGET_EQUAL=38,P49_PD_W_ROOTS_EQUAL=39,
 P49_PD_W_SLOT_AFTER=40,P49_PD_W_ENABLE_AFTER=41,P49_PD_W_PENDING_AFTER=42,
 P49_PD_W_ACTIVE=43,P49_PD_W_INFLIGHT=44,P49_PD_W_CONFIG_SOURCE=45,
 P49_PD_W_CONFIG_SIZE=46,P49_PD_W_CONFIG_BANK=47,P49_PD_W_PROFILE_MODE=48,
 P49_PD_W_SOURCE_CC_ENABLED=49,P49_PD_W_SOURCE_RELOAD_CALLED=50};
/* Same pinned TRE=1 PRRR/NMRR contract as handoff_core.h. Require the
 * entire parameter source to be privileged RW identity Normal-NC memory.
 * DSB is subsequently used only for ordering; no cache operation is guessed.
 */
static P49_PD_CONTRACT_FN int p49_pd_parameter_uncached(uint32_t desc){
 if((desc&3u)!=2u||(desc&0x0004e010u)||!(desc&0x400u)||
    (desc&0xfff00000u)!=(P49_PD_SOURCE&0xfff00000u))return 0;
 unsigned n=((desc>>10)&4u)|((desc>>2)&3u);
 const uint32_t prrr=0x000a8aa4u,nmrr=0x44e048e0u;
 return ((prrr>>(2u*n))&3u)==2u&&
  ((nmrr>>(2u*n))&3u)==0u&&((nmrr>>(16u+2u*n))&3u)==0u;
}
static P49_PD_CONTRACT_FN int p49_pd_irq_boundary(uint32_t sp,uint32_t lr,uint32_t bit,uint32_t pending){
 return sp>=0x81000000u&&sp<0x93fff000u&&!(sp&7u)&&
  lr==P49_PD_ISR_RETURN&&bit==3u&&(pending&P49_PD_BIT);
}
static P49_PD_CONTRACT_FN int p49_pd_completion_record_valid(const uint32_t *v,uint32_t nonce){
 if(!nonce||v[0]!=P49_PD_MAGIC||v[1]!=P49_PD_SCHEMA||v[2]!=nonce||v[3]!=P49_PD_CLOSED||v[4]||
  v[5]<0x81000000u||v[5]>=0x93fff000u||(v[5]&3u)||v[6]!=0xbb71d300u||
  v[7]!=P49_PD_SOURCE||v[8]!=P49_PD_SIZE||v[9]!=1u||v[10]||
  (v[11]&P49_PD_BIT)||(v[12]&P49_PD_BIT)||(v[13]&16u)||v[14]||v[15]||v[16]||
  !p49_pd_parameter_uncached(v[17])||v[18]!=1u||v[19]!=1u||v[20]!=1u||
  v[21]!=2u||v[22]||v[23]!=2u||v[24]!=2u||
  v[25]>P49_PD_MAX_POLLS||v[26]>P49_PD_MAX_POLLS||
  !p49_pd_irq_boundary(v[27],v[28],v[29],v[30])||v[31]!=v[17]||
  v[32]!=P49_PD_SOURCE||v[33]!=P49_PD_SIZE||v[34]!=1u||
  !v[37]||v[38]!=1u||v[39]!=1u||v[40]||(v[41]&P49_PD_BIT)||
  (v[42]&P49_PD_BIT)||v[43]||v[44]||v[45]!=P49_PD_SOURCE||
  v[46]!=P49_PD_SIZE||v[47]!=1u)return 0;
 for(unsigned i=48;i<64;i++){if(v[i])return 0;}
 return 1;
}
static P49_PD_CONTRACT_FN int p49_pd_audit_valid(const uint32_t *v,uint32_t nonce){
 return P49_PD_INITIAL_DRAIN_PROVEN&&p49_pd_completion_record_valid(v,nonce);
}
static P49_PD_CONTRACT_FN int p49_pd_factory_audit_valid(const uint32_t *v,uint32_t nonce){
 if(!nonce||v[0]!=P49_PD_MAGIC||v[1]!=P49_PD_SCHEMA||v[2]!=nonce||
  v[3]!=P49_PD_SKIPPED_FACTORY_CC||v[P49_PD_W_PROFILE_MODE]!=P49_PD_MODE_FACTORY_CC)return 0;
 for(unsigned i=4;i<64;i++){
  if(i!=P49_PD_W_PROFILE_MODE&&v[i])return 0;
 }
 return 1;
}
/* Original SetConfig has a geometry load and a final IQ-manager load.
 * This records both calls, not DMA completion. Source IQ precedes the latter;
 * no extra reload, handler replacement or synthetic interrupt is introduced. */
static P49_PD_CONTRACT_FN int p49_pd_single_audit_valid(const uint32_t*v,uint32_t nonce){
 if(!nonce||v[0]!=P49_PD_MAGIC||v[1]!=P49_PD_SCHEMA||v[2]!=nonce||v[3]!=8||v[48]!=P49_PD_MODE_SINGLE_CONFIG_IQ||v[49]!=1||v[50]||v[51]!=2||v[52]>2||v[53]!=1)return 0;
 for(unsigned i=4;i<64;i++)if((i<48||i>53)&&v[i])return 0;return 1;
}
_Static_assert(P49_PD_OFFSET+P49_PD_BYTES<=0xc000u,"parameter audit before code");
_Static_assert((P49_PD_SOURCE>>20)==((P49_PD_SOURCE+P49_PD_SIZE-1u)>>20),"one parameter MMU section");
#endif
