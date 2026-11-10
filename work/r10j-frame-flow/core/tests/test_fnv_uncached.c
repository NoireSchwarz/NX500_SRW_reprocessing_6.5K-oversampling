#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fnv_uncached.h"
#if defined(__arm__)
extern uint32_t nxl_actual_rtos_hash(uint32_t,uint32_t);
__asm__(".pushsection .text.actual_rtos_hash,\"ax\",%progbits\n.balign 4\n.global nxl_actual_rtos_hash\n.type nxl_actual_rtos_hash,%function\nnxl_actual_rtos_hash:\n.incbin \"RTOS_HASH_BYTES_PLACEHOLDER\"\n.size nxl_actual_rtos_hash,.-nxl_actual_rtos_hash\n.popsection\n");
#endif
static uint64_t cases,bytes_checked;
static uint32_t reference(const volatile unsigned char *p,uint32_t n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static void check(const volatile unsigned char *p,uint32_t n,uint32_t expected,int pinned){
 uint32_t want=reference(p,n);if(pinned)assert(want==expected);assert(nxl_fnv1a_volatile(p,n)==want);
#if defined(__arm__)
 assert(nxl_actual_rtos_hash((uint32_t)(uintptr_t)p,n)==want);
#endif
 cases++;bytes_checked+=n;
}
static void dataset(const char *path,uint32_t exact,uint32_t expected){
 unsigned char *allocation=malloc((size_t)exact+16);assert(allocation);FILE *f=fopen(path,"rb");assert(f);assert(fread(allocation,1,exact,f)==exact&&fgetc(f)==EOF);assert(!fclose(f));
 /* Move one byte each time to exercise all four full-frame alignments. */
 for(unsigned offset=0;offset<4;offset++){if(offset)memmove(allocation+offset,allocation+offset-1,exact);check(allocation+offset,exact,expected,1);}
 free(allocation);
}
int main(int argc,char **argv){assert(argc==3);check(NULL,0,2166136261u,1);check((void*)(uintptr_t)1,0,2166136261u,1);
 _Alignas(16) unsigned char b[16384+16];uint32_t random=0x91af360du;
 for(unsigned pattern=0;pattern<3;pattern++){
  for(unsigned i=0;i<sizeof b;i++){random^=random<<13;random^=random>>17;random^=random<<5;b[i]=pattern==0?0:pattern==1?255:(unsigned char)random;}
  for(unsigned offset=0;offset<16;offset++)for(unsigned n=0;n<=257;n++)check(b+offset,n,0,0);
 }
 for(unsigned i=0;i<1000;i++){random=random*1664525u+1013904223u;unsigned n=random%8193u,offset=(random>>16)&15u;check(b+offset,n,0,0);}
 /* An overread beyond the requested end hits PROT_NONE for every short tail. */
 size_t page=(size_t)sysconf(_SC_PAGESIZE);assert(page>=4096);unsigned char *guard=mmap(NULL,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(guard!=MAP_FAILED&&mprotect(guard+page,page,PROT_READ|PROT_WRITE)==0);memset(guard+page,0x75,page);
 for(unsigned n=0;n<=1025;n++)check(guard+2*page-n,n,0,0);
 check(guard+page,(uint32_t)page,0,0);assert(!munmap(guard,3*page));
 dataset(argv[1],57938944u,3778558267u);dataset(argv[2],56177664u,2396203033u);
 printf("{\"passed\":true,\"cases\":%llu,\"reference_bytes\":%llu,\"full_frame_cases\":8,\"full_source_bytes\":57938944,\"full_target_bytes\":56177664,\"patterns\":[\"zero\",\"ff\",\"random\"],\"alignments\":16,\"short_lengths\":258,\"guard_tail_cases\":1026,\"actual_R6_ELF_hash_executed\":%s}\n",(unsigned long long)cases,(unsigned long long)bytes_checked,
#if defined(__arm__)
 "true"
#else
 "false"
#endif
 );return 0;
}
