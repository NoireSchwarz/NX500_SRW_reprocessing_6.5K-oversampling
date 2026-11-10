/* SPDX-License-Identifier: AGPL-3.0-only. Bounded local read-only load guards.
 * Never guard remote ptrace/native execution, logging, mapping or cleanup.
 */
#ifndef NX_OEM_RO_READ_H
#define NX_OEM_RO_READ_H
#include <stdint.h>
#include <stddef.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <string.h>
struct oem_ro_fault {int signal,code;unsigned offset;};
static int oem_ram_window(uint32_t a,unsigned bytes){return bytes&&bytes<=4096&&a>=0x80000000u&&(uint64_t)a+bytes<=0x94000000ULL&&(a&4095u)+bytes<=4096;}
static int oem_mmio_address(uint32_t a){
 static const uint32_t words[]={0x20810308u,0x20810310u,0x20810314u,0x20810380u,0x20810384u,0x20810388u,0x20810390u,0x20810408u,0x2081040cu,0x20810418u,0x2081045cu,0x20810468u,
  0x20810770u,0x20810774u,0x20810778u,0x2081077cu,0x20810780u,0x20810784u,
  0x208107d4u,0x208107d8u,0x208107dcu,0x208107e0u,0x208107e4u,0x208107e8u};
 if(a&3u)return 0;for(unsigned i=0;i<sizeof words/sizeof *words;i++)if(a==words[i])return 1;return 0;
}
/* Keep the actual device load separate, observable and exactly one word.
 * No memcpy, byte/halfword/vector access or device write may replace it. */
__attribute__((noinline)) static uint32_t oem_mmio_load32(const volatile uint32_t *p){return *p;}
__attribute__((noinline)) static unsigned char oem_ram_load8(const volatile unsigned char *p){return *p;}
static sigjmp_buf oem_ro_jump;
static volatile sig_atomic_t oem_ro_active,oem_ro_signal,oem_ro_code,oem_ro_offset,oem_ro_forwarded;
static volatile sig_atomic_t oem_ro_rejected_code,oem_ro_rejected_in_window;
static volatile uintptr_t oem_ro_low,oem_ro_high;
static volatile uintptr_t oem_ro_rejected_address;
static struct sigaction oem_ro_previous_bus,oem_ro_previous_segv;
static void oem_ro_handler(int sig,siginfo_t *info,void *context){
 (void)context;uintptr_t at=info?(uintptr_t)info->si_addr:0;
 if(oem_ro_active&&info&&info->si_code>0&&at>=oem_ro_low&&at<oem_ro_high){
  oem_ro_signal=sig;oem_ro_code=info->si_code;oem_ro_offset=(sig_atomic_t)(at-oem_ro_low);oem_ro_active=0;siglongjmp(oem_ro_jump,1);
 }
 /* Inactive, foreign-address and user-generated signals retain the original
  * disposition. Requeue while blocked by this handler; never longjmp them. */
 oem_ro_forwarded=1;oem_ro_rejected_code=info?info->si_code:0;oem_ro_rejected_in_window=(at>=oem_ro_low&&at<oem_ro_high);oem_ro_rejected_address=at;const struct sigaction *old=sig==SIGBUS?&oem_ro_previous_bus:&oem_ro_previous_segv;
 sigaction(sig,old,NULL);kill(getpid(),sig);
}
static int oem_ro_guarded_load(const volatile void *from,void *out,unsigned bytes,unsigned width,struct oem_ro_fault *fault){
 if(fault)memset(fault,0,sizeof *fault);
 if(!from||!out||!bytes||bytes>4096||(width!=1&&width!=4)||(width==4&&(bytes!=4||((uintptr_t)from&3u)))||oem_ro_active)return 0;
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_sigaction=oem_ro_handler;sa.sa_flags=SA_SIGINFO;sigemptyset(&sa.sa_mask);sigaddset(&sa.sa_mask,SIGBUS);sigaddset(&sa.sa_mask,SIGSEGV);
 sigset_t original,loads;sigemptyset(&loads);sigaddset(&loads,SIGBUS);sigaddset(&loads,SIGSEGV);
 if(sigprocmask(SIG_SETMASK,NULL,&original)||sigaction(SIGBUS,&sa,&oem_ro_previous_bus))return 0;
 if(sigaction(SIGSEGV,&sa,&oem_ro_previous_segv)){sigaction(SIGBUS,&oem_ro_previous_bus,NULL);return 0;}
 union {uint32_t word;unsigned char bytes[4096];} loaded;
 volatile int ok=0;oem_ro_signal=oem_ro_code=oem_ro_offset=oem_ro_forwarded=0;oem_ro_rejected_code=oem_ro_rejected_in_window=0;oem_ro_low=(uintptr_t)from;oem_ro_high=(uintptr_t)from+bytes;
 if(!sigsetjmp(oem_ro_jump,1)){
  if(!sigprocmask(SIG_UNBLOCK,&loads,NULL)){
   oem_ro_active=1;
   if(width==4)loaded.word=oem_mmio_load32((const volatile uint32_t*)from);
   else for(unsigned i=0;i<bytes;i++)loaded.bytes[i]=oem_ram_load8((const volatile unsigned char*)from+i);
   oem_ro_active=0;ok=!oem_ro_forwarded;
  }
 }
 oem_ro_active=0;
 if(fault){fault->signal=oem_ro_signal;fault->code=oem_ro_code;fault->offset=(unsigned)oem_ro_offset;}
 if(sigprocmask(SIG_SETMASK,&original,NULL))ok=0;
 if(sigaction(SIGBUS,&oem_ro_previous_bus,NULL))ok=0;if(sigaction(SIGSEGV,&oem_ro_previous_segv,NULL))ok=0;
 /* Caller destinations, conversion and copying are outside every load guard
  * and after restoring the caller's handlers/mask. Their faults are not ours. */
 if(ok)memcpy(out,loaded.bytes,bytes);
 return ok;
}
#endif
