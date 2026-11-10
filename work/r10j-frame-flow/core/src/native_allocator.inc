/* SPDX-License-Identifier: AGPL-3.0-only; derived from p23a allocator. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/syscall.h>
#include "memory_core.h"
#include "memory_guards.h"
#include "alloc_core.h"
#include "../../common/bank_capacity.h"
#include "alloc_guards.h"
#ifndef __WALL
#define __WALL 0x40000000
#endif
struct region {uint32_t lo,hi,offset;char perms[5],path[200];};
struct patch {uint32_t address,original;int armed;};
static struct region regions[1200];static unsigned region_count;
static struct mem_mapping mappings[64];static unsigned mapping_count;
static struct patch patches[2];
static int pid,tid,procfd=-1,devfd=-1,attached,trace_stopped,watch_pipe=-1,watch_pid=-1,native_signal;
static uint32_t cap_base,shell_base,libc_base,udd_base,tint_base,type_base,receive_buffer,original_sp,initial_ui;
static uint32_t response[5],index_owned=UINT32_MAX,free_before,free_owned,free_after;
static uint32_t owned_meta[4];
static unsigned native_calls,request_done,ownership_verified,release_done,return_verified;
static unsigned api_inflight,entry_intercepted,code_restored,detached_ok;
static unsigned p48_unverified_shell_message,p48_parked_camera;
static unsigned thread_candidates,proc_syscall_matched,parked_registers_verified,idle_registers_verified,call_boundary_active;
static uint64_t started;
static FILE *log_file,*csv,*trace_file;
static const char *trace_phase="setup",*trace_error,*failure_stage="";
static siginfo_t stop_info;
static struct alloc_regs stop_regs;
static unsigned wait_events,breakpoints_verified,own_faults_suppressed,forwarded_signal;
static int stop_status,stop_signal,stop_info_valid,stop_regs_valid;
static volatile sig_atomic_t stop_requested;
static struct alloc_regs call_original;
static volatile unsigned char *manager_map,*root_map;
static uint32_t *baseline,*baseline_again;
static const char *reason="NOT_STARTED";
static uint64_t ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(uint32_t)t.tv_nsec;}
static unsigned ms(void){return (unsigned)((ns()-started)/1000000u);}
static void pause_ms(unsigned n){struct timespec t={n/1000,(long)(n%1000)*1000000};while(nanosleep(&t,&t)&&errno==EINTR&&!stop_requested){}}
static void stop(int sig){(void)sig;stop_requested=1;}
static void event(const char *s){fprintf(log_file,"%u %s errno=%d\n",ms(),s,errno);fflush(log_file);}
static int file_line(const char *path,char *b,size_t n){FILE *f=fopen(path,"r");if(!f)return 0;int ok=fgets(b,(int)n,f)!=NULL;fclose(f);if(ok)b[strcspn(b,"\r\n")]=0;return ok;}
static int firmware(void){char v[64],m[64];FILE *f=fopen("/etc/version.info","r");if(!f)return 0;int ok=fgets(v,sizeof v,f)&&fgets(m,sizeof m,f);fclose(f);if(!ok)return 0;v[strcspn(v,"\r\n")]=0;m[strcspn(m,"\r\n")]=0;return !strcmp(v,"1.12")&&!strcmp(m,"NX500");}
static int fingerprint(const char *path,size_t expected,uint32_t fnv){FILE *f=fopen(path,"rb");if(!f)return 0;unsigned char b[4096];size_t total=0,n;uint32_t h=2166136261u;while((n=fread(b,1,sizeof b,f))){total+=n;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;}int ok=!ferror(f)&&total==expected&&h==fnv;fclose(f);return ok;}
static int find_camera(void){DIR *d=opendir("/proc");if(!d)return 0;struct dirent *e;int found=0;while((e=readdir(d))){if(!e->d_name[0]||strspn(e->d_name,"0123456789")!=strlen(e->d_name))continue;char p[320],b[64];snprintf(p,sizeof p,"/proc/%s/comm",e->d_name);if(file_line(p,b,sizeof b)&&!strcmp(b,"di-camera-app")){if(found){found=0;break;}found=atoi(e->d_name);}}closedir(d);return found>1?found:0;}
static int save_copy(const char *a,const char *b){FILE *in=fopen(a,"rb"),*out=NULL;if(!in)return 0;out=fopen(b,"wb");if(!out){fclose(in);return 0;}char block[4096];size_t n;int ok=1;while((n=fread(block,1,sizeof block,in)))if(fwrite(block,1,n,out)!=n){ok=0;break;}if(ferror(in))ok=0;fclose(in);if(fclose(out))ok=0;return ok;}
static int load_maps(const char *dir,const char *phase){char p[100],out[1200],line[512];snprintf(p,sizeof p,"/proc/%d/maps",pid);snprintf(out,sizeof out,"%s/camera_maps_%s.txt",dir,phase);if(!save_copy(p,out))return 0;
 FILE *f=fopen(out,"r");if(!f)return 0;region_count=mapping_count=0;cap_base=shell_base=libc_base=udd_base=tint_base=type_base=0;
 while(fgets(line,sizeof line,f)){
  struct region r={0};char dev[32];unsigned long inode;
  int n=sscanf(line,"%x-%x %4s %x %31s %lu %199s",&r.lo,&r.hi,r.perms,&r.offset,dev,&inode,r.path);
  if(n>=6&&r.lo<r.hi&&region_count<1200)regions[region_count++]=r;
  struct mem_mapping m;if(mem_mapping_line(line,&m)&&mapping_count<64)mappings[mapping_count++]=m;
  if(n==7&&!r.offset&&!strcmp(r.perms,"r-xp")){
   if(!strcmp(r.path,"/usr/lib/libcapture-fw-prod.so")){if(cap_base){fclose(f);return 0;}cap_base=r.lo;}
   if(!strcmp(r.path,"/usr/lib/libshell-command.so")){if(shell_base){fclose(f);return 0;}shell_base=r.lo;}
   if(!strcmp(r.path,"/lib/libc-2.13.so")){if(libc_base){fclose(f);return 0;}libc_base=r.lo;}
   if(!strcmp(r.path,"/usr/lib/libudd5.so")){if(udd_base){fclose(f);return 0;}udd_base=r.lo;}
   if(!strcmp(r.path,"/usr/lib/libtint-util.so")){if(tint_base){fclose(f);return 0;}tint_base=r.lo;}
   if(!strcmp(r.path,"/usr/lib/libmm-type.so")){if(type_base){fclose(f);return 0;}type_base=r.lo;}
  }
 }fclose(f);return cap_base&&shell_base&&libc_base&&udd_base&&tint_base&&type_base&&mapping_count;
}
static int region_ok(uint32_t a,size_t n,int writable,int executable){if(!n||(uint64_t)a+n>UINT32_MAX)return 0;for(unsigned i=0;i<region_count;i++)if(a>=regions[i].lo&&(uint64_t)a+n<=regions[i].hi&&regions[i].perms[0]=='r'&&(!writable||regions[i].perms[1]=='w')&&(!executable||regions[i].perms[2]=='x'))return 1;return 0;}
static int read_process(uint32_t a,void *b,size_t n){return region_ok(a,n,0,0)&&pread(procfd,b,n,(off_t)a)==(ssize_t)n;}
static int read_word(uint32_t a,uint32_t *v){return !(a&3)&&read_process(a,v,4);}
static int guards(uint32_t base,const struct alloc_guard *g,size_t n){uint32_t b[8];for(size_t i=0;i<n;i++)if(!region_ok(base+g[i].offset,32,0,1)||!read_process(base+g[i].offset,b,32)||memcmp(b,g[i].words,32))return 0;return 1;}
static int app_guards(void){uint32_t b[8];for(unsigned i=0;i<MEM_GUARD_COUNT;i++)if(!read_process(mem_guards[i].address,b,32)||memcmp(b,mem_guards[i].words,32))return 0;return 1;}
static uint32_t volatile_word(const volatile unsigned char *a){uint32_t x=0;for(unsigned i=0;i<4;i++)x|=(uint32_t)a[i]<<(i*8);return x;}
static int pool_head(uint32_t *free_count){const volatile unsigned char *m=manager_map+0x800;
 if(volatile_word(root_map+0x498)!=0x80574230||volatile_word(root_map+0x49c)!=0x805742ac||volatile_word(root_map+0x4bc)!=MEM_MANAGER||
  volatile_word(m)!=0x80574b60||volatile_word(m+0x104)!=0x80574c48||volatile_word(m+0xb0108)!=0x94000000||volatile_word(m+0xb010c)!=ALLOC_BLOCKS||volatile_word(m+0xb05a0)<=0x94000000)return 0;
 *free_count=volatile_word(m+0xb0110);return *free_count<=ALLOC_BLOCKS;
}
static int map_metadata(void){devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(devfd<0)return 0;
 void *p=mmap(NULL,0xb2000,PROT_READ,MAP_SHARED,devfd,(off_t)0x81dfc000u);if(p==MAP_FAILED)return 0;manager_map=p;
 p=mmap(NULL,4096,PROT_READ,MAP_SHARED,devfd,(off_t)0x817dc000u);if(p==MAP_FAILED)return 0;root_map=p;return pool_head(&free_before);
}
static const char *allocator_snapshot_reason="ALLOCATOR_BASELINE_UNSTABLE";
static int snapshot(const char *dir){size_t n=ALLOC_BLOCKS*16u;baseline=malloc(n);baseline_again=malloc(n);if(!baseline||!baseline_again){allocator_snapshot_reason="ALLOCATOR_BASELINE_HOST_MALLOC_FAILED";return 0;}
 const volatile unsigned char *t=manager_map+0x908;
 for(int tries=0;tries<5&&!stop_requested;tries++){
  uint32_t a,b,c;if(!pool_head(&a))return 0;
  for(size_t i=0;i<n;i++)((unsigned char*)baseline)[i]=t[i];
  if(!pool_head(&b))return 0;
  for(size_t i=0;i<n;i++)((unsigned char*)baseline_again)[i]=t[i];
  if(!pool_head(&c))return 0;
  if(a==b&&b==c&&!memcmp(baseline,baseline_again,n)){
   unsigned largest=0,bank_largest=0;
   if(!alloc_largest_free_span(baseline,&largest)){allocator_snapshot_reason="ALLOCATOR_BASELINE_LAYOUT_INVALID";return 0;}
   fprintf(log_file,"ALLOCATION_CAPACITY required_blocks=%u contiguous_free_blocks=%u reported_free=%u\n",ALLOC_SPAN_BLOCKS,largest,c);fflush(log_file);
   uint32_t bank2=volatile_word(manager_map+0x800+CAP_MANAGER_BANK2_OFFSET),upper=volatile_word(manager_map+0x800+0xb05a0u);
   if(!cap_bank2_largest(baseline,ALLOC_BLOCKS,bank2,upper,&bank_largest)){allocator_snapshot_reason="ALLOCATOR_BANK2_LAYOUT_INVALID";return 0;}
   fprintf(log_file,"BANK_CAPACITY bank=2 start=0x%08x upper=0x%08x largest_blocks=%u required_blocks=%u\n",bank2,upper,bank_largest,ALLOC_SPAN_BLOCKS);fflush(log_file);
   if(bank_largest<ALLOC_SPAN_BLOCKS){allocator_snapshot_reason="ALLOCATOR_BANK2_NO_CONTIGUOUS_EXTENT";return 0;}
   free_before=c;char path[1200];snprintf(path,sizeof path,"%s/allocator_before.bin",dir);FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(baseline,1,n,f)==n;if(fclose(f))ok=0;
   fprintf(log_file,"BASELINE blocks=%u free=%u crc32=%08x\n",ALLOC_BLOCKS,c,mem_crc(baseline,n));fflush(log_file);return ok;
  }pause_ms(50);
 }return 0;
}
static int entry(uint32_t i,uint32_t words[4]){if(i>=ALLOC_BLOCKS)return 0;uint32_t a[4],b[4];const volatile unsigned char *p=manager_map+0x908+i*16;for(unsigned j=0;j<4;j++)a[j]=volatile_word(p+j*4);for(unsigned j=0;j<4;j++)b[j]=volatile_word(p+j*4);if(memcmp(a,b,16))return 0;memcpy(words,a,16);return 1;}
static int translate(uint32_t va,uint32_t *pa,uint32_t *index){int seen=0;for(unsigned i=0;i<mapping_count;i++){uint32_t p;if(mem_translate(mappings+i,va,ALLOC_BYTES,&p)){if(seen&&p!=*pa)return 0;*pa=p;seen=1;}}
 uint32_t e,upper=volatile_word(manager_map+0x800+0xb05a0);return seen&&mem_entry_address(*pa,0x94000000,upper,index,&e);
}
static void row(const char *stage,uint32_t i){uint32_t words[4]={0},f=0;int valid=pool_head(&f)&&i<ALLOC_BLOCKS&&entry(i,words);fprintf(csv,"%u,%s,%u,0x%08x,0x%08x,%u,%u,0x%08x,%u,0x%08x,0x%08x,%u\n",ms(),stage,valid,response[0],response[1],i,f,words[0],words[1],words[2],words[3],mem_crc(words,16));fflush(csv);}
static int queue_id(void){int q=msgget(ALLOC_QUEUE_KEY,0);if(q<0)return -1;struct msqid_ds ds;if(msgctl(q,IPC_STAT,&ds)||ds.msg_qnum||ds.msg_perm.uid!=getuid())return -1;return q;}
static int receive_syscall(int candidate,int queue,uint32_t *buffer,uint32_t *sp){char path[128],text[512];snprintf(path,sizeof path,"/proc/%d/task/%d/syscall",pid,candidate);if(!file_line(path,text,sizeof text))return 0;unsigned long long no,args[6],stack,pc;
 if(sscanf(text,"%llu %llx %llx %llx %llx %llx %llx %llx %llx",&no,&args[0],&args[1],&args[2],&args[3],&args[4],&args[5],&stack,&pc)!=9||no!=302||args[0]!=(unsigned)queue||args[2]!=208||args[3]||args[4]||args[1]>UINT32_MAX||stack>UINT32_MAX)return 0;
 *buffer=(uint32_t)args[1];*sp=(uint32_t)stack;
 return !(*buffer&3)&&region_ok(*buffer,208,1,0)&&region_ok(*sp,512,1,0);
}
static int find_worker(int queue,const char *directory){char path[1200],text[1024];
 snprintf(path,sizeof path,"%s/thread_context.txt",directory);FILE *dump=fopen(path,"w");if(!dump)return 0;
 fprintf(dump,"pid=%d queue=%d libc_base=0x%08x native_name=shell_di_app\n",pid,queue,libc_base);
 snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *d=opendir(path);if(!d){fprintf(dump,"TASK_DIRECTORY_ERROR errno=%d\n",errno);fclose(dump);return 0;}struct dirent *e;int found=0;unsigned scanned=0;
 while((e=readdir(d))){int candidate=atoi(e->d_name);if(candidate<=1)continue;if(++scanned>512){found=0;break;}
  char name[64]={0};snprintf(path,sizeof path,"/proc/%d/task/%d/comm",pid,candidate);errno=0;int ok=file_line(path,name,sizeof name),err=errno;
  fprintf(dump,"\ntid=%d comm_ok=%d errno=%d comm=%s\n",candidate,ok,err,name);
  const char *items[]={"syscall","wchan","stat"};for(unsigned i=0;i<3;i++){
   snprintf(path,sizeof path,"/proc/%d/task/%d/%s",pid,candidate,items[i]);errno=0;text[0]=0;int got=file_line(path,text,sizeof text),error=errno;
   fprintf(dump,"%s ok=%d errno=%d raw=%s\n",items[i],got,error,text);
  }
  uint32_t buf=0,sp=0;int match=receive_syscall(candidate,queue,&buf,&sp);fprintf(dump,"old_proc_predicate=%d\n",match);
  if(ok&&!strcmp(name,"shell_di_app")){
   thread_candidates++;found=candidate;if(match){proc_syscall_matched=1;receive_buffer=buf;original_sp=sp;}
  }
 }closedir(d);fprintf(dump,"scanned=%u native_name_candidates=%u old_proc_matched=%u\n",scanned,thread_candidates,proc_syscall_matched);
 int saved=fclose(dump)==0;snprintf(path,sizeof path,"%s/sysvipc_msg.txt",directory);save_copy("/proc/sysvipc/msg",path);
 return saved&&thread_candidates==1?found:0;
}
static int get_regs(struct alloc_regs *r){return ptrace(PTRACE_GETREGS,tid,0,r)==0;}
static int set_regs(const struct alloc_regs *r){return ptrace(PTRACE_SETREGS,tid,0,(void*)r)==0;}
/* Keep the actual event before a caller can replace its failure description. */
static int wait_stop(unsigned timeout,int expected_signal){uint64_t end=ns()+(uint64_t)timeout*1000000;trace_error=NULL;
 while(ns()<end&&!stop_requested){int status=0;pid_t p=waitpid(tid,&status,__WALL|WNOHANG);int wait_errno=errno;
  if(p==tid){
   stop_status=status;stop_signal=WIFSTOPPED(status)?WSTOPSIG(status):0;stop_info_valid=stop_regs_valid=0;memset(&stop_info,0,sizeof stop_info);wait_events++;
   fprintf(trace_file,"EVENT %u phase=%s elapsed_ms=%u status=0x%08x signal=%d expected=%d ptrace_event=%u\n",wait_events,trace_phase,ms(),(unsigned)status,stop_signal,expected_signal,(unsigned)status>>16);
   if(!WIFSTOPPED(status)){attached=trace_stopped=0;trace_error="NATIVE_WORKER_EXITED";fflush(trace_file);return 0;}
   trace_stopped=1;native_signal=stop_signal;
   errno=0;stop_info_valid=ptrace(PTRACE_GETSIGINFO,tid,0,&stop_info)==0;int info_errno=errno;
   errno=0;stop_regs_valid=get_regs(&stop_regs);int regs_errno=errno;
   fprintf(trace_file,"SIGINFO valid=%d errno=%d signo=%d code=%d address=0x%08x sender_pid=%d\n",stop_info_valid,info_errno,stop_info.si_signo,stop_info.si_code,(uint32_t)(uintptr_t)stop_info.si_addr,stop_info.si_pid);
   fprintf(trace_file,"REGISTERS valid=%d errno=%d",stop_regs_valid,regs_errno);if(stop_regs_valid)for(unsigned i=0;i<18;i++)fprintf(trace_file," r%u=0x%08x",i,stop_regs.r[i]);fputc('\n',trace_file);fflush(trace_file);
   if(stop_signal!=expected_signal||(unsigned)status>>16){trace_error="UNEXPECTED_NATIVE_SIGNAL_OR_EVENT";return 0;}
   /* A breakpoint signal stays pending until its exact instruction and origin
    * are verified. An attach-generated SIGSTOP is consumed as before. */
   if(expected_signal==SIGSTOP)native_signal=0;
   return 1;
  }
  if(p<0&&wait_errno!=EINTR){trace_error="WAITPID_FAILED";fprintf(trace_file,"WAIT_ERROR phase=%s errno=%d\n",trace_phase,wait_errno);fflush(trace_file);return 0;}
  pause_ms(10);
 }
 trace_error=stop_requested?"TRACE_STOP_REQUESTED":"TRACE_WAIT_TIMEOUT";
 fprintf(trace_file,"WAIT_END phase=%s elapsed_ms=%u limit_ms=%u reason=%s\n",trace_phase,ms(),timeout,trace_error);fflush(trace_file);return 0;
}
static int resume(void){trace_error=NULL;errno=0;int rc=ptrace(PTRACE_CONT,tid,0,0),error=errno;
 fprintf(trace_file,"CONT phase=%s elapsed_ms=%u result=%d errno=%d\n",trace_phase,ms(),rc,error);fflush(trace_file);
 if(rc){trace_error="PTRACE_CONT_FAILED";return 0;}trace_stopped=0;return 1;
}
static int own_site(unsigned i){uint32_t word=0;return trace_stopped&&stop_regs_valid&&patches[i].armed&&
 !(stop_regs.r[16]&0x20)&&(stop_regs.r[15]==patches[i].address||stop_regs.r[15]==patches[i].address+4)&&
 read_word(patches[i].address,&word)&&word==ALLOC_TRAP;
}
static int own_breakpoint(unsigned i){
 return own_site(i)&&stop_info_valid&&stop_signal==SIGTRAP&&stop_info.si_signo==SIGTRAP&&
 ((unsigned)stop_status>>16)==0&&(stop_info.si_code==1||stop_info.si_code==0x30001)&&
 (uint32_t)(uintptr_t)stop_info.si_addr==patches[i].address;
}
static int verify_breakpoint(unsigned i){
 if(!own_breakpoint(i)){trace_error=stop_info_valid?"BREAKPOINT_ORIGIN_NOT_CONFIRMED":"BREAKPOINT_SIGINFO_UNAVAILABLE";return 0;}
 breakpoints_verified++;native_signal=0;trace_error=NULL;
 fprintf(trace_file,"BREAKPOINT_VERIFIED phase=%s index=%u address=0x%08x instruction=0x%08x\n",trace_phase,i,patches[i].address,ALLOC_TRAP);fflush(trace_file);return 1;
}
static int own_instruction_fault(unsigned i){
 /* Only a synchronous fault at our still-present replacement instruction.
  * A user signal or a fault inside native code is never swallowed. */
 return own_site(i)&&stop_info_valid&&(stop_signal==SIGILL||stop_signal==SIGBUS)&&
 stop_info.si_signo==stop_signal&&stop_info.si_code>0&&((unsigned)stop_status>>16)==0&&
 stop_regs.r[15]==patches[i].address&&(uint32_t)(uintptr_t)stop_info.si_addr==patches[i].address;
}
static int restore_patch(unsigned i){uint32_t word=0;if(!patches[i].armed)return 1;if(!read_word(patches[i].address,&word))return 0;
 if(word==patches[i].original){patches[i].armed=0;return 1;}
 if(word!=ALLOC_TRAP)return 0;
 int ok=0;if(attached&&trace_stopped)ok=ptrace(PTRACE_POKETEXT,tid,(void*)(uintptr_t)patches[i].address,(void*)(uintptr_t)patches[i].original)==0;
 else ok=pwrite(procfd,&patches[i].original,4,(off_t)patches[i].address)==4;
 if(!ok||!read_word(patches[i].address,&word)||word!=patches[i].original)return 0;patches[i].armed=0;return 1;
}
static int arm_patch(unsigned i){uint32_t word;if(!trace_stopped||!read_word(patches[i].address,&word)||word!=patches[i].original)return 0;
 if(ptrace(PTRACE_POKETEXT,tid,(void*)(uintptr_t)patches[i].address,(void*)(uintptr_t)ALLOC_TRAP))return 0;patches[i].armed=1;
 return read_word(patches[i].address,&word)&&word==ALLOC_TRAP;
}
static int other_threads_running(void){char path[128],line[512];snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *d=opendir(path);if(!d)return 0;int running=0,blocked=0;struct dirent *e;
 while((e=readdir(d))){int n=atoi(e->d_name);if(n<=1||n==tid)continue;snprintf(path,sizeof path,"/proc/%d/task/%d/stat",pid,n);if(!file_line(path,line,sizeof line))continue;char *p=strrchr(line,')');if(!p||p[1]!=' ')continue;char state=p[2];if(state=='T'||state=='t')blocked++;else if(state=='R'||state=='S'||state=='D')running++;}
 closedir(d);return running>5&&!blocked;
}
static int watchdog_mode(char mode){return watch_pipe>=0&&write(watch_pipe,&mode,1)==1;}
static int watchdog_start(void){
 int p[2],owner=getpid();if(pipe2(p,O_CLOEXEC))return 0;int child=fork();if(child<0){close(p[0]);close(p[1]);return 0;}
 if(!child){
  close(p[1]);int hold=0,normal=0;uint64_t end=ns()+240000000000ULL;
  for(;;){uint64_t now=ns();int left=now<end?(int)((end-now+999999)/1000000):0;if(!left)break;struct pollfd f={p[0],POLLIN|POLLHUP,0};int rc;do{rc=poll(&f,1,left);}while(rc<0&&errno==EINTR);
   if(rc<=0)break;if(f.revents&POLLIN){char c=0;if(read(p[0],&c,1)!=1)break;if(c=='D'||c=='P'){normal=1;break;}if(c=='H')hold=1;else if(c=='V')hold=0;else break;continue;}break;
  }
  if(!normal){
   /* H is sent BEFORE the allocation-shaped message is queued. If identity
    * was not proven, never let tracer death release that message to execute. */
   if(hold&&pid>1)kill(pid,SIGSTOP);
   for(unsigned i=0;i<2;i++){uint32_t word;if(pread(procfd,&word,4,(off_t)patches[i].address)==4&&word==ALLOC_TRAP){int ok=pwrite(procfd,&patches[i].original,4,(off_t)patches[i].address)==4;dprintf(fileno(log_file),"WATCHDOG_RESTORE index=%u ok=%d parked=%d\n",i,ok,hold);}}
   if(owner>1&&getppid()==owner)kill(owner,SIGTERM);
  }
  close(p[0]);_exit(normal?0:1);
 }close(p[0]);watch_pipe=p[1];watch_pid=child;return 1;
}
static int save_regs(const char *dir,const char *phase,const struct alloc_regs *r){char path[1200];snprintf(path,sizeof path,"%s/registers_%s.txt",dir,phase);FILE *f=fopen(path,"w");if(!f)return 0;for(unsigned i=0;i<18;i++)fprintf(f,"r%u=0x%08x\n",i,r->r[i]);int ok=fclose(f)==0;return ok;}
static int verify_parked(const char *directory,const char *phase,int queue,int same_buffer){
 struct alloc_regs r;uint32_t saved_lr=0;if(!get_regs(&r))return 0;if(!save_regs(directory,phase,&r))return 0;
 if(r.r[15]==libc_base+0xc5c0c||r.r[15]==libc_base+0xc5c10){if(!read_word(r.r[13]+8,&saved_lr))return 0;}
 fprintf(log_file,"PARKED_REGS pc=0x%08x lr=0x%08x sp=0x%08x r7=%u orig_r0=%u buffer=0x%08x saved_lr=0x%08x\n",r.r[15],r.r[14],r.r[13],r.r[7],r.r[17],r.r[1],saved_lr);fflush(log_file);
 if(!alloc_parked_regs(&r,libc_base,shell_base,queue,saved_lr)||!region_ok(r.r[1],208,1,0)||!region_ok(r.r[13],16,1,0))return 0;
 if(same_buffer&&r.r[1]!=receive_buffer)return 0;
 receive_buffer=r.r[1];original_sp=r.r[13];return 1;
}
static int own_command(const struct alloc_message *sent,const struct alloc_regs *r){struct alloc_message got;uint32_t av[2];return alloc_entry_regs(r,shell_base)&&read_process(receive_buffer,&got,sizeof got)&&!memcmp(sent,&got,sizeof got)&&read_process(r->r[1],av,sizeof av)&&av[0]==receive_buffer+28&&av[1]==receive_buffer+48&&region_ok(r->r[13]-4096,4096,1,0);}
static int at_return(struct alloc_regs *r){return get_regs(r)&&!(r->r[16]&0x20)&&(r->r[15]==shell_base+ALLOC_RETURN||r->r[15]==shell_base+ALLOC_RETURN+4)&&r->r[13]==call_original.r[13];}
static int invoke3(uint32_t fn,uint32_t a,uint32_t b,uint32_t c,unsigned timeout){struct alloc_regs r=alloc_call3(&call_original,fn,a,b,c);trace_error=NULL;
 if(!set_regs(&r)){trace_error="NATIVE_CALL_SETREGS_FAILED";return 0;}native_calls++;api_inflight=1;
 if(!resume()||!wait_stop(timeout,SIGTRAP)||!verify_breakpoint(1))return 0;
 if(!at_return(&r)){trace_error="NATIVE_RETURN_CONTEXT_MISMATCH";return 0;}api_inflight=0;return 1;
}
static int invoke(uint32_t fn,uint32_t arg,unsigned timeout){return invoke3(fn,arg,call_original.r[1],call_original.r[2],timeout);}
static int still_owned(void){uint32_t meta[4];return ownership_verified&&entry(index_owned,meta)&&
 !memcmp(meta,owned_meta,sizeof meta)&&alloc_response(response,response[1],index_owned,baseline,meta)&&region_ok(response[0],ALLOC_BYTES,1,0);}
static int stabilize_worker(const char *directory,int queue){
 pause_ms(100);if(stop_requested||ptrace(PTRACE_ATTACH,tid,0,0))return 0;attached=1;trace_stopped=0;
 trace_phase="idle_attach";if(!wait_stop(3000,SIGSTOP))return 0;
 int ok=verify_parked(directory,"idle",queue,1);
 if(ptrace(PTRACE_DETACH,tid,0,0))return 0;attached=trace_stopped=0;detached_ok=1;
 return ok;
}
static int cleanup_trace(void){int ok=1;
 if(attached&&!trace_stopped){
  /* Signal-delivery stop for this traced worker only. Never abort a native API. */
  trace_phase="cleanup_interrupt";
  if(syscall(SYS_tgkill,pid,tid,SIGTRAP)==0){sig_atomic_t was=stop_requested;stop_requested=0;wait_stop(2000,SIGTRAP);stop_requested=was;
   if(trace_stopped&&stop_info_valid&&stop_signal==SIGTRAP&&stop_info.si_signo==SIGTRAP&&stop_info.si_code==SI_TKILL&&stop_info.si_pid==getpid()){
    native_signal=0;fputs("CLEANUP_INTERRUPT_CONSUMED\n",trace_file);fflush(trace_file);
   }
  }
 }
 if(attached&&trace_stopped){struct alloc_regs r;int own=-1;
  for(unsigned i=0;i<2;i++)if(own_breakpoint(i)||own_instruction_fault(i)){own=(int)i;break;}
  if(own>=0&&native_signal){if(stop_signal!=SIGTRAP)own_faults_suppressed++;
   fprintf(trace_file,"OWN_INSTRUCTION_SIGNAL_CONSUMED index=%d signal=%d code=%d\n",own,stop_signal,stop_info.si_code);fflush(trace_file);native_signal=0;
  }
  if(!get_regs(&r))ok=0;else if(call_boundary_active&&!api_inflight&&!native_signal){
   r=call_original;r.r[0]=0;r.r[15]=shell_base+ALLOC_RETURN;r.r[17]=0xffffffffu;if(!set_regs(&r))ok=0;
  }else{
   if(own>=0){r.r[15]=patches[own].address;if(!set_regs(&r))ok=0;}
  }
 }
 for(unsigned i=0;i<2;i++)if(!restore_patch(i))ok=0;
 code_restored=!patches[0].armed&&!patches[1].armed;
 if(attached){forwarded_signal=(unsigned)native_signal;fprintf(trace_file,"DETACH forward_signal=%u restored=%u\n",forwarded_signal,code_restored);fflush(trace_file);
  if(!trace_stopped||ptrace(PTRACE_DETACH,tid,0,(void*)(uintptr_t)native_signal))ok=0;else{attached=trace_stopped=0;detached_ok=1;call_boundary_active=0;}}
 else if(tid)detached_ok=1;
 if(watch_pipe>=0){char c='D';if(ok&&write(watch_pipe,&c,1)!=1)ok=0;close(watch_pipe);watch_pipe=-1;}
 if(watch_pid>0){int st=0;pid_t done;do{done=waitpid(watch_pid,&st,0);}while(done<0&&errno==EINTR);if(done!=watch_pid||!WIFEXITED(st)||WEXITSTATUS(st))ok=0;watch_pid=-1;}
 return ok;
}
