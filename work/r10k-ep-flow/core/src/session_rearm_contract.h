/* SPDX-License-Identifier: AGPL-3.0-only. Pure read-only resource predicates. */
#ifndef NX51_REARM_CONTRACT_H
#define NX51_REARM_CONTRACT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define R7_BLOCKS 45056u
#define R7_OLD_BLOCKS 8192u
#define R7_CAP_BYTES 134217728u
static uint32_t r7_front_bytes=134217728u,r7_backend_bytes=201326592u;
static int r7_set_front_capacity(uint32_t n){if(n!=167772160u&&n!=134217728u&&n!=201326592u&&n!=218103808u&&n!=251658240u)return 0;r7_front_bytes=n;return 1;}
static int r7_set_backend_capacity(uint32_t n){if(n!=167772160u&&n!=201326592u&&n!=218103808u&&n!=251658240u)return 0;r7_backend_bytes=n;return 1;}

static int r7_native_worker_name(const char*s){const char*p[]={"nxl-demo-front","nxl-demo-post","nxl-hw-worker","nxl-hw-controll","nxl-oem-restor","nxl-allocprobe","nxl-bitbworker"};if(!s)return 1;for(unsigned i=0;i<sizeof p/sizeof*p;i++)if(!strncmp(s,p[i],strlen(p[i])))return 1;return !strcmp(s,"dfmsd");}
static int r7_extent(uint32_t pa,uint32_t va){return pa>=0x94000000u&&!(pa&16383u)&&va&&!(va&16383u)&&(uint64_t)pa+r7_front_bytes<=0xc0000000ULL&&(uint64_t)va+r7_front_bytes<=0x100000000ULL&&((pa-0x94000000u)>>14)<=R7_BLOCKS-r7_front_bytes/16384u;}
static int r7_old_ref(uint32_t v,uint32_t pa,uint32_t va){return ((uint64_t)v>=pa&&(uint64_t)v<(uint64_t)pa+r7_front_bytes)||((uint64_t)v>=va&&(uint64_t)v<(uint64_t)va+r7_front_bytes);}
static int r7_roots_clear(const uint32_t *v,size_t n,uint32_t pa,uint32_t va){if(!r7_extent(pa,va))return 0;for(size_t i=0;i<n;i++)if(r7_old_ref(v[i],pa,va))return 0;return 1;}
/* Free-looking interior entries are insufficient: check every busy span. */
static int r7_span_free(const uint32_t *t,uint32_t begin,uint32_t blocks){if(!blocks||blocks>R7_BLOCKS||begin>R7_BLOCKS-blocks)return 0;uint32_t end=begin+blocks;for(uint32_t i=0;i<R7_BLOCKS;i++){uint32_t kind=t[4*i]&255u,span=t[4*i+1];if(kind){if(!span||span>R7_BLOCKS-i)return 0;if(i<end&&i+span>begin)return 0;}if(i>=begin&&i<end&&(kind||span))return 0;}return 1;}
static int r7_old_range_free(const uint32_t *t,uint32_t pa,uint32_t va){if(!r7_extent(pa,va))return 0;uint32_t begin=(pa-0x94000000u)>>14,end=begin+r7_front_bytes/16384u;for(uint32_t i=0;i<R7_BLOCKS;i++){uint32_t kind=t[4*i]&255u,span=t[4*i+1];if(kind){if(!span||span>R7_BLOCKS-i)return 0;if(i<end&&i+span>begin)return 0;}if(i>=begin&&i<end&&(kind||span))return 0;}return 1;}
#endif
