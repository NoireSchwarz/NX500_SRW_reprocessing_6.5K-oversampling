/* SPDX-License-Identifier: AGPL-3.0-only. Host RAM, not physical DMA. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "bitb_job.h"
#include "full_memory.h"
#include "alloc_core.h"
int main(void){
 assert(bitb_crc("123456789",9)==0xcbf43926u);
 for(unsigned i=0;i<32768;i++)assert(bitb_pattern(i,12345)==bitb_pattern(i+32768,12345));
 uint32_t *table=calloc(ALLOC_BLOCKS,16);assert(table);uint32_t index=100,info[5]={0x90000000,0x98000000,ALLOC_BYTES,2,0},entry[4]={0x23,ALLOC_SPAN_BLOCKS,0,0};
 assert(alloc_response(info,info[1],index,table,entry));
 table[(index-1)*4]=0x23;table[(index-1)*4+1]=2;assert(!alloc_response(info,info[1],index,table,entry));table[(index-1)*4]=table[(index-1)*4+1]=0;
 table[(index+ALLOC_SPAN_BLOCKS-1)*4]=0x23;table[(index+ALLOC_SPAN_BLOCKS-1)*4+1]=1;assert(!alloc_response(info,info[1],index,table,entry));table[(index+ALLOC_SPAN_BLOCKS-1)*4]=table[(index+ALLOC_SPAN_BLOCKS-1)*4+1]=0;
 table[(index+ALLOC_SPAN_BLOCKS)*4]=0x23;table[(index+ALLOC_SPAN_BLOCKS)*4+1]=1;assert(alloc_response(info,info[1],index,table,entry));
 table[(index+1)*4+1]=1;assert(!alloc_response(info,info[1],index,table,entry));table[(index+1)*4+1]=0;
 entry[1]--;assert(!alloc_response(info,info[1],index,table,entry));entry[1]++;
 assert(!alloc_response(info,info[1],ALLOC_BLOCKS-ALLOC_SPAN_BLOCKS+1,table,entry));free(table);
 unsigned char *memory=malloc(TEST_BYTES);assert(memory);struct bitb_request q={BITB_REQUEST_MAGIC,10,0xa1c00000,TEST_BYTES,12345,99,0x91000000,0,0};q.crc=bitb_crc(&q,32);struct bitb_result r={0};
 assert(full_begin(FULL_REFERENCE,&q,&r,memory));assert(full_verify(&full_before,&q,0,NULL,0));
 unsigned faults[]={TEST_FRAME-1,TEST_TABLE+19711,TEST_BYTES-1};
 for(unsigned i=0;i<sizeof faults/sizeof *faults;i++){
  unsigned at=faults[i];memory[at]^=1;assert(!full_capture(&full_after,&q,memory,0,NULL,0));assert(full_after.errors==1&&full_after.first_bad==at);assert(!full_verify(&full_after,&q,0,NULL,0));memory[at]^=1;
 }
 assert(full_capture(&full_after,&q,memory,0,NULL,0));struct full_manifest tamper=full_after;tamper.blocks[TEST_PAGES-1]^=1;tamper.crc=bitb_crc(&tamper,offsetof(struct full_manifest,crc));assert(!full_verify(&tamper,&q,0,NULL,0));
 free(memory);puts("PASS CRC, periodic guards,7 full-span allocator boundaries,96MiB exact streaming,3 physical-RAM-model mutations and CRC-consistent manifest tampering; no camera hardware");return 0;
}
