#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../common/bank_capacity.h"
int main(void){
 uint32_t*t=calloc(45056,16);assert(t);FILE*f=fopen("/mnt/mmc/bank.bin","rb");assert(f&&fread(t,16,45056,f)==45056&&!fclose(f));unsigned largest=0;
 assert(cap_bank2_largest(t,45056,0xa1c00000u,0xc0000000u,&largest));assert(largest==12316u);
 assert(largest>=160u*64u&&largest<208u*64u);printf("PASS actual field bank2 capacity %u blocks,160 fits208 rejected\n",largest);
 assert(!cap_bank2_largest(t,45055,0xa1c00000u,0xc0000000u,&largest));
 assert(!cap_bank2_largest(t,45056,0xa1c00001u,0xc0000000u,&largest));
 assert(!cap_bank2_largest(t,45056,0xa1c00000u,0xa1c00000u,&largest));
 memset(t,0,45056*16);unsigned head=(0xa1bfc000u-0x94000000u)/16384u;t[head*4]=35;t[head*4+1]=100;
 assert(cap_bank2_largest(t,45056,0xa1c00000u,0xa2000000u,&largest)&&largest==157u);
 t[head*4+1]=45056;assert(!cap_bank2_largest(t,45056,0xa1c00000u,0xa2000000u,&largest));free(t);puts("PASS dynamic bank bounds and occupied-span interiors; no hardware");return 0;
}
