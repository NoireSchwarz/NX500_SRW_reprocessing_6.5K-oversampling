/* SPDX-License-Identifier: AGPL-3.0-only. Reference reuse is not a global
 * completion-queue reset. Completed notifications on unrelated channels
 * remain untouched. Waiters, enabled interrupts and owned stale completions
 * still prevent admission. A cold device keeps the former all-empty rule.
 */
#ifndef NXL_EP_REFERENCE_IDLE_H
#define NXL_EP_REFERENCE_IDLE_H
#include "../hardware/src/ep_sync_core.h"
/* JPEG error/completion 2/5; LUT table access 7/8; copy/ownership 13.
 * These are the existing worker's channels, not a list of observed residual
 * values. The worker independently checks its whole baseline and HW idle. */
#define EPRI_PIPELINE_ACC_MASK ((1u<<2)|(1u<<5)|(1u<<7)|(1u<<8)|(1u<<13))
static int epri_entry_refcount(uint32_t references,int releasing){return releasing?references>0u&&references<=32u:references<32u;}
static const char *epri_error(const uint32_t *raw,const uint32_t mutex[3],uint32_t references,uint32_t *retained){
 /* Existing acquisition admits baseline <32. Its one acquired reference can
  * therefore be 32 until return; it must remain possible to verify/close it.
  * Public acquisition/read-only entry retains the original baseline limit. */
 *retained=0;if(references>32u)return "EP_REFERENCE_COUNT_OUT_OF_RANGE";
 struct ep_sync_observation o;ep_sync_decode((const unsigned char*)raw,mutex,&o);const char *error=ep_sync_observation_error(&o);if(error)return error;
 for(unsigned i=0;i<5;i++)if(raw[(0x64u+i*4u)/4u])return "EP_INTERRUPT_TRANSACTION_ACTIVE";
 if(raw[(0xc07554c4u-EP_KERNEL_STATE_VA)/4u])return "EP_DRIVER_ERROR_PRESENT";
 for(unsigned i=0;i<21;i++){
  uint32_t at=EP_KERNEL_ACC_ARRAY_VA+i*12u,off=(at-EP_KERNEL_STATE_VA)/4u;
  if(raw[off+1]!=at+4u||raw[off+2]!=at+4u)return "EP_COMPLETION_WAITERS_OR_LAYOUT_UNCONFIRMED";
  if(raw[off]){
   if(!references)return "EP_COMPLETION_QUEUE_NOT_EMPTY";
   if(EPRI_PIPELINE_ACC_MASK&(1u<<i))return "EP_PIPELINE_COMPLETION_NOT_FRESH";
   *retained|=1u<<i;
  }
 }
 /* The existing LUT worker waits on CORE5 as well as its ACC channels. */
 const uint32_t core5=0xc0755604u;unsigned off=(core5-EP_KERNEL_STATE_VA)/4u;
 if(raw[off]||raw[off+1]!=core5+4u||raw[off+2]!=core5+4u)return "EP_PIPELINE_CORE5_NOT_FRESH";
 return NULL;
}
#endif
