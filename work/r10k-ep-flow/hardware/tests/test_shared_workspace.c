#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "../../common/shared_retirement.h"
static struct sw_lease lease(void){
 struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;
 s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;s.pa=0x98000000;s.va=0x87000000;s.nonce=1;s.owner_pid=77;s.owner_start_ticks=12345;s.index=(s.pa-0x94000000u)/16384;
 s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;s.metadata[2]=42;s.metadata[3]=100;
 memset(s.prefix_sha256,'a',64);memset(s.lut_sha256,'b',64);memset(s.source_sha256,'c',64);
 s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;sw_seal(&s);assert(sw_valid(&s));return s;
}
int main(void){
 unsigned checks=0;struct sw_lease good=lease(),s;
 assert(SW_BYTES==201326592u&&SW_INPUT==0x4000000u&&SW_LDC_OUT==0x8000000u&&SW_LUT_OUT==65536u);checks++;
 for(unsigned i=0;i<sizeof s;i++){
  s=good;((unsigned char*)&s)[i]^=1;assert(!sw_valid(&s));checks++;
 }
 s=good;assert(sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE));assert(sw_same_allocation(&s,&good));
 assert(!sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE));assert(sw_transition(&s,SW_BACKEND_ACTIVE,SW_BACKEND_DONE));
 assert(sw_transition(&s,SW_BACKEND_DONE,SW_RELEASED));assert(!sw_transition(&s,SW_RELEASED,SW_BACKEND_ACTIVE));checks+=6;
 uint32_t*t=calloc(45056u,16);assert(t);memcpy(t+good.index*4,good.metadata,16);
 unsigned free_count=45056u-good.metadata[1];assert(sw_allocation_in_table(&good,t,45056,free_count));assert(!sw_range_returned(&good,t,45056,free_count));checks+=2;
 t[(good.index+good.metadata[1]-1)*4]=35;t[(good.index+good.metadata[1]-1)*4+1]=1;assert(sw_allocation_in_table(&good,t,45056,free_count));checks++;
 t[(good.index+2)*4+1]=1;assert(!sw_allocation_in_table(&good,t,45056,free_count));t[(good.index+2)*4+1]=0;checks++;
 memset(t+good.index*4,0,good.metadata[1]*16);assert(sw_range_returned(&good,t,45056,45056));assert(!sw_allocation_in_table(&good,t,45056,45056));checks+=2;
 t[(good.index-1)*4]=35;t[(good.index-1)*4+1]=2;assert(!sw_range_returned(&good,t,45056,45054));checks++;
 memset(t,0,45056u*16);memcpy(t+good.index*4,good.metadata,16);assert(!sw_range_returned(&good,t,45056,free_count));checks++;
 struct sw_front_retirement snap={0};snap.dispatch_hash=0x3dda5d7e;snap.slot[0]=0x800e454c;snap.task_vtable=0x80592640;snap.task_state=7;snap.prefix_unchanged=1;
 assert(sw_front_retired(&snap,&good));checks++;
 for(unsigned i=0;i<4;i++){snap.counts[i]=1;assert(!sw_front_retired(&snap,&good));snap.counts[i]=0;checks++;}
 for(unsigned i=0;i<80;i++){snap.callbacks[i]=good.pa+65536;assert(!sw_front_retired(&snap,&good));snap.callbacks[i]=0;checks++;}
 for(unsigned i=0;i<14;i++){snap.current_idle[i]^=1;assert(!sw_front_retired(&snap,&good));snap.current_idle[i]^=1;checks++;}
 snap.slot[0]=good.pa;assert(!sw_front_retired(&snap,&good));snap.slot[0]=0x800e454c;checks++;
 snap.prefix_unchanged=0;assert(!sw_front_retired(&snap,&good));snap.prefix_unchanged=1;checks++;
 snap.dispatch_hash^=1;assert(!sw_front_retired(&snap,&good));checks++;
 printf("PASS shared192 lease corruption/phase/partition/return and callback retirement gates: checks=%u; no hardware\n",checks);free(t);return 0;
}
