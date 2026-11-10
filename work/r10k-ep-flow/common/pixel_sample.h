/* SPDX-License-Identifier: AGPL-3.0-only. Bounded spatial samples, never frames.
 * Same display coordinates for NV16 Y and interleaved CbCr. Missing native
 * OTF planes are explicit and MUST NOT be compared as black pixel samples. */
#ifndef NXL_PIXEL_SAMPLE_H
#define NXL_PIXEL_SAMPLE_H
#include <stdint.h>
#include <string.h>
#define PS_MAGIC 0x3153504eu
#define PS_POINTS 25u
#define PS_SIDE 16u
enum ps_status {PS_VALID=1,PS_OTF_NO_RAM=2,PS_NOT_RETIRED=3,PS_UNSUPPORTED_LAYOUT=4,PS_CHANGED=5};
enum ps_boundary {PS_BAYER_OUT=1,PS_LDC_OUT=2,PS_LUT_OUT=3};
struct ps_sample {
 uint32_t magic,schema,bytes,status,boundary,nonce,width,height,stride,uv_offset;
 uint32_t source_y,source_uv,begin_sec,begin_nsec,end_sec,end_nsec;
 char source_sha256[68];
 uint32_t xy[PS_POINTS][2];
 unsigned char tiles[PS_POINTS][2][PS_SIDE*PS_SIDE];
 uint32_t checksum;
};
_Static_assert(sizeof(struct ps_sample)<16384,"compact pixel sample");
static uint32_t ps_hash(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static void ps_seal(struct ps_sample*s){s->checksum=ps_hash(s,sizeof *s-4);}
static void ps_init(struct ps_sample*s,unsigned boundary,unsigned nonce,const char*source){
 memset(s,0,sizeof *s);s->magic=PS_MAGIC;s->schema=1;s->bytes=sizeof *s;s->boundary=boundary;s->nonce=nonce;s->width=6480;s->height=4320;
 if(source)memcpy(s->source_sha256,source,64);
 for(unsigned y=0;y<5;y++)for(unsigned x=0;x<5;x++){unsigned k=y*5+x;s->xy[k][0]=((s->width*(2*x+1)/10-8)&~1u);s->xy[k][1]=s->height*(2*y+1)/10-8;}
}
static int ps_copy(struct ps_sample*s,volatile const unsigned char*p,unsigned capacity,unsigned stride,unsigned uv){
 if(stride<s->width||stride>16384||uv<(uint64_t)stride*s->height||(uint64_t)uv+stride*s->height>capacity)return 0;
 s->stride=stride;s->uv_offset=uv;
 for(unsigned k=0;k<PS_POINTS;k++)for(unsigned c=0;c<2;c++)for(unsigned y=0;y<PS_SIDE;y++){
  uint32_t at=(c?uv:0)+(s->xy[k][1]+y)*stride+s->xy[k][0];
  for(unsigned x=0;x<PS_SIDE;x++)s->tiles[k][c][y*PS_SIDE+x]=p[at+x];
 }
 s->status=PS_VALID;ps_seal(s);return 1;
}
static int ps_valid(const struct ps_sample*s){return s->magic==PS_MAGIC&&s->schema==1&&s->bytes==sizeof *s&&s->checksum==ps_hash(s,sizeof *s-4)&&s->status==PS_VALID&&s->width==6480&&s->height==4320;}
#endif
