#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/allocator_stability.h"
int main(void){unsigned n=45056,changed,first;uint32_t *a=calloc(n,16),*b=calloc(n,16);assert(a&&b);
 assert(alloc_snapshot_equal(123,123,123,a,b,n,&changed,&first)&&!changed&&first==UINT32_MAX);
 assert(!alloc_snapshot_equal(123,122,123,a,b,n,&changed,&first)&&!changed);
 b[70*4+3]=1;assert(!alloc_snapshot_equal(123,123,123,a,b,n,&changed,&first)&&changed==1&&first==70);
 b[2*4]=1;b[(n-1)*4+2]=1;assert(!alloc_snapshot_equal(123,123,123,a,b,n,&changed,&first)&&changed==3&&first==2);
 a[2*4]=1;a[(n-1)*4+2]=1;a[70*4+3]=1;assert(alloc_snapshot_equal(123,123,123,a,b,n,&changed,&first));
 free(a);free(b);puts("PASS 5 exactallocatorstability/count/changedentry contracts; no hardware");return 0;}
