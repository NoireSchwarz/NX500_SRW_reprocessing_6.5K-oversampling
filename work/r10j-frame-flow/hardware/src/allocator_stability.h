/* SPDX-License-Identifier: AGPL-3.0-only. Exact allocator baseline predicate. */
#ifndef NX52_ALLOCATOR_STABILITY_H
#define NX52_ALLOCATOR_STABILITY_H
#include <stdint.h>
#include <string.h>
static int alloc_snapshot_equal(uint32_t a,uint32_t b,uint32_t c,const uint32_t *first,const uint32_t *second,unsigned entries,unsigned *changed,unsigned *first_changed){
 *changed=0;*first_changed=UINT32_MAX;
 for(unsigned i=0;i<entries;i++)if(memcmp(first+4*i,second+4*i,16)){if(!*changed)*first_changed=i;(*changed)++;}
 return a==b&&b==c&&!*changed;
}
#endif
