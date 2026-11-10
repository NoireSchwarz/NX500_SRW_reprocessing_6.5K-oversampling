#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "../src/oem_ro_read.h"
static unsigned checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void previous(int sig,siginfo_t *info,void *context){(void)context;char b[240];int n=snprintf(b,sizeof b,"PREVIOUS signal=%d code=%d address=%p active=%d low=%p high=%p rejected_code=%d rejected_in_window=%d original_address=%p\n",sig,info?info->si_code:0,info?info->si_addr:NULL,oem_ro_active,(void*)oem_ro_low,(void*)oem_ro_high,oem_ro_rejected_code,oem_ro_rejected_in_window,(void*)oem_ro_rejected_address);ssize_t written=write(2,b,(size_t)n);(void)written;_exit(90);}
static int same_mask(const sigset_t *a,const sigset_t *b){for(int s=1;s<NSIG;s++)if(sigismember(a,s)!=sigismember(b,s))return 0;return 1;}
static int same_handler(const struct sigaction *a,const struct sigaction *b){return a->sa_sigaction==b->sa_sigaction&&a->sa_flags==b->sa_flags&&same_mask(&a->sa_mask,&b->sa_mask);}
static void outside(unsigned mode){fprintf(stderr,"OUTSIDE %u\n",mode);
 pid_t p=fork();assert(p>=0);if(!p){struct rlimit r={0,0};setrlimit(RLIMIT_CORE,&r);struct sigaction old={0},sa={0};old.sa_handler=SIG_DFL;sigemptyset(&old.sa_mask);oem_ro_previous_bus=oem_ro_previous_segv=old;sa.sa_sigaction=oem_ro_handler;sa.sa_flags=SA_SIGINFO;sigemptyset(&sa.sa_mask);sigaction(SIGBUS,&sa,NULL);sigaction(SIGSEGV,&sa,NULL);
  unsigned char *from=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),*other=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(from!=MAP_FAILED&&other!=MAP_FAILED);oem_ro_low=(uintptr_t)from;oem_ro_high=(uintptr_t)from+4;oem_ro_active=mode!=0;
  if(sigsetjmp(oem_ro_jump,1))_exit(77);
  if(mode==2)raise(SIGBUS);
  else if(mode==3){oem_ro_active=0;sigaction(SIGBUS,&old,NULL);sigaction(SIGSEGV,&old,NULL);struct oem_ro_fault fault;unsigned char value;assert(oem_ro_guarded_load(from,&value,1,1,&fault));oem_ro_guarded_load(from,other,4,4,&fault);}
  else if(mode==4){oem_ro_high=(uintptr_t)other+4;oem_ro_low=(uintptr_t)other+1;(void)*(volatile unsigned char*)other;}
  else (void)*(volatile unsigned char*)other;
  _exit(78);
 }int status=0;assert(waitpid(p,&status,0)==p);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==(mode==2?SIGBUS:SIGSEGV));
}
int main(void){
 static const uint32_t allowed[]={0x20810308,0x20810310,0x20810314,0x20810380,0x20810384,0x20810388,0x20810390,0x20810408,0x2081040c,0x20810418,0x2081045c,0x20810468,0x20810770,0x20810774,0x20810778,0x2081077c,0x20810780,0x20810784,0x208107d4,0x208107d8,0x208107dc,0x208107e0,0x208107e4,0x208107e8};
 for(unsigned i=0;i<sizeof allowed/sizeof *allowed;i++){CHECK(oem_mmio_address(allowed[i]));CHECK(!oem_mmio_address(allowed[i]+1));CHECK(!oem_ram_window(allowed[i],4));}
 CHECK(!oem_mmio_address(0x2081030c));CHECK(!oem_mmio_address(0x2081076c));CHECK(!oem_mmio_address(0x20810788));CHECK(!oem_mmio_address(0x208107d0));CHECK(!oem_mmio_address(0x208107ec));CHECK(!oem_mmio_address(0x8142a6f0));
 CHECK(oem_ram_window(0x80000000,1));CHECK(oem_ram_window(0x93ffffff,1));CHECK(oem_ram_window(0x93fffffc,4));CHECK(!oem_ram_window(0x7fffffff,1));CHECK(!oem_ram_window(0x94000000,4));CHECK(!oem_ram_window(0x93ffffff,2));CHECK(!oem_ram_window(0x81000fff,2));CHECK(!oem_ram_window(0x81000000,4097));CHECK(!oem_ram_window(0x81000000,0));
 struct sigaction custom={0},before_bus,before_segv,after;custom.sa_sigaction=previous;custom.sa_flags=SA_SIGINFO;sigemptyset(&custom.sa_mask);sigaddset(&custom.sa_mask,SIGUSR2);assert(!sigaction(SIGBUS,&custom,NULL)&&!sigaction(SIGSEGV,&custom,NULL));sigaction(SIGBUS,NULL,&before_bus);sigaction(SIGSEGV,NULL,&before_segv);
 sigset_t mask,before,after_mask;sigprocmask(SIG_SETMASK,NULL,&mask);sigaddset(&mask,SIGUSR1);sigaddset(&mask,SIGBUS);sigaddset(&mask,SIGSEGV);assert(!sigprocmask(SIG_SETMASK,&mask,NULL));sigprocmask(SIG_SETMASK,NULL,&before);
 void *map;struct oem_ro_fault fault;uint32_t word=0xdeadbeef;
#if !defined(__arm__)
 int fd=open("ro-truncation.bin",O_RDWR|O_CREAT|O_EXCL,0600);assert(fd>=0&&!ftruncate(fd,4096));map=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,0);assert(map!=MAP_FAILED&&!ftruncate(fd,0));
 fprintf(stderr,"BUS START at=16\n");CHECK(!oem_ro_guarded_load((unsigned char*)map+16,&word,4,4,&fault));CHECK(fault.signal==SIGBUS&&fault.code>0&&fault.offset==0);CHECK(word==0xdeadbeef&&!oem_ro_active);munmap(map,4096);close(fd);unlink("ro-truncation.bin");
#else
 /* QEMU user-mode forwards the host truncation fault address outside the
  * actual guest read window. Do not weaken the guard to accommodate it.
  * The host run covers these three real mmap-truncation SIGBUS assertions. */
 fputs("QEMU_LIMIT:3 host-truncation BUS assertions excluded; exact-window policy unchanged\n",stderr);
#endif
 fprintf(stderr,"SEGV START\n");map=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(map!=MAP_FAILED);unsigned char byte=99;CHECK(!oem_ro_guarded_load((unsigned char*)map+31,&byte,1,1,&fault));CHECK(fault.signal==SIGSEGV&&fault.code>0&&fault.offset==0&&byte==99&&!oem_ro_active);munmap(map,4096);
 map=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(map!=MAP_FAILED);*(uint32_t*)map=0x12345678;CHECK(oem_ro_guarded_load(map,&word,4,4,&fault)&&word==0x12345678&&!fault.signal&&!oem_ro_active);CHECK(oem_ro_guarded_load((unsigned char*)map+1,&byte,1,1,&fault)&&byte==0x56&&!fault.signal);CHECK(!oem_ro_guarded_load((unsigned char*)map+1,&word,4,4,&fault));CHECK(!oem_ro_guarded_load(map,&word,8,4,&fault));munmap(map,4096);
 sigprocmask(SIG_SETMASK,NULL,&after_mask);CHECK(same_mask(&before,&after_mask));sigaction(SIGBUS,NULL,&after);CHECK(same_handler(&before_bus,&after));sigaction(SIGSEGV,NULL,&after);CHECK(same_handler(&before_segv,&after));
 fprintf(stderr,"SCOPES START\n");sigemptyset(&mask);sigprocmask(SIG_SETMASK,&mask,NULL);for(unsigned i=0;i<5;i++)outside(i);
 printf("PASS %u actual guarded-reader/policy checks; real SIGBUS/SIGSEGV, masks and out-of-scope fatal cases\n",checks);return 0;
}
