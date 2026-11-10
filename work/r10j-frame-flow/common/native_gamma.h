/* SPDX-License-Identifier: AGPL-3.0-only. NX5001.12 passive CIpcGammaD5.
 * Known constructor links only, no heap search, native calls or MMIO reads.
 * Equal brackets and repeated data are consistency checks, never atomicity. */
#ifndef NXL_NATIVE_GAMMA_H
#define NXL_NATIVE_GAMMA_H
#include <stdint.h>
#include <string.h>
#include "isp_compare.h"
#define GC_MAGIC 0x3143474eu
#define GC_MANAGER 0x817dccb4u
#define GC_MANAGER_VPTR 0x8062e398u
#define GC_PARENT_VPTR 0x8062f5f8u
#define GC_GAMMA_VPTR 0x8062f6b0u
#define GC_DDR_OFFSET 0x1228u
#define GC_DDR_BYTES 432u
struct gc_channel {
 uint32_t status,mask,parent,gamma,context;
 uint32_t parent_before[3],parent_after[3],head_before[8],head_after[8];
 uint32_t widths[5],tail[78],input[44];
};
struct gc_sample {
 uint32_t magic,schema,status,bytes,manager_before[5],manager_after[5];
 uint32_t roots_before[4],roots_after[4];
 struct gc_channel channel[2];uint32_t checksum,commit;
};
static int gc_ram(uint32_t a,unsigned bytes){return !(a&3u)&&a>=0x81000000u&&(uint64_t)a+bytes<=0x94000000ULL;}
static int gc_parent(ic_reader read,void*ctx,uint32_t a,uint32_t out[3]){return read(ctx,a,out,4)&&read(ctx,a+12,out+1,4)&&read(ctx,a+0x170,out+2,4);}
static void gc_read_channel(ic_reader read,void*ctx,struct gc_channel*s,uint32_t parent,unsigned bank){
 uint32_t scratch[78];s->parent=parent;
 if(!parent){s->status=2;return;}if(!gc_ram(parent,0x174)){s->status=5;return;}
 if(!gc_parent(read,ctx,parent,s->parent_before)){s->status=3;return;}
 if(s->parent_before[0]!=GC_PARENT_VPTR||((s->parent_before[1]>>8)&255u)!=bank){s->status=2;return;}
 s->gamma=s->parent_before[2];if(!s->gamma){s->status=2;return;}if(!gc_ram(s->gamma,0xf74)){s->status=5;return;}
 if(!read(ctx,s->gamma,s->head_before,sizeof s->head_before)){s->status=3;return;}s->mask|=1;
 if(s->head_before[0]!=GC_GAMMA_VPTR||s->head_before[3]!=parent){s->status=2;return;}
 if(s->head_before[7]!=5||!gc_ram(s->head_before[6],20)){s->status=6;return;}
 if(!read(ctx,s->head_before[6],s->widths,20)){s->status=3;return;}s->mask|=2;
 const uint32_t expected[5]={3,5,4,13,2};if(memcmp(s->widths,expected,20)){s->status=6;return;}
 if(!read(ctx,s->gamma+0xe3c,s->tail,sizeof s->tail)){s->status=3;return;}s->mask|=4;
 s->context=s->head_before[2];if(s->context){
  if(!gc_ram(s->context,sizeof s->input)){s->status=5;return;}
  if(!read(ctx,s->context,s->input,sizeof s->input)){s->status=3;return;}s->mask|=8;
 }
 if(!read(ctx,s->gamma+0xe3c,scratch,sizeof s->tail)){s->status=3;return;}
 if(memcmp(scratch,s->tail,sizeof s->tail)){s->status=4;return;}
 if(s->context){if(!read(ctx,s->context,scratch,sizeof s->input)){s->status=3;return;}if(memcmp(scratch,s->input,sizeof s->input)){s->status=4;return;}}
 if(!read(ctx,s->gamma,s->head_after,sizeof s->head_after)||!gc_parent(read,ctx,parent,s->parent_after)){s->status=3;return;}
 if(memcmp(s->head_before,s->head_after,sizeof s->head_before)||memcmp(s->parent_before,s->parent_after,sizeof s->parent_before)){s->status=4;return;}
 s->status=1;
}
static int gc_capture(ic_reader read,void*ctx,struct gc_sample*s){
 uint32_t bases_before[2],bases_after[2];const uint32_t expected_bases[2]={0xbb71b300u,0xbb71d300u};
 memset(s,0,sizeof *s);s->schema=1;s->bytes=sizeof *s;s->status=3;
 if(!ic_roots(read,ctx,s->roots_before)||!read(ctx,GC_MANAGER,s->manager_before,sizeof s->manager_before))goto done;
 if(s->roots_before[0]!=0x80592640u||s->manager_before[0]!=GC_MANAGER_VPTR){s->status=2;goto done;}
 if(!read(ctx,0x813ebf50u,bases_before,8))goto done;
 if(memcmp(bases_before,expected_bases,8)){s->status=6;goto done;}
 for(unsigned i=0;i<2;i++)gc_read_channel(read,ctx,s->channel+i,s->manager_before[2+i],i);
 if(!read(ctx,GC_MANAGER,s->manager_after,sizeof s->manager_after)||!ic_roots(read,ctx,s->roots_after))goto done;
 if(!read(ctx,0x813ebf50u,bases_after,8))goto done;
 s->status=memcmp(bases_before,bases_after,8)||memcmp(s->manager_before,s->manager_after,sizeof s->manager_before)||memcmp(s->roots_before,s->roots_after,sizeof s->roots_before)?4u:1u;
done:s->magic=GC_MAGIC;s->checksum=ic_hash(s,sizeof *s-8);s->commit=GC_MAGIC^s->checksum;return s->status==1;
}
_Static_assert(sizeof(struct gc_channel)==616,"Gamma channel schema1");
_Static_assert(sizeof(struct gc_sample)==1328,"Gamma sample schema1");
#endif
