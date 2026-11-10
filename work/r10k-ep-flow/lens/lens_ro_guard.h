/* SPDX-License-Identifier: AGPL-3.0-only. Single-threaded local RAM reader.
 * Handlers are installed once, avoiding six signal syscalls per sampled word.
 * The guarded region contains volatile RAM loads only; no driver operations. */
#ifndef LENS_RO_GUARD_H
#define LENS_RO_GUARD_H
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
static sigjmp_buf lens_ro_jump;
static volatile sig_atomic_t lens_ro_active,lens_ro_faults,lens_ro_installed;
static volatile uintptr_t lens_ro_low,lens_ro_high;
static struct sigaction lens_ro_old_bus,lens_ro_old_segv;
static void lens_ro_handler(int sig,siginfo_t*info,void*ctx){
 (void)ctx;uintptr_t p=info?(uintptr_t)info->si_addr:0;
 if(lens_ro_active&&info&&info->si_code>0&&p>=lens_ro_low&&p<lens_ro_high){lens_ro_active=0;lens_ro_faults++;siglongjmp(lens_ro_jump,1);}
 const struct sigaction*old=sig==SIGBUS?&lens_ro_old_bus:&lens_ro_old_segv;
 sigaction(sig,old,NULL);kill(getpid(),sig);
}
static int lens_ro_begin(void){
 if(lens_ro_installed)return 0;struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_sigaction=lens_ro_handler;sa.sa_flags=SA_SIGINFO;sigemptyset(&sa.sa_mask);sigaddset(&sa.sa_mask,SIGBUS);sigaddset(&sa.sa_mask,SIGSEGV);
 if(sigaction(SIGBUS,&sa,&lens_ro_old_bus))return 0;
 if(sigaction(SIGSEGV,&sa,&lens_ro_old_segv)){sigaction(SIGBUS,&lens_ro_old_bus,NULL);return 0;}
 lens_ro_installed=1;return 1;
}
static void lens_ro_end(void){lens_ro_active=0;if(lens_ro_installed){sigaction(SIGBUS,&lens_ro_old_bus,NULL);sigaction(SIGSEGV,&lens_ro_old_segv,NULL);lens_ro_installed=0;}}
static int lens_ro_load(const volatile void*from,void*out,unsigned bytes){
 if(!lens_ro_installed||!from||!out||!bytes||bytes>4096||(bytes&3u)||((uintptr_t)from&3u)||lens_ro_active)return 0;
 uint32_t scratch[1024];lens_ro_low=(uintptr_t)from;lens_ro_high=lens_ro_low+bytes;
 if(sigsetjmp(lens_ro_jump,1)){lens_ro_active=0;return 0;}
 lens_ro_active=1;const volatile uint32_t*src=from;for(unsigned i=0;i<bytes/4;i++)scratch[i]=src[i];lens_ro_active=0;
 memcpy(out,scratch,bytes);return 1;
}
static int lens_ro_byte(const volatile unsigned char*from,unsigned char*out){
 if(!lens_ro_installed||!from||!out||lens_ro_active)return 0;lens_ro_low=(uintptr_t)from;lens_ro_high=lens_ro_low+1;
 if(sigsetjmp(lens_ro_jump,1)){lens_ro_active=0;return 0;}lens_ro_active=1;unsigned char c=*from;lens_ro_active=0;*out=c;return 1;
}
static int lens_ro_bytes(const volatile unsigned char*from,void*out,unsigned bytes){
 if(!lens_ro_installed||!from||!out||!bytes||bytes>4096||lens_ro_active)return 0;unsigned char scratch[4096];lens_ro_low=(uintptr_t)from;lens_ro_high=lens_ro_low+bytes;
 if(sigsetjmp(lens_ro_jump,1)){lens_ro_active=0;return 0;}lens_ro_active=1;for(unsigned i=0;i<bytes;i++)scratch[i]=from[i];lens_ro_active=0;memcpy(out,scratch,bytes);return 1;
}
#endif
