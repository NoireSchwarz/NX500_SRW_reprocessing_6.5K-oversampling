/* SPDX-License-Identifier: AGPL-3.0-only
 * One 16 KiB request/return inside the existing native shell worker.
 * Original code at two call boundaries is temporarily replaced by ARM BKPT.
 * No new MCB receiver, injected executable stub, pixel or physical-memory writes.
 */
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
static uint32_t cap_base,shell_base,receive_buffer,original_sp,initial_ui;
static uint32_t response[5],index_owned=UINT32_MAX,free_before,free_owned,free_after;
static unsigned native_calls,request_done,ownership_verified,release_done,return_verified;
static unsigned api_inflight,entry_intercepted,code_restored,detached_ok;
static uint64_t started;
static FILE *log_file,*csv;
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
 FILE *f=fopen(out,"r");if(!f)return 0;region_count=mapping_count=0;cap_base=shell_base=0;
 while(fgets(line,sizeof line,f)){
  struct region r={0};char dev[32];unsigned long inode;
  int n=sscanf(line,"%x-%x %4s %x %31s %lu %199s",&r.lo,&r.hi,r.perms,&r.offset,dev,&inode,r.path);
  if(n>=6&&r.lo<r.hi&&region_count<1200)regions[region_count++]=r;
  struct mem_mapping m;if(mem_mapping_line(line,&m)&&mapping_count<64)mappings[mapping_count++]=m;
  if(n==7&&!r.offset&&!strcmp(r.perms,"r-xp")){
   if(!strcmp(r.path,"/usr/lib/libcapture-fw-prod.so")){if(cap_base){fclose(f);return 0;}cap_base=r.lo;}
   if(!strcmp(r.path,"/usr/lib/libshell-command.so")){if(shell_base){fclose(f);return 0;}shell_base=r.lo;}
  }
 }fclose(f);return cap_base&&shell_base&&mapping_count;
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
 p=mmap(NULL,4096,PROT_READ,MAP_SHARED,devfd,(off_t)0x817dc000u);if(p==MAP_FAILED)return 0;root_map=p;return pool_head(&free_before)&&free_before>512;
}
static int snapshot(const char *dir){size_t n=ALLOC_BLOCKS*16u;baseline=malloc(n);baseline_again=malloc(n);if(!baseline||!baseline_again)return 0;
 const volatile unsigned char *t=manager_map+0x908;
 for(int tries=0;tries<5&&!stop_requested;tries++){
  uint32_t a,b,c;if(!pool_head(&a))return 0;
  for(size_t i=0;i<n;i++)((unsigned char*)baseline)[i]=t[i];
  if(!pool_head(&b))return 0;
  for(size_t i=0;i<n;i++)((unsigned char*)baseline_again)[i]=t[i];
  if(!pool_head(&c))return 0;
  if(a==b&&b==c&&!memcmp(baseline,baseline_again,n)){
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
static int find_worker(int queue){char path[100];snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *d=opendir(path);if(!d)return 0;struct dirent *e;int found=0;
 while((e=readdir(d))){int candidate=atoi(e->d_name);if(candidate<=1)continue;char b[64];snprintf(path,sizeof path,"/proc/%d/task/%d/comm",pid,candidate);uint32_t buf,sp;
  if(file_line(path,b,sizeof b)&&!strcmp(b,"shell_di_app")&&receive_syscall(candidate,queue,&buf,&sp)){
   if(found){found=0;break;}found=candidate;receive_buffer=buf;original_sp=sp;
  }
 }closedir(d);return found;
}
static int get_regs(struct alloc_regs *r){return ptrace(PTRACE_GETREGS,tid,0,r)==0;}
static int set_regs(const struct alloc_regs *r){return ptrace(PTRACE_SETREGS,tid,0,(void*)r)==0;}
static int wait_stop(unsigned timeout,int expected_signal){uint64_t end=ns()+(uint64_t)timeout*1000000;
 while(ns()<end&&!stop_requested){int status;pid_t p=waitpid(tid,&status,__WALL|WNOHANG);if(p==tid){if(!WIFSTOPPED(status)){attached=trace_stopped=0;reason="NATIVE_WORKER_EXITED";return 0;}trace_stopped=1;native_signal=WSTOPSIG(status);if(native_signal!=expected_signal){reason="UNEXPECTED_NATIVE_SIGNAL";return 0;}native_signal=0;return 1;}if(p<0&&errno!=EINTR)return 0;pause_ms(10);}return 0;
}
static int resume(void){if(ptrace(PTRACE_CONT,tid,0,0))return 0;trace_stopped=0;return 1;}
static int restore_patch(unsigned i){uint32_t word;if(!patches[i].armed)return 1;if(!read_word(patches[i].address,&word))return 0;
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
static int watchdog_start(void){int p[2],owner=getpid();if(pipe2(p,O_CLOEXEC))return 0;int child=fork();if(child<0){close(p[0]);close(p[1]);return 0;}
 if(!child){close(p[1]);struct pollfd f={p[0],POLLIN|POLLHUP,0};int rc;do{rc=poll(&f,1,45000);}while(rc<0&&errno==EINTR);char c=0;int normal=rc>0&&(f.revents&POLLIN)&&read(p[0],&c,1)==1&&c=='D';
  if(!normal){for(unsigned i=0;i<2;i++){uint32_t word;if(pread(procfd,&word,4,(off_t)patches[i].address)==4&&word==ALLOC_TRAP){int ok=pwrite(procfd,&patches[i].original,4,(off_t)patches[i].address)==4;dprintf(fileno(log_file),"WATCHDOG_RESTORE index=%u ok=%d\n",i,ok);}}if(owner>1&&getppid()==owner)kill(owner,SIGTERM);}
  close(p[0]);_exit(normal?0:1);
 }close(p[0]);watch_pipe=p[1];watch_pid=child;return 1;
}
static int save_regs(const char *dir,const char *phase,const struct alloc_regs *r){char path[1200];snprintf(path,sizeof path,"%s/registers_%s.txt",dir,phase);FILE *f=fopen(path,"w");if(!f)return 0;for(unsigned i=0;i<18;i++)fprintf(f,"r%u=0x%08x\n",i,r->r[i]);int ok=fclose(f)==0;return ok;}
static int own_command(const struct alloc_message *sent,const struct alloc_regs *r){struct alloc_message got;uint32_t av[2];return alloc_entry_regs(r,shell_base)&&read_process(receive_buffer,&got,sizeof got)&&!memcmp(sent,&got,sizeof got)&&read_process(r->r[1],av,sizeof av)&&av[0]==receive_buffer+28&&av[1]==receive_buffer+48&&region_ok(r->r[13]-4096,4096,1,0);}
static int at_return(struct alloc_regs *r){return get_regs(r)&&!(r->r[16]&0x20)&&(r->r[15]==shell_base+ALLOC_RETURN||r->r[15]==shell_base+ALLOC_RETURN+4)&&r->r[13]==call_original.r[13];}
static int invoke(uint32_t fn,uint32_t arg,unsigned timeout){struct alloc_regs r=alloc_call(&call_original,fn,arg);if(!set_regs(&r))return 0;native_calls++;api_inflight=1;if(!resume()||!wait_stop(timeout,SIGTRAP)||!at_return(&r))return 0;api_inflight=0;return 1;}
static int stabilize_worker(unsigned timeout){uint64_t end=ns()+(uint64_t)timeout*1000000;
 while(ns()<end&&!stop_requested){char path[128],line[512];snprintf(path,sizeof path,"/proc/%d/task/%d/syscall",pid,tid);if(file_line(path,line,sizeof line)){unsigned long long no,a[6],sp,pc;if(sscanf(line,"%llu %llx %llx %llx %llx %llx %llx %llx %llx",&no,&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&sp,&pc)==9&&no==302&&a[1]==receive_buffer&&a[2]==208)return 1;}pause_ms(20);}return 0;
}
static int cleanup_trace(void){int ok=1;
 if(attached&&!trace_stopped){
  /* Signal-delivery stop for this traced worker only. Never abort a native API. */
  if(syscall(SYS_tgkill,pid,tid,SIGTRAP)==0){sig_atomic_t was=stop_requested;stop_requested=0;wait_stop(2000,SIGTRAP);stop_requested=was;}
 }
 if(attached&&trace_stopped){struct alloc_regs r;if(!get_regs(&r))ok=0;else if(entry_intercepted&&!api_inflight&&!native_signal){
   r=call_original;r.r[0]=0;r.r[15]=shell_base+ALLOC_RETURN;r.r[17]=0xffffffffu;if(!set_regs(&r))ok=0;
  }else{
   for(unsigned i=0;i<2;i++)if(patches[i].armed&&(r.r[15]==patches[i].address||r.r[15]==patches[i].address+4)){
    r.r[15]=patches[i].address;if(!set_regs(&r))ok=0;
   }
  }
 }
 for(unsigned i=0;i<2;i++)if(!restore_patch(i))ok=0;
 code_restored=!patches[0].armed&&!patches[1].armed;
 if(attached){if(!trace_stopped||ptrace(PTRACE_DETACH,tid,0,(void*)(uintptr_t)native_signal))ok=0;else{attached=trace_stopped=0;detached_ok=1;}}
 else if(tid)detached_ok=1;
 if(watch_pipe>=0){char c='D';if(ok&&write(watch_pipe,&c,1)!=1)ok=0;close(watch_pipe);watch_pipe=-1;}
 if(watch_pid>0){int st=0;pid_t done;do{done=waitpid(watch_pid,&st,0);}while(done<0&&errno==EINTR);if(done!=watch_pid||!WIFEXITED(st)||WEXITSTATUS(st))ok=0;watch_pid=-1;}
 return ok;
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_ALLOCPROBE=0.3.4; ONE_16K_NATIVE_REQUEST_RETURN; TRANSIENT_PTRACE; NO_PIXELS");return 0;}
 if(argc==2&&!strcmp(argv[1],"--clock")){struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);clock_gettime(CLOCK_REALTIME,&b);printf("mono=%lld.%09ld realtime=%lld.%09ld\n",(long long)a.tv_sec,a.tv_nsec,(long long)b.tv_sec,b.tv_nsec);return 0;}
#if !defined(__arm__) && !defined(NXL_ALLOC_SIMULATION)
 (void)argv;fputs("ARM camera required\n",stderr);return 77;
#else
#if defined(__arm__)
 _Static_assert(sizeof(long)==4,"ARM32 ptrace ABI");
#endif
 if(argc!=2||argv[1][0]!='/'||strlen(argv[1])>1000)return 1;
 started=ns();const char *dir=argv[1];char path[1200];snprintf(path,sizeof path,"%s/probe.log",dir);log_file=fopen(path,"w");if(!log_file)return 1;
 int rc=1,queue=-1;struct alloc_regs regs;uint32_t f=0,meta[4],mapped_pa=0,ptr=0;struct alloc_message message;
#define REQUIRE(condition,message) do{if(!(condition)){reason=(message);event(reason);goto finish;}}while(0)
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGALRM,stop);signal(SIGPIPE,SIG_IGN);alarm(40);
 REQUIRE(firmware(),"NX500_1_12_REQUIRED");
 REQUIRE(fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV),"ORIGINAL_BUILD_FINGERPRINT_MISMATCH");
 pid=find_camera();REQUIRE(pid,"CAMERA_PROCESS_NOT_UNIQUE");
 snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,O_RDWR|O_CLOEXEC);REQUIRE(procfd>=0,"PROCESS_MEMORY_READ_WRITE_PERMISSION_REQUIRED");
 REQUIRE(load_maps(dir,"begin")&&app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof alloc_cap_guards[0])&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof alloc_shell_guards[0]),"RUNTIME_CODE_OR_MAP_GUARD_FAILED");
 REQUIRE(read_word(shell_base+0x1e33c+15*8+4,&ptr)&&ptr==shell_base+ALLOC_ENTRY,"NATIVE_APP_DISPATCH_CHANGED");
 REQUIRE(read_word(cap_base+0xb3350,&ptr)&&region_ok(ptr,0x18,1,0),"CAPTURE_CONTEXT_UNAVAILABLE");
 {unsigned char flag=0;REQUIRE(read_process(ptr+0x10,&flag,1)&&flag==1,"NATIVE_REQUEST_SERIALIZATION_NOT_ENABLED");}
 REQUIRE(read_word(cap_base+0xb33dc,&ptr)&&read_word(ptr,&f)&&f==cap_base+0xb0240,"NATIVE_RECEIVER_CONTEXT_UNAVAILABLE");
 REQUIRE(read_word(PB_POINTER_VA,&initial_ui)&&map_metadata(),"PHYSICAL_METADATA_PREFLIGHT_FAILED");
 queue=queue_id();REQUIRE(queue>=0,"NATIVE_COMMAND_QUEUE_BUSY_OR_UNAVAILABLE");
 tid=find_worker(queue);REQUIRE(tid>0,"PARKED_NATIVE_SHELL_WORKER_NOT_FOUND");
 fprintf(log_file,"CONTEXT pid=%d tid=%d cap_base=0x%08x shell_base=0x%08x queue=%d receive_buffer=0x%08x syscall_sp=0x%08x\n",pid,tid,cap_base,shell_base,queue,receive_buffer,original_sp);fflush(log_file);
 REQUIRE(snapshot(dir),"ALLOCATOR_BASELINE_UNSTABLE");
 patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;
 REQUIRE(read_word(patches[0].address,&patches[0].original)&&read_word(patches[1].address,&patches[1].original),"CALL_BOUNDARY_READ_FAILED");
 REQUIRE(patches[0].original==0xe92d4008&&patches[1].original==0xe8bd85f8,"CALL_BOUNDARY_INSTRUCTION_MISMATCH");
 REQUIRE(watchdog_start(),"ROLLBACK_WATCHDOG_UNAVAILABLE");
 snprintf(path,sizeof path,"%s/READY",dir);{FILE *ready=fopen(path,"w");REQUIRE(ready,"READY_CREATE_FAILED");fputs("ONE_16K_NATIVE_REQUEST_RETURN; KEEP_CAMERA_IDLE\n",ready);fclose(ready);}event("READY");pause_ms(2500);
 REQUIRE(!stop_requested&&ptrace(PTRACE_ATTACH,tid,0,0)==0,"NATIVE_WORKER_ATTACH_REFUSED");attached=1;
 REQUIRE(wait_stop(3000,SIGSTOP),"NATIVE_WORKER_ATTACH_TIMEOUT");
 REQUIRE(other_threads_running(),"OTHER_NATIVE_THREADS_STOPPED");
 REQUIRE(arm_patch(0),"ENTRY_BREAKPOINT_NOT_INSTALLED");
 alloc_make_message(&message,(uint32_t)getpid());
 REQUIRE(queue_id()==queue&&msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT)==0,"CONTROLLED_COMMAND_NOT_QUEUED");
 REQUIRE(resume()&&wait_stop(5000,SIGTRAP)&&get_regs(&regs),"CONTROLLED_COMMAND_ENTRY_TIMEOUT");
 REQUIRE(own_command(&message,&regs),"CONTROLLED_COMMAND_CONTEXT_MISMATCH");entry_intercepted=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;
 REQUIRE(save_regs(dir,"entry",&regs)&&restore_patch(0)&&arm_patch(1),"CALL_BOUNDARY_PREPARATION_FAILED");
 snprintf(path,sizeof path,"%s/allocator.csv",dir);csv=fopen(path,"w");REQUIRE(csv,"CSV_CREATE_FAILED");fputs("elapsed_ms,stage,stable,va,pa,index,free_blocks,entry_word0,span_blocks,entry_word2,entry_word3,entry_crc32\n",csv);
 event("REQUEST_BEGIN");REQUIRE(invoke(cap_base+ALLOC_REQUEST,receive_buffer+48,8000),"REQUEST_RESPONSE_TIMEOUT_OR_STOP");
 REQUIRE(get_regs(&regs)&&read_process(receive_buffer+48,response,sizeof response),"REQUEST_RESULT_READ_FAILED");request_done=1;save_regs(dir,"request_return",&regs);
 fprintf(log_file,"REQUEST_RETURN status=0x%08x info=%08x,%08x,%u,%u,%u\n",regs.r[0],response[0],response[1],response[2],response[3],response[4]);fflush(log_file);
 REQUIRE(regs.r[0]==0&&translate(response[0],&mapped_pa,&index_owned)&&entry(index_owned,meta)&&alloc_response(response,mapped_pa,index_owned,baseline,meta),"REQUEST_OWNERSHIP_NOT_CONFIRMED_NO_GUESSED_FREE");
 ownership_verified=1;REQUIRE(pool_head(&free_owned),"POOL_CHANGED_AFTER_REQUEST");row("owned",index_owned);
 for(int i=0;i<10&&!stop_requested;i++){pause_ms(50);row("owned_hold",index_owned);}
 REQUIRE(entry(index_owned,meta)&&alloc_response(response,mapped_pa,index_owned,baseline,meta),"OWNED_ENTRY_CHANGED_NO_GUESSED_FREE");
 event("RELEASE_BEGIN");REQUIRE(invoke(cap_base+ALLOC_RELEASE,response[0],8000),"RELEASE_RETURN_TIMEOUT_OR_STOP");release_done=1;
 REQUIRE(get_regs(&regs),"RELEASE_RESULT_READ_FAILED");save_regs(dir,"release_return",&regs);fprintf(log_file,"RELEASE_RETURN status=0x%08x\n",regs.r[0]);fflush(log_file);
 REQUIRE(regs.r[0]==0,"RELEASE_API_REPORTED_ERROR");
 {unsigned consecutive=0;for(int i=0;i<100&&!stop_requested;i++){row("release_wait",index_owned);if(pool_head(&free_after)&&entry(index_owned,meta)&&!(meta[0]&255u)&&!meta[1]){if(++consecutive==3){return_verified=1;break;}}else consecutive=0;pause_ms(50);}}
 REQUIRE(return_verified,"NATIVE_RELEASE_NOT_CONFIRMED");
 REQUIRE(read_word(PB_POINTER_VA,&f)&&f==initial_ui,"UNEXPECTED_NATIVE_UI_BUFFER_CHANGE");
 /* Only at a verified completed call boundary may registers be restored. */
 regs=call_original;regs.r[0]=0;regs.r[15]=shell_base+ALLOC_RETURN;regs.r[17]=0xffffffffu;
 REQUIRE(set_regs(&regs),"COMPLETED_CALL_REGISTER_RESTORE_FAILED");
 REQUIRE(cleanup_trace(),"CALL_BOUNDARY_RESTORE_OR_DETACH_FAILED");
 REQUIRE(stabilize_worker(3000),"NATIVE_WORKER_DID_NOT_RETURN_TO_RECEIVE");
 reason="OWN_REQUEST_AND_RETURN_VERIFIED";rc=0;
finish:
 alarm(0);if(!cleanup_trace()&&rc==0){rc=1;reason="CLEANUP_INCOMPLETE";}
 if(csv){if(fflush(csv)||fsync(fileno(csv)))rc=1;if(fclose(csv))rc=1;csv=NULL;}
 if(pid>0&&procfd>=0)load_maps(dir,"end");
 snprintf(path,sizeof path,"%s/result.json",dir);FILE *result=fopen(path,"w");
 if(result){fprintf(result,"{\n\"format\":\"NXALLOC1\",\"version\":\"0.3.4\",\"exit\":%d,\"reason\":\"%s\",\n\"pid\":%d,\"native_worker_tid\":%d,\"requested_bytes\":%u,\"native_calls\":%u,\n\"request_returned\":%u,\"ownership_verified\":%u,\"release_returned\":%u,\"allocator_return_verified\":%u,\n\"entry_intercepted\":%u,\"api_inflight_at_exit\":%u,\"original_code_restored\":%u,\"detached\":%u,\n\"va\":%u,\"pa\":%u,\"allocator_index\":%u,\"free_before\":%u,\"free_owned\":%u,\"free_after\":%u,\n\"pixel_reads\":0,\"pixel_writes\":0,\"physical_writes\":0,\"isp_jobs\":0,\"new_mcb_receivers\":0,\"elapsed_ms\":%u\n}\n",rc,reason,pid,tid,ALLOC_BYTES,native_calls,request_done,ownership_verified,release_done,return_verified,entry_intercepted,api_inflight,code_restored,detached_ok,response[0],response[1],index_owned,free_before,free_owned,free_after,ms());if(fclose(result))rc=1;}else rc=1;
 event(reason);fprintf(log_file,"RESULT exit=%d native_calls=%u owned=%u returned=%u restored=%u detached=%u\n",rc,native_calls,ownership_verified,return_verified,code_restored,detached_ok);fclose(log_file);
 if(manager_map)munmap((void*)manager_map,0xb2000);if(root_map)munmap((void*)root_map,4096);if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);free(baseline);free(baseline_again);return rc;
#endif
}
