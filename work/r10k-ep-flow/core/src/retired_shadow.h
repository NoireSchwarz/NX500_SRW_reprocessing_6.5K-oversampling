/* OEM DMA setters retain their configuration shadows after completion.
 * These are not allocation owners. Accept old addresses in these typed
 * fields only with a current complete allocator and inactive Still/DMA,
 * original one-shot dispatcher, retired callbacks and stable observations.
 * Reset remains read-only and never authorizes a new hardware submission. */
#ifndef NXL_RETIRED_SHADOW_H
#define NXL_RETIRED_SHADOW_H
#include "rearm_front.h"
#include "oem_restore_contract.h"
#include "../../common/resource_inventory.h"
struct rs_snapshot {uint32_t read_mask,dispatch_hash,slot[2],callbacks[80],dma[14],shadow[42];};
static int rs_idle(const struct fr_snapshot*f,const struct rs_snapshot*s,const struct ri_inventory*r){
 if(!s||!r||!r->valid||!ri_retired(r)||s->read_mask!=15u||s->dispatch_hash!=0x3dda5d7eu||
    s->slot[0]!=0x800e454cu||s->slot[1]||!r7_front_retired(f,NULL)||!oem_dma_idle(s->dma))return 0;
 for(unsigned i=0;i<80;i++)if(ri_stale_reference(r,s->callbacks[i]))return 0;
 return 1;
}
static int rs_descriptor_word(uint32_t address,const struct rs_snapshot*s){
 if((address>=0x817d7b48u&&address<0x817d7bd8u)&&!(address&3u))return 1;
 /* The six-word Still request is a saved command, not a pending callback.
  * Only its payload pointer is a physical address; require its known wire
  * type/size instead of treating arbitrary words as historical pointers. */
 return address==0x8153f418u&&s&&s->shadow[36]==0x04000001u&&s->shadow[37]==65536u;
}
static int rs_stable(const struct rs_snapshot*a,const struct rs_snapshot*b){
 return a&&b&&!memcmp(a,b,sizeof *a);
}
#endif
