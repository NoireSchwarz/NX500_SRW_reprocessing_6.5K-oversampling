/* SPDX-License-Identifier: AGPL-3.0-only. Allocation shape is not an address
 * reservation. An original fixed-size block cannot coexist with a validated
 * different partition of the same slots. Same-shape ABA remains ambiguous. */
#ifndef NX_ALLOCATION_LIFETIME_H
#define NX_ALLOCATION_LIFETIME_H
#include "session_rearm_contract.h"
enum r7_range_state {R7_RANGE_INVALID,R7_RANGE_VACANT,R7_RANGE_REUSED,R7_RANGE_SAME_SHAPE};
struct r7_allocation_lifetime {
 unsigned valid,front_state,backend_state,bad_index,head_count,free_blocks;
 uint32_t front_pa,backend_pa,front_bytes,backend_bytes;
 unsigned char occupied[(R7_BLOCKS+7u)/8u];
};
static const char *r7_range_name(unsigned state){
 switch(state){case R7_RANGE_VACANT:return "VACANT";case R7_RANGE_REUSED:return "REUSED";case R7_RANGE_SAME_SHAPE:return "SAME_SHAPE_UNPROVEN";default:return "INVALID";}
}
static int r7_allocation_absent(unsigned state){return state==R7_RANGE_VACANT||state==R7_RANGE_REUSED;}
static int r7_allocation_layout(const uint32_t *table,uint32_t front,uint32_t back,struct r7_allocation_lifetime *out){
 memset(out,0,sizeof *out);out->front_pa=front;out->backend_pa=back;out->front_bytes=r7_front_bytes;out->backend_bytes=r7_backend_bytes;
 if(!table||front<0x94000000u||(front&16383u)||(uint64_t)front+r7_front_bytes>0xc0000000ULL||
    back<0x94000000u||(back&16383u)||(uint64_t)back+r7_backend_bytes>0xc0000000ULL)return 0;
 uint32_t f=(front-0x94000000u)>>14,b=(back-0x94000000u)>>14;
 unsigned f_busy=0,b_busy=0,f_same=0,b_same=0,front_blocks=r7_front_bytes/16384u,back_blocks=r7_backend_bytes/16384u;
 for(unsigned i=0;i<R7_BLOCKS;){
  unsigned kind=table[4*i]&255u,span=table[4*i+1];out->bad_index=i;
  if(!kind){if(span)return 0;out->free_blocks++;i++;continue;}
  if(!span||span>R7_BLOCKS-i)return 0;
  /* Allocated interiors may be unmarked or repeat the kind and remaining
   * span. Never interpret a repeated interior as a new allocation head.
   * Stale auxiliary words are not treated as generation identifiers. */
  for(unsigned j=1;j<span;j++){
   unsigned k=table[4*(i+j)]&255u,n=table[4*(i+j)+1];
   if(k?(k!=kind||n!=span-j):n!=0){out->bad_index=i+j;return 0;}
  }
  out->head_count++;
  for(unsigned j=0;j<span;j++)out->occupied[(i+j)>>3]|=(unsigned char)(1u<<((i+j)&7));
  if(i<f+front_blocks&&i+span>f)f_busy=1;
  if(i<b+back_blocks&&i+span>b)b_busy=1;
  if(i==f&&span==front_blocks)f_same=1;
  if(i==b&&span==back_blocks)b_same=1;
  i+=span;
 }
 out->front_state=f_same?R7_RANGE_SAME_SHAPE:f_busy?R7_RANGE_REUSED:R7_RANGE_VACANT;
 out->backend_state=b_same?R7_RANGE_SAME_SHAPE:b_busy?R7_RANGE_REUSED:R7_RANGE_VACANT;
 out->valid=1;out->bad_index=UINT32_MAX;return 1;
}
static int r7_reused_reference(const struct r7_allocation_lifetime *m,uint32_t pa){
 if(!m->valid||pa<0x94000000u||pa>=0xc0000000u)return 0;
 unsigned f=(uint64_t)pa>=m->front_pa&&(uint64_t)pa<(uint64_t)m->front_pa+m->front_bytes;
 unsigned b=(uint64_t)pa>=m->backend_pa&&(uint64_t)pa<(uint64_t)m->backend_pa+m->backend_bytes;
 if(!f&&!b)return 1;
 if((f&&!r7_allocation_absent(m->front_state))||(b&&!r7_allocation_absent(m->backend_state)))return 0;
 unsigned index=(pa-0x94000000u)>>14;
 /* A reference into a free slot still lacks a current owner. Replacement
  * attribution requires a validated occupied allocation in this snapshot. */
 return !!(m->occupied[index>>3]&(1u<<(index&7)));
}
#endif
