/* SPDX-License-Identifier: AGPL-3.0-only. Resource retirement, not elapsed time. */
#ifndef NXL_SHARED_RETIREMENT_H
#define NXL_SHARED_RETIREMENT_H
#include "shared_workspace.h"
struct sw_front_retirement {
 uint32_t dispatch_hash,slot[2],task_vtable,task_state,counts[4],callbacks[80];
 uint32_t current_idle[14],completed_idle[14],prefix_unchanged;
};
static int sw_front_retired(const struct sw_front_retirement*s,const struct sw_lease*l){
 if(!sw_valid(l)||!s||s->dispatch_hash!=0x3dda5d7eu||s->slot[0]!=0x800e454cu||s->slot[1]||
    s->task_vtable!=0x80592640u||s->task_state!=7||!s->prefix_unchanged||
    memcmp(s->current_idle,s->completed_idle,sizeof s->current_idle))return 0;
 for(unsigned i=0;i<4;i++)if(s->counts[i])return 0;
 for(unsigned i=0;i<80;i++)if((uint64_t)s->callbacks[i]>=l->pa&&(uint64_t)s->callbacks[i]<(uint64_t)l->pa+l->capacity)return 0;
 return 1;
}
#endif
