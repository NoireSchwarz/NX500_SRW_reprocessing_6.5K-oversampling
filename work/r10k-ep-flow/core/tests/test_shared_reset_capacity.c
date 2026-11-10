#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/allocation_lifetime.h"
int main(void){
 uint32_t*t=calloc(R7_BLOCKS,16);assert(t);struct r7_allocation_lifetime m;unsigned checks=0;
 const unsigned caps[]={134217728u,167772160u,201326592u,218103808u,251658240u};uint32_t f=0x98000000u,b=0xb0000000u;unsigned at=(f-0x94000000u)/16384u;
 for(unsigned k=0;k<sizeof caps/sizeof *caps;k++){
  assert(r7_set_front_capacity(caps[k])&&r7_set_backend_capacity(k?caps[k]:201326592u));memset(t,0,R7_BLOCKS*16u);
  assert(r7_old_range_free(t,f,0x87000000u));assert(r7_allocation_layout(t,f,b,&m));assert(m.front_bytes==caps[k]&&m.front_state==R7_RANGE_VACANT);checks+=3;
  t[at*4]=35;t[at*4+1]=caps[k]/16384u;assert(!r7_old_range_free(t,f,0x87000000u));assert(r7_allocation_layout(t,f,b,&m)&&m.front_state==R7_RANGE_SAME_SHAPE);checks+=2;
  memset(t,0,R7_BLOCKS*16u);unsigned last=at+caps[k]/16384u-1;t[last*4]=35;t[last*4+1]=1;
  assert(!r7_old_range_free(t,f,0x87000000u));assert(r7_allocation_layout(t,f,b,&m)&&m.front_state==R7_RANGE_REUSED);assert(r7_reused_reference(&m,0x94000000u+last*16384u));assert(!r7_reused_reference(&m,f));checks+=4;
 }
 assert(!r7_set_front_capacity(135*1048576u));assert(!r7_set_backend_capacity(128*1048576u));checks+=2;
 printf("PASS dynamic RESET 128/160/192/208/240MiB, exact old shape, tail overlap and reused references; checks=%u\n",checks);free(t);return 0;
}
