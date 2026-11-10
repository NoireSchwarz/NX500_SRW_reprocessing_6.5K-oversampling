/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_MEMORY_CORE_H
#define NXL_MEMORY_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define MEM_WORDS 32
#define PB_REQUEST_BYTES 10000000u
#define PB_POINTER_VA 0x004aa258u
#define MEM_MANAGER 0x81dfc800u
#define MEM_OBJECT_END 0x81ead058u
struct mem_mapping {uint32_t lo,hi,physical;};
static uint32_t mem_crc(const void *data,size_t n){const unsigned char *p=data;uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
static int mem_mapping_line(const char *line,struct mem_mapping *m){
 unsigned lo,hi,physical;char perms[8],dev[24],path[160];unsigned long inode;
 if(sscanf(line,"%x-%x %7s %x %23s %lu %159s",&lo,&hi,perms,&physical,dev,&inode,path)!=7)return 0;
 if(strcmp(path,"/dev/mem")||perms[0]!='r'||lo>=hi||physical<0x94000000u||(uint64_t)physical+hi-lo>0xc0000000ULL)return 0;
 *m=(struct mem_mapping){lo,hi,physical};return 1;
}
static int mem_translate(const struct mem_mapping *m,uint32_t va,uint32_t n,uint32_t *pa){
 if(!n||va<m->lo||(uint64_t)va+n>m->hi)return 0;
 uint64_t value=(uint64_t)m->physical+va-m->lo;
 if(value<0x94000000u||value+n>0xc0000000ULL)return 0;
 *pa=(uint32_t)value;return 1;
}
static int mem_entry_address(uint32_t pa,uint32_t base,uint32_t upper,uint32_t *index,uint32_t *address){
 if(base!=0x94000000u||upper<=base||upper>0xc0000000u||pa<base||pa>=upper||((pa-base)&16383u))return 0;
 uint32_t i=(pa-base)>>14;
 if(i>=45056u)return 0;
 uint32_t a=MEM_MANAGER+0x108u+i*16u;
 if(a<MEM_MANAGER||a>MEM_MANAGER+0xb0108u-16u)return 0;
 *index=i;*address=a;return 1;
}
static int mem_metadata_range(uint32_t address,size_t n){
 if(!n||n>264||(address&3))return 0;
 return (address>=0x817dc498u&&(uint64_t)address+n<=0x817dc4c8u)||
        (address>=MEM_MANAGER&&(uint64_t)address+n<=MEM_OBJECT_END);
}
#endif
