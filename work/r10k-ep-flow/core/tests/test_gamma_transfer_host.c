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
int main(void){source_desc.c0=source;((uint32_t*)(expected+P48_CC_OFFSET))[63]=456;
 for(unsigned i=0;i<19;i++){uint32_t x=gt_transfer_x[i],y=(x*1023u+8191u)/16383u;memcpy(source+356+4*i,&x,4);memcpy(source+432+4*i,&y,4);}unsigned count=0;
 for(unsigned m=0;m<=GT_MODE_MAX;m++){char mode[2]={(char)('0'+m),0};assert(!setenv("NX51_GAMMA_MODE",mode,1));assert(gt_host_prepare(123));uint32_t*q=(uint32_t*)area;assert(gt_request_valid(q,123,456)&&q[3]==m&&q[7]==p39_fnv(source,2048));if(m>=3)assert(q[1]==2&&q[35]==gt_transfer_y(m,0)&&q[53]==gt_transfer_y(m,18));count++;}
 const char*bad[]={"6","-1","03","3x","","40"};for(unsigned i=0;i<6;i++){assert(!setenv("NX51_GAMMA_MODE",bad[i],1));assert(!gt_host_prepare(123));count++;}
 assert(!unsetenv("NX51_GAMMA_MODE"));assert(gt_host_prepare(123)&&((uint32_t*)area)[3]==1);count++;assert(!gt_host_prepare(0));count++;
 puts("PASS14 production Gamma host selection; modeled CAP address only");return count==14?0:1;}
