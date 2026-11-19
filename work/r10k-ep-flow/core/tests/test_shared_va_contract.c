#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../common/shared_workspace.h"
#include "../src/session_rearm_contract.h"
static unsigned checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
static struct sw_lease make(uint32_t va){struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;s.pa=0xa1c00000u;s.va=va;s.nonce=1005382510;s.owner_pid=606;s.owner_start_ticks=6763;s.index=(s.pa-0x94000000u)/16384;s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;memset(s.prefix_sha256,'a',64);memset(s.lut_sha256,'b',64);memset(s.source_sha256,'c',64);s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;sw_seal(&s);return s;}
int main(void){uint32_t*t=calloc(R7_BLOCKS,16);CHECK(t);const unsigned caps[]={134217728,167772160,184549376,201326592,218103808,251658240};
 for(unsigned page=0;page<4;page++){uint32_t va=0x90b34000u+page*4096;struct sw_lease a=make(va),s=a;CHECK(sw_valid(&s));CHECK(sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE));CHECK(sw_transition(&s,SW_BACKEND_ACTIVE,SW_BACKEND_DONE));CHECK(sw_transition(&s,SW_BACKEND_DONE,SW_RELEASED));CHECK(sw_valid(&s)&&sw_same_allocation(&a,&s));CHECK(!sw_transition(&s,SW_RELEASED,SW_BACKEND_ACTIVE));s=a;s.va+=4096;sw_seal(&s);CHECK(sw_valid(&s)&&!sw_same_allocation(&a,&s));
  for(unsigned fault=0;fault<10;fault++){s=a;switch(fault){case 0:s.va=0;break;case 1:s.va+=4;break;case 2:s.va=0xf8000000u;break;case 3:s.pa+=4096;break;case 4:s.index++;break;case 5:s.nonce=0;break;case 6:s.owner_pid=1;break;case 7:s.source_sha256[0]='g';break;case 8:s.reserved[0]=1;break;case 9:s.frame_callbacks_retired=0;break;}sw_seal(&s);CHECK(!sw_valid(&s));}s=a;s.checksum^=1;CHECK(!sw_valid(&s));
  for(unsigned c=0;c<6;c++){CHECK(r7_set_front_capacity(caps[c]));CHECK(r7_extent(a.pa,va));CHECK(!r7_extent(a.pa+4096,va));CHECK(!r7_extent(a.pa,va+4));CHECK(!r7_extent(a.pa,0));CHECK(!r7_extent(a.pa,0xff000000u));memset(t,0,R7_BLOCKS*16u);CHECK(r7_old_range_free(t,a.pa,va));unsigned at=(a.pa-0x94000000u)/16384;t[4*at]=35;t[4*at+1]=caps[c]/16384;CHECK(!r7_old_range_free(t,a.pa,va));memset(t,0,R7_BLOCKS*16u);uint32_t roots[2]={0,0};CHECK(r7_roots_clear(roots,2,a.pa,va));const uint32_t occupied[]={a.pa,a.pa+caps[c]-1,va,va+caps[c]-1};for(unsigned k=0;k<4;k++){roots[0]=occupied[k];CHECK(!r7_roots_clear(roots,2,a.pa,va));}roots[0]=va+caps[c];CHECK(r7_roots_clear(roots,2,a.pa,va));}
 }
 free(t);printf("PASS shared and rearm VA4K/PA16K matrix: checks=%u\n",checks);return 0;}
