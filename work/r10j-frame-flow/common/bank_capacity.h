/* SPDX-License-Identifier: AGPL-3.0-only. NX5001.12 original bank2 start.
 * Original80092f5c indexes manager+0x70+bank*4 before requesting kind0x23.
 * Read the runtime field: a free run in bank1 is not bank2 capacity. */
#ifndef NXL_BANK_CAPACITY_H
#define NXL_BANK_CAPACITY_H
#include <stdint.h>
#define CAP_MANAGER_BANK2_OFFSET 0x78u
static int cap_bank2_largest(const uint32_t*t,unsigned blocks,uint32_t bank2,uint32_t upper,unsigned*out){
 if(!t||!out||blocks!=45056u||bank2<0x94000000u||(bank2&16383u)||upper<=bank2||upper>0xc0000000u||(upper&16383u))return 0;
 unsigned begin=(bank2-0x94000000u)/16384u,end=(upper-0x94000000u)/16384u,covered=0,run=0;*out=0;
 for(unsigned i=0;i<blocks;i++){
  unsigned kind=t[4*i]&255u,span=t[4*i+1];
  if(kind){if(!span||span>blocks-i)return 0;if(i+span>covered)covered=i+span;}
  else if(span)return 0;
  if(i>=begin&&i<end&&i>=covered&&!kind&&!span){if(++run>*out)*out=run;}else run=0;
 }
 return 1;
}
#endif
