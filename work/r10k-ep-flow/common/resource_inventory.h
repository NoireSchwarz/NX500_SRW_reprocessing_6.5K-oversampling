/* SPDX-License-Identifier: AGPL-3.0-only. Allocation identities, not address lists.
 * An unlocated request is absent only when no current native allocation has
 * its original kind/rounded span. An ambiguous matching shape remains held. */
#ifndef NX_RESOURCE_INVENTORY_H
#define NX_RESOURCE_INVENTORY_H
#include <stdint.h>
#include <string.h>
#define RI_BLOCKS 45056u
#define RI_MAX 64u
enum {RI_EXTENT=1,RI_UNLOCATED=2};
enum {RI_VACANT=1,RI_REUSED=2,RI_AMBIGUOUS=3};
struct ri_item {uint32_t mode,pa,va,bytes,state;};
struct ri_inventory {struct ri_item item[RI_MAX];unsigned count,operations,resolved,valid,free_blocks,heads;unsigned char occupied[(RI_BLOCKS+7)/8];};
static int ri_add(struct ri_inventory*r,unsigned mode,uint32_t pa,uint32_t va,uint32_t bytes){
 if((mode!=RI_EXTENT&&mode!=RI_UNLOCATED)||!bytes||(bytes&16383u)||bytes>268435456u)return 0;
 if(mode==RI_EXTENT&&(pa<0x94000000u||(pa&16383u)||(uint64_t)pa+bytes>0xc0000000ULL))return 0;
 for(unsigned i=0;i<r->count;i++)if(r->item[i].mode==mode&&r->item[i].pa==pa&&r->item[i].bytes==bytes)return 1;
 if(r->count==RI_MAX)return 0;r->item[r->count++]=(struct ri_item){mode,pa,va,bytes,0};return 1;
}
static int ri_partition(struct ri_inventory*r,const uint32_t*t,unsigned expected_free){
 r->valid=r->heads=r->free_blocks=0;memset(r->occupied,0,sizeof r->occupied);for(unsigned k=0;k<r->count;k++)r->item[k].state=RI_VACANT;
 for(unsigned i=0;i<RI_BLOCKS;){unsigned kind=t[i*4]&255u,span=t[i*4+1];
  if(!kind){if(span)return 0;r->free_blocks++;i++;continue;}if(!span||span>RI_BLOCKS-i)return 0;
  for(unsigned j=1;j<span;j++){unsigned a=t[4*(i+j)]&255u,n=t[4*(i+j)+1];if(a?(a!=kind||n!=span-j):n!=0)return 0;}
  r->heads++;for(unsigned j=0;j<span;j++)r->occupied[(i+j)/8]|=(unsigned char)(1u<<((i+j)&7));
  for(unsigned k=0;k<r->count;k++){struct ri_item*x=&r->item[k];unsigned blocks=x->bytes/16384u;
   if(x->mode==RI_UNLOCATED){if(kind==35&&span==blocks)x->state=RI_AMBIGUOUS;}
   else{unsigned begin=(x->pa-0x94000000u)/16384u;if(i==begin&&kind==35&&span==blocks)x->state=RI_AMBIGUOUS;
    else if(i<begin+blocks&&i+span>begin&&x->state!=RI_AMBIGUOUS)x->state=RI_REUSED;}
  }i+=span;
 }
 if(r->free_blocks!=expected_free)return 0;r->valid=1;return 1;
}
static int ri_retired(const struct ri_inventory*r){if(!r->valid)return 0;for(unsigned i=0;i<r->count;i++)if(r->item[i].state!=RI_VACANT&&r->item[i].state!=RI_REUSED)return 0;return 1;}
static int ri_stale_reference(const struct ri_inventory*r,uint32_t pa){
 if(!r->valid)return 1;for(unsigned i=0;i<r->count;i++){const struct ri_item*x=&r->item[i];if(x->mode!=RI_EXTENT||(uint64_t)pa<x->pa||(uint64_t)pa>=(uint64_t)x->pa+x->bytes)continue;
  unsigned at=(pa-0x94000000u)/16384u;if(!(r->occupied[at/8]&(1u<<(at&7))))return 1;
 }return 0;
}
#endif
