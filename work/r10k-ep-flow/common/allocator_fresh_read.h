/* Short-lived mappings of ordinary RTOS allocator RAM. A stable old alias
 * does not establish coherence with the allocator's current metadata.
 * Keep the existing full partition/range/count predicates authoritative. */
#ifndef NXL_ALLOCATOR_FRESH_READ_H
#define NXL_ALLOCATOR_FRESH_READ_H
#include <sys/mman.h>
#include "allocator_observation.h"
#include "../core/src/oem_ro_read.h"
#define AF_TABLE_PA 0x81dfc908u
static int af_guarded_words(const volatile void*from,uint32_t*out,unsigned words){
 if(!from||!out||!words||words>1024u||((uintptr_t)from&3u)||oem_ro_active)return 0;
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_sigaction=oem_ro_handler;sa.sa_flags=SA_SIGINFO;
 sigemptyset(&sa.sa_mask);sigaddset(&sa.sa_mask,SIGBUS);sigaddset(&sa.sa_mask,SIGSEGV);
 sigset_t original,loads;sigemptyset(&loads);sigaddset(&loads,SIGBUS);sigaddset(&loads,SIGSEGV);
 if(sigprocmask(SIG_SETMASK,NULL,&original)||sigaction(SIGBUS,&sa,&oem_ro_previous_bus))return 0;
 if(sigaction(SIGSEGV,&sa,&oem_ro_previous_segv)){sigaction(SIGBUS,&oem_ro_previous_bus,NULL);return 0;}
 uint32_t loaded[1024];volatile int ok=0;
 oem_ro_signal=oem_ro_code=oem_ro_offset=oem_ro_forwarded=0;
 oem_ro_low=(uintptr_t)from;oem_ro_high=(uintptr_t)from+words*4u;
 if(!sigsetjmp(oem_ro_jump,1)&&!sigprocmask(SIG_UNBLOCK,&loads,NULL)){
  oem_ro_active=1;ok=ar_copy_words(loaded,from,words);oem_ro_active=0;ok=ok&&!oem_ro_forwarded;
 }
 oem_ro_active=0;
 if(sigprocmask(SIG_SETMASK,&original,NULL))ok=0;
 if(sigaction(SIGBUS,&oem_ro_previous_bus,NULL))ok=0;
 if(sigaction(SIGSEGV,&oem_ro_previous_segv,NULL))ok=0;
 if(ok)memcpy(out,loaded,words*4u);return ok;
}
static int af_read_words(int fd,uint32_t at,uint32_t*out,unsigned words){
 if(fd<0||!out||!words||words>AR_TABLE_WORDS||(at&3u)||at<0x80000000u||(uint64_t)at+words*4u>0x94000000ULL)return 0;
 while(words){
  unsigned count=(4096u-(at&4095u))/4u;if(count>words)count=words;
  void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,(off_t)(uint64_t)(at&~4095u));if(p==MAP_FAILED)return 0;
  int ok=af_guarded_words((volatile unsigned char*)p+(at&4095u),out,count);
  if(munmap(p,4096))ok=0;if(!ok)return 0;
  at+=count*4u;out+=count;words-=count;
 }return 1;
}
static int af_pool_head(int fd,uint32_t*free_count){
 uint32_t root[10],head[66],tail[3],upper;
 if(!free_count||!af_read_words(fd,0x817dc498u,root,10)||!af_read_words(fd,0x81dfc800u,head,66)||
    !af_read_words(fd,0x81eac908u,tail,3)||!af_read_words(fd,0x81eacda0u,&upper,1))return 0;
 if(root[0]!=0x80574230u||root[1]!=0x805742acu||root[9]!=0x81dfc800u||head[0]!=0x80574b60u||head[65]!=0x80574c48u||
    tail[0]!=0x94000000u||tail[1]!=45056u||tail[2]>45056u||upper<=0x94000000u)return 0;
 *free_count=tail[2];return 1;
}
#endif
