/* SPDX-License-Identifier: AGPL-3.0-only. Stable-table capacity regression. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc_core.h"
static unsigned checks;
#define CHECK(c) do{assert(c);checks++;}while(0)
static void used(uint32_t*t,unsigned start,unsigned blocks){t[4*start]=0x23;t[4*start+1]=blocks;}
int main(int argc,char **argv){
 uint32_t*t=calloc(ALLOC_BLOCKS,16);unsigned largest=0;assert(t);
 if(argc==2){FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(t,16,ALLOC_BLOCKS,f)==ALLOC_BLOCKS&&fgetc(f)==EOF);assert(!fclose(f));assert(alloc_largest_free_span(t,&largest));printf("FIELD largest_free_blocks=%u requested_blocks=%u eligible=%u\n",largest,ALLOC_SPAN_BLOCKS,largest>=ALLOC_SPAN_BLOCKS);free(t);return 0;}
 CHECK(alloc_largest_free_span(t,&largest)&&largest==ALLOC_BLOCKS);
 /* Free-looking interior entries belong to the busy head before them. */
 used(t,0,ALLOC_BLOCKS);CHECK(alloc_largest_free_span(t,&largest)&&!largest);
 memset(t,0,ALLOC_BLOCKS*16);used(t,8192,ALLOC_BLOCKS-8192);
 CHECK(alloc_largest_free_span(t,&largest)&&largest==8192);
 CHECK(largest>=8192&&largest<12288); /* exactly 128 MiB, no arbitrary reserve */
 memset(t,0,ALLOC_BLOCKS*16);used(t,12288,ALLOC_BLOCKS-12288);
 CHECK(alloc_largest_free_span(t,&largest)&&largest==12288); /* exact 192 MiB */
 /* Plenty of total free blocks, but no 192 MiB contiguous run. */
 memset(t,0,ALLOC_BLOCKS*16);used(t,8191,1);used(t,16383,1);used(t,24575,1);used(t,32767,1);used(t,40959,1);
 CHECK(alloc_largest_free_span(t,&largest)&&largest==8191);
 memset(t,0,ALLOC_BLOCKS*16);used(t,0,1);used(t,ALLOC_BLOCKS-1,1);
 CHECK(alloc_largest_free_span(t,&largest)&&largest==ALLOC_BLOCKS-2);
 memset(t,0,ALLOC_BLOCKS*16);used(t,0,0);CHECK(!alloc_largest_free_span(t,&largest)&&largest==0);
 memset(t,0,ALLOC_BLOCKS*16);used(t,ALLOC_BLOCKS-1,2);CHECK(!alloc_largest_free_span(t,&largest));
 memset(t,0,ALLOC_BLOCKS*16);used(t,1,20);used(t,10,1);CHECK(alloc_largest_free_span(t,&largest)&&largest==ALLOC_BLOCKS-21);
 memset(t,0,ALLOC_BLOCKS*16);for(unsigned i=0;i<3840;i++)used(t,i,3840-i);
 CHECK(alloc_largest_free_span(t,&largest)&&largest==ALLOC_BLOCKS-3840); /* actual OEM interior layout */
 memset(t,0,ALLOC_BLOCKS*16);t[4*100+1]=1;CHECK(alloc_largest_free_span(t,&largest)&&largest==ALLOC_BLOCKS-101);
 CHECK(!alloc_largest_free_span(NULL,&largest));CHECK(!alloc_largest_free_span(t,NULL));
 free(t);printf("PASS allocator_capacity_r7c checks=%u; exact 128/192 MiB, fragmented ranges, busy interiors, malformed spans\n",checks);return 0;
}
