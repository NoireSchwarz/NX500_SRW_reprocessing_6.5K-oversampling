#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned char code_page[4096];
static unsigned mode,map_calls,unmap_calls;
static uint32_t target_page;
static void*test_map(void*want,size_t n,int prot,int flags,int fd,off_t offset){
 assert(!want&&n==4096&&prot==PROT_READ&&flags==MAP_SHARED&&fd==7&&(uint64_t)offset==target_page);map_calls++;
 if(mode==1){errno=EACCES;return MAP_FAILED;}return code_page;
}
static int test_unmap(void*p,size_t n){assert(p==code_page&&n==4096);unmap_calls++;if(mode==3){errno=EIO;return -1;}return 0;}
#define mmap test_map
#define munmap test_unmap
#include "../src/rtos_ro_guard.h"
#undef mmap
#undef munmap
int main(void){
 const uint32_t expected[8]={0xe3500000u,0xe52d4004u,0x0a00001bu,0xe3510000u,0xe3a04000u,0xe3424082u,0x03a01001u,0xe5840120u};
 for(mode=0;mode<8;mode++){
  uint32_t address=mode==7?0x804bb948u:0x804b6810u;if(mode==4)address++;if(mode==5)address=0x804b6ff0u;
  target_page=address&~4095u;map_calls=unmap_calls=0;memset(code_page,0,sizeof code_page);
  if(mode!=5)memcpy(code_page+(address&4095u),expected,sizeof expected);
  if(mode==2)code_page[(address&4095u)+12]^=1;
  struct rtos_ro_guard_audit a;int rc=rtos_ro_code_guard(mode==6?-1:7,address,expected,&a);
  assert(rc==(mode==0||mode==7));
  if(mode==4||mode==5||mode==6)assert(!map_calls&&!unmap_calls&&!a.valid);
  else if(mode==1)assert(map_calls==1&&!unmap_calls&&!a.map_success&&a.map_errno==EACCES&&!a.read_success);
  else{assert(map_calls==1&&unmap_calls==1&&a.map_success&&a.read_success);if(mode==2)assert(!a.matched&&a.first_mismatch==3);else assert(a.matched&&a.first_mismatch==UINT32_MAX);if(mode==3)assert(!a.unmap_success&&a.unmap_errno==EIO);else assert(a.unmap_success);}
  rtos_ro_guard_log(stdout,"DMA_STOP",expected,&a);
 }
 puts("PASS8 read-only P7 guard cases: matching words, map failure, actual mismatch, unmap failure, unaligned address, crossing page, invalid fd, second page; no pread or hardware writes");return 0;
}
