#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../src/gamma_contract.h"
#include "../src/cc_contract.h"
#undef GT_OFFSET
#define GT_OFFSET 0u
static unsigned char area[GT_BYTES],expected[P48_CC_OFFSET+256],source[2048];
static volatile unsigned char*cap_alias=area;
static struct {const unsigned char*c0;} source_desc;
static const __typeof__(source_desc)*p48_source=&source_desc;
static uint32_t p41_u32(const unsigned char*p,int le){assert(le);uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t p39_fnv(const void*in,unsigned n){const unsigned char*p=in;uint32_t h=2166136261u;for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
#include "../src/gamma_host.inc"

int main(void){
 uint32_t old[512];FILE*f=fopen("/c0.bin","rb");assert(f&&fread(source,1,2048,f)==2048&&!fclose(f));f=fopen("/audit.bin","rb");assert(f&&fread(old,1,sizeof old,f)==sizeof old&&!fclose(f));assert(old[3]==1);source_desc.c0=source;((uint32_t*)(expected+P48_CC_OFFSET))[63]=old[4];
 assert(!setenv("NX51_GAMMA_MODE","1",1)&&gt_host_prepare(old[2]));assert(!memcmp(area,old,256));
 for(unsigned mode=3;mode<=4;mode++){char text[2]={(char)('0'+mode),0};assert(!setenv("NX51_GAMMA_MODE",text,1)&&gt_host_prepare(old[2]));uint32_t*q=(uint32_t*)area;assert(gt_request_valid(q,old[2],old[4])&&q[1]==2&&q[7]==old[7]);for(unsigned n=0;n<19;n++)assert(q[16+n]==gt_transfer_x[n]&&q[35+n]==gt_transfer_y(mode,n));}
 puts("SOURCE19 request byte-identical to successful camera baseline; LINEAR/LOG exact predefined nodes and original C0 binding.");return 0;
}
