/* SPDX-License-Identifier: AGPL-3.0-only
 * One owned 96 MiB allocation and a synthetic native 3DLUT/JPEG pipeline.
 * Original code at two call boundaries is temporarily replaced by Linux ARM UDF breakpoint.
 * No new MCB receiver, executable stub or photo pixels; /dev/mem stays read-only.
 * The separate native worker writes bounded EP registers through the EP device.
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
#include "../../common/bank_capacity.h"
#include "allocator_stability.h"
#include "alloc_guards.h"
#include "ep_sync_core.h"
#include "ep_kernel_guards.h"
#include "bitb_job.h"
#include "bitb_payload.h"
#include "shared_memory.h"
#include "pipeline_proof.h"
#include "p12_steps.h"
#include "pipeline_timing.h"
#include "h3dl_probe.h"
#include "h3dl_rtos_guards.h"
#include "p10_owner_guards.h"
#include "source_owner.h"
#ifdef NXL_ALLOC_SIMULATION
#include "bitb_native_guards.h"
#endif
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
static unsigned attach_attempted,shell_message_queued;
static unsigned api_inflight,entry_intercepted,code_restored,detached_ok;
static unsigned cpu_roundtrips,cache_wrapper_returns,buffer_test_started,buffer_test_passed,buffer_bytes_written;
static uint32_t pattern_nonce;
static const char *buffer_error="";
static unsigned ep_context_observed,ep_fd_verified,ep_queries,ep_matches,ep_contract_verified;
static uint32_t ep_open_count;
static int ep_sma_fd=-1;
static const char *ep_error="";
static unsigned sync_kernel_guards_verified,sync_samples,sync_samples_quiet,sync_preflight_verified;
static unsigned sync_ep_fd_verified,sync_register_maps_verified;
static const char *sync_error="";
struct p7_watch {volatile uint32_t camera_paused,job_pid,worker_done;};
static struct p7_watch *p7_shared;
static struct bitb_result p7_result;
static unsigned p7_peer_scans,p7_peer_pids,p7_resume_window;
static int p7_task_states(int want_stopped,unsigned *count);
static char p7_peer_audit[65536];
static size_t p7_peer_audit_bytes;
static unsigned p7_worker_ready,p7_go_sent,p7_worker_exited,p7_worker_exit,p7_result_valid,p7_safe_to_release,p7_quarantined,p7_verified,p7_rehearsal_verified,p7_peer_scans,p7_peer_pids,p7_pauses,p7_resumes,p7_resumed_verified;
static const char *p7_error="COPY_NOT_STARTED";
static void p7_watch_recover(void);

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
static unsigned shared_borrowed,shared_claimed,shared_completed,shared_release_only;
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
static const char *allocator_snapshot_reason="ALLOCATOR_SNAPSHOT_NOT_STARTED";
static int snapshot(const char *dir){
 size_t bytes=ALLOC_BLOCKS*16u;baseline=malloc(bytes);baseline_again=malloc(bytes);unsigned attempts=0,changed=0,first=UINT32_MAX,largest=0;uint32_t a=0,b=0,c=0,crc_a=0,crc_b=0;int stable=0,eligible=0;
 uint64_t begin=ns(),deadline=begin+5000000000ULL;char path[1200];snprintf(path,sizeof path,"%s/allocator-snapshot.csv",dir);FILE *audit=fopen(path,"w");
 allocator_snapshot_reason="ALLOCATOR_BASELINE_UNSTABLE_TIMEOUT";
 if(!baseline||!baseline_again){allocator_snapshot_reason="ALLOCATOR_BASELINE_HOST_MALLOC_FAILED";goto save;}
 if(!audit){allocator_snapshot_reason="ALLOCATOR_BASELINE_AUDIT_OPEN_FAILED";goto save;}
 fputs("attempt,elapsed_ms,free_a,free_b,free_c,table_crc_a,table_crc_b,changed_entries,first_changed_entry,stable\n",audit);
 const volatile unsigned char *table=manager_map+0x908;
 while(!stop_requested&&ns()<deadline&&attempts<64){
  attempts++;
  if(!pool_head(&a)){allocator_snapshot_reason="ALLOCATOR_HEADER_READ_A_FAILED";break;}
  for(size_t i=0;i<bytes;i++)((unsigned char*)baseline)[i]=table[i];
  if(!pool_head(&b)){allocator_snapshot_reason="ALLOCATOR_HEADER_READ_B_FAILED";break;}
  for(size_t i=0;i<bytes;i++)((unsigned char*)baseline_again)[i]=table[i];
  if(!pool_head(&c)){allocator_snapshot_reason="ALLOCATOR_HEADER_READ_C_FAILED";break;}
  crc_a=mem_crc(baseline,bytes);crc_b=mem_crc(baseline_again,bytes);stable=alloc_snapshot_equal(a,b,c,baseline,baseline_again,ALLOC_BLOCKS,&changed,&first);
  fprintf(audit,"%u,%u,%u,%u,%u,%08x,%08x,%u,%u,%u\n",attempts,(unsigned)((ns()-begin)/1000000ULL),a,b,c,crc_a,crc_b,changed,first,stable);fflush(audit);
  if(stable){allocator_snapshot_reason="ALLOCATOR_BASELINE_STABLE";break;}pause_ms(50);
 }
 if(stop_requested){stable=0;allocator_snapshot_reason="ALLOCATOR_BASELINE_CANCELLED";}
 if(stable){
  eligible=alloc_largest_free_span(baseline,&largest)&&(shared_borrowed||largest>=ALLOC_SPAN_BLOCKS);
  if(!eligible)allocator_snapshot_reason="ALLOCATOR_NO_CONTIGUOUS_EXTENT";
  fprintf(log_file,"ALLOCATION_CAPACITY required_blocks=%u contiguous_free_blocks=%u reported_free=%u eligible=%d\n",ALLOC_SPAN_BLOCKS,largest,c,eligible);fflush(log_file);
  free_before=c;snprintf(path,sizeof path,"%s/allocator_before.bin",dir);FILE *f=fopen(path,"wb");int ok=f&&fwrite(baseline,1,bytes,f)==bytes;
  if(f){if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;}if(!ok){stable=0;allocator_snapshot_reason="ALLOCATOR_BASELINE_SAVE_FAILED";}
  fprintf(log_file,"BASELINE blocks=%u free=%u crc32=%08x attempts=%u\n",ALLOC_BLOCKS,c,crc_a,attempts);fflush(log_file);
 }
 save:
 if(audit){int written=!fflush(audit)&&!fsync(fileno(audit));if(fclose(audit))written=0;if(!written){stable=0;allocator_snapshot_reason="ALLOCATOR_AUDIT_SAVE_FAILED";}}
 snprintf(path,sizeof path,"%s/allocator-snapshot.json",dir);FILE *f=fopen(path,"w");
 if(!f){allocator_snapshot_reason="ALLOCATOR_REPORT_OPEN_FAILED";return 0;}fprintf(f,"{\"format\":\"NX52ALLOCATOR1\",\"read_only\":true,\"native_calls\":%u,\"stable\":%s,\"reason\":\"%s\",\"attempts\":%u,\"elapsed_ms\":%u,\"wait_limit_ms\":5000,\"free_a\":%u,\"free_b\":%u,\"free_c\":%u,\"table_crc_a\":%u,\"table_crc_b\":%u,\"changed_entries\":%u,\"first_changed_entry\":%u,\"contiguous_free_blocks\":%u,\"required_blocks\":%u,\"allocation_eligible\":%s}\n",native_calls,stable?"true":"false",allocator_snapshot_reason,attempts,(unsigned)((ns()-begin)/1000000ULL),a,b,c,crc_a,crc_b,changed,first,largest,ALLOC_SPAN_BLOCKS,eligible?"true":"false");
 int saved=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))saved=0;if(!saved)allocator_snapshot_reason="ALLOCATOR_REPORT_SAVE_FAILED";return stable&&eligible&&saved;
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
   if(expected_signal==SIGTRAP&&p7_resume_window&&p7_shared&&!p7_shared->camera_paused&&stop_signal==SIGSTOP&&!stop_info_valid&&info_errno==EINVAL&&!((unsigned)status>>16)){
    unsigned count=0;if(p7_task_states(0,&count)){
     native_signal=0;fputs("OWN_COPY_GROUP_STOP_RESOLVED\n",trace_file);fflush(trace_file);
     if(ptrace(PTRACE_CONT,tid,0,0)){trace_error="COPY_GROUP_STOP_CONTINUE_FAILED";return 0;}trace_stopped=0;continue;
    }
   }
   if(expected_signal==SIGTRAP&&p7_resumes&&p7_shared&&!p7_shared->camera_paused&&stop_info_valid&&
      (stop_signal==SIGSTOP||stop_signal==SIGCONT)&&stop_info.si_signo==stop_signal&&stop_info.si_code==SI_USER&&stop_info.si_pid==getpid()&&!((unsigned)status>>16)){
    native_signal=0;fputs("OWN_COPY_PAUSE_SIGNAL_CONSUMED\n",trace_file);fflush(trace_file);
    if(ptrace(PTRACE_CONT,tid,0,0)){trace_error="COPY_PAUSE_SIGNAL_CONTINUE_FAILED";return 0;}trace_stopped=0;continue;
   }
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
static int watchdog_start(void){
 p7_shared=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);if((void*)p7_shared==MAP_FAILED){p7_shared=NULL;return 0;}
 int p[2],owner=getpid();if(pipe2(p,O_CLOEXEC))return 0;int child=fork();if(child<0){close(p[0]);close(p[1]);return 0;}
 if(!child){close(p[1]);struct pollfd f={p[0],POLLIN|POLLHUP,0};int rc;do{rc=poll(&f,1,P7_WATCHDOG_MS);}while(rc<0&&errno==EINTR);char c=0;int normal=rc>0&&(f.revents&POLLIN)&&read(p[0],&c,1)==1&&(c=='D'||c=='P');
  if(!normal){for(unsigned i=0;i<2;i++){uint32_t word;if(pread(procfd,&word,4,(off_t)patches[i].address)==4&&word==ALLOC_TRAP){int ok=pwrite(procfd,&patches[i].original,4,(off_t)patches[i].address)==4;dprintf(fileno(log_file),"WATCHDOG_RESTORE index=%u ok=%d\n",i,ok);}}p7_watch_recover();if(owner>1&&getppid()==owner)kill(owner,SIGTERM);}
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
 if(!at_return(&r)){trace_error="NATIVE_RETURN_CONTEXT_MISMATCH";return 0;}api_inflight=0;p7_resume_window=0;return 1;
}
static int invoke4(uint32_t fn,uint32_t a,uint32_t b,uint32_t c,uint32_t d,unsigned timeout){struct alloc_regs r=alloc_call3(&call_original,fn,a,b,c);r.r[3]=d;trace_error=NULL;
 if(!set_regs(&r)){trace_error="NATIVE_CALL_SETREGS_FAILED";return 0;}native_calls++;api_inflight=1;
 if(!resume()||!wait_stop(timeout,SIGTRAP)||!verify_breakpoint(1))return 0;
 if(!at_return(&r)){trace_error="NATIVE_RETURN_CONTEXT_MISMATCH";return 0;}api_inflight=0;p7_resume_window=0;return 1;
}
static int invoke(uint32_t fn,uint32_t arg,unsigned timeout){return invoke3(fn,arg,call_original.r[1],call_original.r[2],timeout);}
static int still_owned(void){uint32_t meta[4],pa=0,index=0;
 if(!ownership_verified||!entry(index_owned,meta)||memcmp(meta,owned_meta,sizeof meta)||!region_ok(response[0],ALLOC_BYTES,1,0))return 0;
 if(shared_borrowed)return response[2]==SW_BYTES&&response[3]==2&&translate(response[0],&pa,&index)&&pa==response[1]&&index==index_owned;
 return baseline&&alloc_response(response,response[1],index_owned,baseline,meta);
}
static int sample_file(const char *dir,unsigned round,const char *name,const unsigned char *bytes){
 char path[1200];snprintf(path,sizeof path,"%s/sample_%u_%s.bin",dir,round,name);FILE *f=fopen(path,"wb");if(!f)return 0;
 int ok=fwrite(bytes,1,ALLOC_SAMPLE_BYTES,f)==ALLOC_SAMPLE_BYTES;if(fclose(f))ok=0;return ok;
}
static int exercise_buffer(const char *dir,uint32_t nonce){
 static const char *phases[2][3]={{"write_head","cache_head","read_head"},{"write_tail","cache_tail","read_tail"}};
 unsigned char expected[ALLOC_SAMPLE_BYTES],observed[ALLOC_SAMPLE_BYTES],poison[ALLOC_SAMPLE_BYTES];memset(poison,0,sizeof poison);
 const uint32_t scratch=receive_buffer+48;buffer_test_started=1;pattern_nonce=nonce;trace_error=NULL;
#define BUFFER_REQUIRE(condition,message) do{if(!(condition)){buffer_error=trace_error?trace_error:(message);event(buffer_error);return 0;}}while(0)
 for(unsigned round=0;round<2;round++){
  uint32_t offset=round?ALLOC_BYTES-ALLOC_SAMPLE_BYTES:0,address=0;struct alloc_regs r;
  BUFFER_REQUIRE(!stop_requested&&still_owned()&&alloc_sample_range(response[0],offset,ALLOC_SAMPLE_BYTES,&address),"BUFFER_OWNERSHIP_OR_RANGE_CHANGED");
  BUFFER_REQUIRE(region_ok(scratch,ALLOC_SAMPLE_BYTES,1,0),"SCRATCH_RANGE_UNAVAILABLE");
  alloc_pattern(expected,nonce,round);BUFFER_REQUIRE(sample_file(dir,round,"expected",expected),"EXPECTED_SAMPLE_SAVE_FAILED");
  BUFFER_REQUIRE(pwrite(procfd,expected,sizeof expected,(off_t)scratch)==sizeof expected&&read_process(scratch,observed,sizeof observed)&&!memcmp(expected,observed,sizeof expected),"SCRATCH_PATTERN_WRITE_FAILED");
  trace_phase=phases[round][0];BUFFER_REQUIRE(invoke3(libc_base+ALLOC_MEMCPY,address,scratch,ALLOC_SAMPLE_BYTES,3000),"NATIVE_BUFFER_WRITE_FAILED");buffer_bytes_written+=ALLOC_SAMPLE_BYTES;
  BUFFER_REQUIRE(get_regs(&r)&&r.r[0]==address,"NATIVE_MEMCPY_WRITE_RETURN_MISMATCH");
  BUFFER_REQUIRE(still_owned(),"BUFFER_OWNERSHIP_CHANGED_AFTER_WRITE");
  trace_phase=phases[round][1];BUFFER_REQUIRE(invoke3(cap_base+ALLOC_FLUSH,address,ALLOC_SAMPLE_BYTES,0,3000),"CACHE_WRAPPER_DID_NOT_RETURN");cache_wrapper_returns++;
  BUFFER_REQUIRE(get_regs(&r),"CACHE_RETURN_REGISTERS_UNAVAILABLE");
  fprintf(log_file,"CACHE_WRAPPER_RETURN round=%u va=0x%08x bytes=%u raw_r0=0x%08x backend_ack=NOT_VERIFIED\n",round,address,ALLOC_SAMPLE_BYTES,r.r[0]);fflush(log_file);
  /* The pinned valid-argument path propagates the bridge send return word.
   * Require its observed normal value; even zero is not a completion fence. */
  BUFFER_REQUIRE(r.r[0]==0,"CACHE_SEND_RETURN_NOT_CONFIRMED");
  /* A delay is not a completion fence. No ISP coherence claim is made. */
  pause_ms(100);BUFFER_REQUIRE(!stop_requested&&still_owned(),"BUFFER_OWNERSHIP_CHANGED_AFTER_CACHE_CALL");
  BUFFER_REQUIRE(pwrite(procfd,poison,sizeof poison,(off_t)scratch)==sizeof poison&&read_process(scratch,observed,sizeof observed)&&!memcmp(poison,observed,sizeof poison),"SCRATCH_POISON_WRITE_FAILED");
  trace_phase=phases[round][2];BUFFER_REQUIRE(invoke3(libc_base+ALLOC_MEMCPY,scratch,address,ALLOC_SAMPLE_BYTES,3000),"NATIVE_BUFFER_READBACK_FAILED");
  BUFFER_REQUIRE(get_regs(&r)&&r.r[0]==scratch&&read_process(scratch,observed,sizeof observed),"NATIVE_READBACK_RESULT_UNAVAILABLE");
  BUFFER_REQUIRE(sample_file(dir,round,"observed",observed),"OBSERVED_SAMPLE_SAVE_FAILED");
  fprintf(log_file,"BUFFER_SAMPLE round=%u offset=%u bytes=%u expected_crc32=%08x observed_crc32=%08x\n",round,offset,ALLOC_SAMPLE_BYTES,mem_crc(expected,sizeof expected),mem_crc(observed,sizeof observed));fflush(log_file);
  BUFFER_REQUIRE(!memcmp(expected,observed,sizeof expected)&&still_owned(),"BUFFER_COPY_READBACK_MISMATCH");cpu_roundtrips++;row(phases[round][2],index_owned);
 }
 buffer_test_passed=1;return 1;
#undef BUFFER_REQUIRE
}
static int sma_target(char target[256]){
 char path[128];snprintf(path,sizeof path,"/proc/%d/fd/%d",pid,ep_sma_fd);
 ssize_t n=readlink(path,target,255);if(n<0||n>=255){target[0]=0;return 0;}target[n]=0;return !strcmp(target,"/dev/d5_sma");
}
static int ep_preflight(const char *dir){
 static const uint32_t offsets[4]={0,EP_TEST_Y_BYTES,8192,8192+EP_TEST_Y_BYTES};
 static const char *phases[4]={"ep_source_y","ep_source_c","ep_target_y","ep_target_c"};
 uint32_t count_before=0,count_after=0,fd=0;struct alloc_regs r;struct ep_candidate candidates[2];char path[1200],target[256]={0};FILE *context=NULL,*table=NULL;
 int ok=0;trace_error=NULL;
#define EP_REQUIRE(condition,message) do{if(!(condition)){ep_error=trace_error?trace_error:(message);event(ep_error);goto ep_finish;}}while(0)
 EP_REQUIRE(still_owned()&&ep_make_candidates(response[0],response[1],candidates),"EP_CANDIDATE_RANGE_UNAVAILABLE");
 snprintf(path,sizeof path,"%s/ep_candidates.bin",dir);FILE *saved=fopen(path,"wb");EP_REQUIRE(saved,"EP_CANDIDATE_SAVE_FAILED");
 int saved_ok=fwrite(candidates,1,sizeof candidates,saved)==sizeof candidates;if(fclose(saved))saved_ok=0;EP_REQUIRE(saved_ok,"EP_CANDIDATE_SAVE_FAILED");
 snprintf(path,sizeof path,"%s/ep_context.txt",dir);context=fopen(path,"w");EP_REQUIRE(context,"EP_CONTEXT_SAVE_FAILED");
 EP_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count_before)&&read_word(udd_base+EP_SMA_FD_DATA,&fd),"EP_CONTEXT_MEMORY_UNAVAILABLE");ep_sma_fd=(int32_t)fd;
 trace_phase="ep_open_count";EP_REQUIRE(invoke(udd_base+EP_OPEN_COUNT_FN,0,3000)&&get_regs(&r),"EP_OPEN_COUNT_CALL_FAILED");ep_open_count=r.r[0];
 EP_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count_after),"EP_OPEN_COUNT_READ_FAILED");
 fprintf(context,"udd_base=0x%08x tint_base=0x%08x type_base=0x%08x\nopen_count_before=%u native_return=%u after=%u\nsma_fd=%d\ncandidate_bytes=%u hardware_jobs=0 module_open_calls=0\n",udd_base,tint_base,type_base,count_before,ep_open_count,count_after,ep_sma_fd,(unsigned)sizeof candidates);fflush(context);
 EP_REQUIRE(count_before==ep_open_count&&ep_open_count==count_after,"EP_OPEN_COUNT_UNSTABLE");ep_context_observed=1;
 EP_REQUIRE(ep_sma_fd>=0,"EP_SMA_CONTEXT_NOT_OPEN");ep_fd_verified=sma_target(target);
 fprintf(context,"sma_target=%s verified=%u\n",target,ep_fd_verified);fflush(context);EP_REQUIRE(ep_fd_verified,"EP_SMA_FD_TARGET_UNCONFIRMED");
 snprintf(path,sizeof path,"%s/ep_preflight.csv",dir);table=fopen(path,"w");EP_REQUIRE(table,"EP_ADDRESS_TABLE_CREATE_FAILED");
 fputs("plane,offset,va,cap_expected_pa,sma_return_pa,match\n",table);
 for(unsigned i=0;i<4;i++){
  EP_REQUIRE(!stop_requested&&still_owned()&&read_word(udd_base+EP_SMA_FD_DATA,&fd)&&fd==(uint32_t)ep_sma_fd&&sma_target(target),"EP_CONTEXT_CHANGED_DURING_QUERY");
  trace_phase=phases[i];EP_REQUIRE(invoke(udd_base+EP_SMA_VTOP_FN,response[0]+offsets[i],3000)&&get_regs(&r),"EP_SMA_QUERY_DID_NOT_RETURN");ep_queries++;
  unsigned match=r.r[0]==response[1]+offsets[i];ep_matches+=match;
  fprintf(table,"%s,%u,0x%08x,0x%08x,0x%08x,%u\n",phases[i],offsets[i],response[0]+offsets[i],response[1]+offsets[i],r.r[0],match);fflush(table);
  fprintf(log_file,"EP_ADDRESS plane=%s va=0x%08x cap_pa=0x%08x sma_pa=0x%08x match=%u\n",phases[i],response[0]+offsets[i],response[1]+offsets[i],r.r[0],match);fflush(log_file);
 }
 EP_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count_after)&&count_after==ep_open_count&&still_owned(),"EP_CONTEXT_CHANGED_AFTER_QUERY");
 EP_REQUIRE(ep_queries==4&&ep_matches==4,"EP_SMA_TRANSLATION_MISMATCH");ep_contract_verified=1;ok=1;
ep_finish:
 if(table&&fclose(table)){ok=0;ep_error="EP_ADDRESS_TABLE_SAVE_FAILED";}
 if(!ok)ep_contract_verified=0;
 if(context){fprintf(context,"contract_verified=%u error=%s\n",ep_contract_verified,ep_error);if(fclose(context)){ok=0;ep_contract_verified=0;ep_error="EP_CONTEXT_SAVE_FAILED";}}
 return ok;
#undef EP_REQUIRE
}

static int ep_kernel_read(uint32_t va,void *data,size_t bytes){
 if(va<0xc0008000u||(uint64_t)va+bytes>0xc0800000u||!bytes)return 0;
 return pread(devfd,data,bytes,(off_t)(va-EP_KERNEL_DELTA))==(ssize_t)bytes;
}
static int ep_device_target(int fd,char target[256]){
 char path[128];if(fd<0)return 0;snprintf(path,sizeof path,"/proc/%d/fd/%d",pid,fd);
 ssize_t n=readlink(path,target,255);if(n<0||n>=255)return 0;target[n]=0;return !strcmp(target,"/dev/drime5_ep");
}
static int ep_register_mapping(uint32_t va,uint32_t pa,uint32_t bytes){
 for(unsigned i=0;i<region_count;i++){
  const struct region *r=regions+i;
  if(!strcmp(r->path,"/dev/drime5_ep")&&r->perms[0]=='r'&&r->perms[1]=='w'&&r->perms[3]=='s'&&
     va>=r->lo&&(uint64_t)va+bytes<=r->hi&&(uint64_t)r->offset+va-r->lo==pa)return 1;
 }
 return 0;
}
static int ep_sync_preflight(const char *dir){
 static const char kernel_version[]="Linux version 3.5.0 (hs2704.sung@SWDA7604) (gcc version 4.4.1 (Sourcery G++ Lite 2010q1-202) ) #7 PREEMPT Wed Jun 3 14:40:29 KST 2015";
 unsigned char raw[EP_KERNEL_STATE_BYTES];uint32_t mutex[3],fd=0,sma=0,count=0,ptrs[10],first_fd=0,first_ptrs[10];
 struct ep_sync_observation observed;char path[1200],target[256]={0},version[512];FILE *table=NULL,*binary=NULL,*context=NULL,*guard_file=NULL;int ok=0;
#define SYNC_REQUIRE(condition,message) do{if(!(condition)){sync_error=(message);event(sync_error);goto sync_finish;}}while(0)
 snprintf(path,sizeof path,"%s/ep_sync_context.txt",dir);context=fopen(path,"w");SYNC_REQUIRE(context,"EP_SYNC_CONTEXT_CREATE_FAILED");
 SYNC_REQUIRE(file_line("/proc/version",version,sizeof version)&&!strcmp(version,kernel_version),"EP_KERNEL_VERSION_UNCONFIRMED");
 fprintf(context,"kernel=%s\nstate_va=0x%08x state_bytes=%u mutex_va=0x%08x selector=%u\nphysical_read_only=1 kernel_writes=0 register_reads=0 register_writes=0 lock_calls=0 irq_wait_calls=0 hardware_jobs=0\n",version,EP_KERNEL_STATE_VA,EP_KERNEL_STATE_BYTES,EP_KERNEL_MUTEX_VA,EP_SYNC_SELECTOR);fflush(context);
 snprintf(path,sizeof path,"%s/ep_kernel_guards.bin",dir);guard_file=fopen(path,"wb");SYNC_REQUIRE(guard_file,"EP_KERNEL_GUARD_SAVE_FAILED");
 for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof ep_kernel_guards[0];i++){
  uint32_t words[8];SYNC_REQUIRE(ep_kernel_read(ep_kernel_guards[i].va,words,sizeof words),"EP_KERNEL_CODE_GUARD_FAILED");
  SYNC_REQUIRE(fwrite(&ep_kernel_guards[i].va,1,4,guard_file)==4&&fwrite(words,1,sizeof words,guard_file)==sizeof words,"EP_KERNEL_GUARD_SAVE_FAILED");
  SYNC_REQUIRE(!memcmp(words,ep_kernel_guards[i].words,sizeof words),"EP_KERNEL_CODE_GUARD_FAILED");sync_kernel_guards_verified++;
 }
 {int good=fclose(guard_file)==0;guard_file=NULL;SYNC_REQUIRE(good,"EP_KERNEL_GUARD_SAVE_FAILED");}
 snprintf(path,sizeof path,"%s/interrupts_before.txt",dir);save_copy("/proc/interrupts",path);
 snprintf(path,sizeof path,"%s/ep_sync.csv",dir);table=fopen(path,"w");SYNC_REQUIRE(table,"EP_SYNC_TABLE_CREATE_FAILED");
 fputs("sample,elapsed_ms,ep_fd,open_count,acc_enabled_mask,completion_done,wait_next,wait_prev,mutex_count,mutex_next,mutex_prev,top_va,bitb_va,quiet,error\n",table);
 snprintf(path,sizeof path,"%s/ep_sync_samples.bin",dir);binary=fopen(path,"wb");SYNC_REQUIRE(binary,"EP_SYNC_BINARY_CREATE_FAILED");
 {uint32_t header[8]={0x36535045u,1,EP_SYNC_SAMPLES,EP_KERNEL_STATE_BYTES,EP_SYNC_SELECTOR,EP_KERNEL_STATE_VA,EP_KERNEL_MUTEX_VA,1408};SYNC_REQUIRE(fwrite(header,1,sizeof header,binary)==sizeof header,"EP_SYNC_BINARY_WRITE_FAILED");}
 for(unsigned i=0;i<EP_SYNC_SAMPLES;i++){
  SYNC_REQUIRE(!stop_requested&&still_owned(),"EP_SYNC_OWNERSHIP_OR_STOP_CHANGED");
  SYNC_REQUIRE(read_word(udd_base+EP_DEVICE_FD_DATA,&fd)&&read_word(udd_base+EP_SMA_FD_DATA,&sma)&&
    read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&read_process(udd_base+EP_REGISTER_POINTERS_DATA,ptrs,sizeof ptrs),"EP_SYNC_APP_CONTEXT_READ_FAILED");
  fprintf(context,"sample=%u ep_fd=%d sma_fd=%d open_count=%u top_va=0x%08x bitb_va=0x%08x\n",i,(int32_t)fd,(int32_t)sma,count,ptrs[0],ptrs[3]);fflush(context);
  SYNC_REQUIRE((int32_t)fd>=0&&ep_device_target((int32_t)fd,target),"EP_DEVICE_FD_TARGET_UNCONFIRMED");sync_ep_fd_verified=1;
  SYNC_REQUIRE(sma==(uint32_t)ep_sma_fd&&sma_target(target)&&count==ep_open_count,"EP_SYNC_APP_CONTEXT_CHANGED");
  if(!i){first_fd=fd;memcpy(first_ptrs,ptrs,sizeof ptrs);}else SYNC_REQUIRE(fd==first_fd&&!memcmp(first_ptrs,ptrs,sizeof ptrs),"EP_SYNC_APP_CONTEXT_CHANGED");
  SYNC_REQUIRE(ep_kernel_read(EP_KERNEL_STATE_VA,raw,sizeof raw)&&ep_kernel_read(EP_KERNEL_MUTEX_VA,mutex,sizeof mutex),"EP_SYNC_KERNEL_METADATA_READ_FAILED");
  ep_sync_decode(raw,mutex,&observed);
  unsigned maps_ok=ep_register_mapping(ptrs[0],observed.top_pa,observed.top_bytes)&&ep_register_mapping(ptrs[3],observed.bitb_pa,observed.bitb_bytes);
  sync_register_maps_verified=maps_ok&&(!i||sync_register_maps_verified);
  const char *error=ep_sync_observation_error(&observed);if(!error&&!maps_ok)error="EP_REGISTER_MAPPING_UNCONFIRMED";
  unsigned quiet=error==NULL;sync_samples++;sync_samples_quiet+=quiet;
  uint32_t stamp=ms(),record[8]={i,stamp,fd,sma,count,ptrs[0],ptrs[3],0};
  SYNC_REQUIRE(fwrite(record,1,sizeof record,binary)==sizeof record&&fwrite(mutex,1,sizeof mutex,binary)==sizeof mutex&&fwrite(raw,1,sizeof raw,binary)==sizeof raw,"EP_SYNC_BINARY_WRITE_FAILED");
  fprintf(table,"%u,%u,%u,%u,0x%08x,%u,0x%08x,0x%08x,%d,0x%08x,0x%08x,0x%08x,0x%08x,%u,%s\n",i,stamp,fd,count,observed.acc_mask,observed.completion[0],observed.completion[1],observed.completion[2],(int32_t)mutex[0],mutex[1],mutex[2],ptrs[0],ptrs[3],quiet,error?error:"");
  SYNC_REQUIRE(!fflush(table)&&!fflush(binary),"EP_SYNC_SAMPLE_SAVE_FAILED");
  /* Preserve all samples even when a lane is already enabled or a completion
   * is pending. Do not reset a queue, wait for an IRQ or try to take a lock. */
  if(error&&!sync_error[0])sync_error=error;
  if(i+1<EP_SYNC_SAMPLES)pause_ms(100);
 }
 SYNC_REQUIRE(sync_samples==EP_SYNC_SAMPLES&&sync_samples_quiet==EP_SYNC_SAMPLES,sync_error[0]?sync_error:"EP_SYNC_SAMPLE_COUNT_MISMATCH");
 sync_preflight_verified=1;ok=1;
sync_finish:
 if(guard_file&&fclose(guard_file)){ok=0;sync_error="EP_KERNEL_GUARD_SAVE_FAILED";}
 snprintf(path,sizeof path,"%s/interrupts_after.txt",dir);save_copy("/proc/interrupts",path);
 if(table&&fclose(table)){ok=0;sync_error="EP_SYNC_TABLE_SAVE_FAILED";}
 if(binary&&fclose(binary)){ok=0;sync_error="EP_SYNC_BINARY_SAVE_FAILED";}
 if(!ok)sync_preflight_verified=0;
 if(context){fprintf(context,"kernel_guards=%u samples=%u quiet_samples=%u metadata_preflight_verified=%u\nexclusive_ownership_verified=0 hardware_completion_verified=0\nerror=%s\n",sync_kernel_guards_verified,sync_samples,sync_samples_quiet,sync_preflight_verified,sync_error);if(fclose(context)){ok=0;sync_preflight_verified=0;sync_error="EP_SYNC_CONTEXT_SAVE_FAILED";}}
 return ok;
#undef SYNC_REQUIRE
}

#include "shared_workspace_controller.inc"
#include "bitb_control.inc"

static int stabilize_worker(const char *directory,int queue){
 pause_ms(100);if(stop_requested||ptrace(PTRACE_ATTACH,tid,0,0))return 0;attached=1;trace_stopped=0;
 trace_phase="idle_attach";if(!wait_stop(3000,SIGSTOP))return 0;
 int ok=verify_parked(directory,"idle",queue,1);
 if(ptrace(PTRACE_DETACH,tid,0,0))return 0;attached=trace_stopped=0;detached_ok=1;
 return ok;
}
static int cleanup_trace(void){int ok=1;
 if(p7_quarantined){
  /* No SIGCONT, native cleanup call or memory free is permitted here. The
   * durable checkpoint was written by finish before entering this branch. */
  if(pid>1&&kill(pid,SIGSTOP))ok=0;
  for(unsigned i=0;i<2;i++)if(!restore_patch(i))ok=0;
  code_restored=!patches[0].armed&&!patches[1].armed;
  if(attached){if(!trace_stopped||ptrace(PTRACE_DETACH,tid,0,(void*)(uintptr_t)SIGSTOP))ok=0;else{attached=trace_stopped=0;detached_ok=1;call_boundary_active=0;}}
  if(watch_pipe>=0){char terminal='P';if(write(watch_pipe,&terminal,1)!=1)ok=0;close(watch_pipe);watch_pipe=-1;}
  if(watch_pid>0){
   uint64_t until=ns()+1000000000ULL;unsigned rounds=0;int status=0;pid_t got=0;
   do{got=waitpid(watch_pid,&status,WNOHANG);if(got==watch_pid)break;if(got<0&&errno!=EINTR)break;pause_ms(10);}while(ns()<until&&++rounds<100);
   if(got!=watch_pid||!WIFEXITED(status)||WEXITSTATUS(status))ok=0;
   watch_pid=-1;
  }
  return ok;
 }
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
 if(p7_shared&&p7_shared->camera_paused){p7_watch_recover();if(p7_shared->camera_paused)ok=0;}
 if(watch_pipe>=0){char c='D';if(ok&&write(watch_pipe,&c,1)!=1)ok=0;close(watch_pipe);watch_pipe=-1;}
 if(watch_pid>0){int st=0;pid_t done;do{done=waitpid(watch_pid,&st,0);}while(done<0&&errno==EINTR);if(done!=watch_pid||!WIFEXITED(st)||WEXITSTATUS(st))ok=0;watch_pid=-1;}
 return ok;
}
#include "shared_release_controller.inc"
#include "allocator_cache_fence.inc"
#include "controller_outcome.inc"
#include "../../common/album_output.h"

int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_ALLOCPROBE=0.4.0-demo51-422; SHARED192_FRONT_LUT_JPEG; NV16; NO_EXTRA_CAP_REQUEST; ONE_VERIFIED_RELEASE_R7M; MMAP_RETIREMENT_R7O; OPERATION_PROGRESS3");return 0;}
 if(argc==2&&!strcmp(argv[1],"--bitb-abi")){printf("BITB_REQUEST_BYTES=%u; BITB_RESULT_BYTES=%u; BITB_RESULT_CRC_OFFSET=%u\n",(unsigned)sizeof(struct bitb_request),(unsigned)sizeof(struct bitb_result),(unsigned)offsetof(struct bitb_result,crc));return 0;}
 if(argc==2&&!strcmp(argv[1],"--clock")){struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);clock_gettime(CLOCK_REALTIME,&b);printf("mono=%lld.%09ld realtime=%lld.%09ld\n",(long long)a.tv_sec,a.tv_nsec,(long long)b.tv_sec,b.tv_nsec);return 0;}
#if !defined(__arm__) && !defined(NXL_ALLOC_SIMULATION)
 (void)argv;fputs("ARM camera required\n",stderr);return 77;
#else
#if defined(__arm__)
 _Static_assert(sizeof(long)==4,"ARM32 ptrace ABI");
#endif
 unsigned fast=argc==4&&!strcmp(argv[1],"--fast-pipeline");
 shared_release_only=argc==3&&!strcmp(argv[1],"--release-shared");
 if((argc!=2&&!shared_release_only&&!fast)||argv[(shared_release_only||fast)?2:1][0]!='/'||strlen(argv[(shared_release_only||fast)?2:1])>1000)return 1;
 started=ns();const char *dir=argv[(shared_release_only||fast)?2:1];char path[1200];snprintf(path,sizeof path,"%s/probe.log",dir);log_file=fopen(path,"w");if(!log_file){reason="PROBE_LOG_OPEN_FAILED";controller_report(dir,1,"SAFE_NO_SUBMIT");return 1;}
 snprintf(path,sizeof path,"%s/trace_events.txt",dir);trace_file=fopen(path,"w");if(!trace_file){reason="TRACE_LOG_OPEN_FAILED";controller_report(dir,1,"SAFE_NO_SUBMIT");fclose(log_file);return 1;}
 r6_timing_set(log_file);r6_timing("CONTROLLER","BEGIN",0,1);
 int rc=1,queue=-1;struct alloc_regs regs;uint32_t f=0,ptr=0;struct alloc_message message;
#define REQUIRE(condition,message) do{if(!(condition)){reason=(message);event(reason);goto finish;}}while(0)
#define REQUIRE_TRACE(condition,message) do{if(!(condition)){failure_stage=(message);reason=trace_error?trace_error:(message);event(reason);goto finish;}}while(0)
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGALRM,stop);signal(SIGPIPE,SIG_IGN);alarm(P7_CONTROLLER_SECONDS);
 fprintf(log_file,"TIMEOUT_POLICY worker_ms=%u controller_seconds=%u watchdog_ms=%u; hardware IRQ timeouts unchanged\n",P7_WORKER_MS,P7_CONTROLLER_SECONDS,P7_WATCHDOG_MS);fflush(log_file);
 if(shared_release_only){
  snprintf(path,sizeof path,"%s/shared-workspace.bin",dir);
  REQUIRE(sw_file_read(path,&full_workspace_original,sizeof full_workspace_original)&&sw_valid(&full_workspace_original)&&full_workspace_original.phase==SW_FRONT_READY,"RELEASE_ONLY_FRONT_LEASE_INVALID");
 }else {
  if(fast){
   REQUIRE(!strncmp(argv[3],"/mnt/mmc/NXDEMO51/JOB_",sizeof "/mnt/mmc/NXDEMO51/JOB_"-1)&&!strstr(argv[3],"..")&&strlen(argv[3])<1000,"FINAL_OUTPUT_PATH_REJECTED");
   char target[1200];const char*album_pending=getenv("NX51_FINAL_PENDING");if(album_pending){REQUIRE(ao_target(album_pending,1),"ALBUM_OUTPUT_PATH_INVALID");snprintf(target,sizeof target,"%s",album_pending);}else snprintf(target,sizeof target,"%s/native.jpg.pending",argv[3]);snprintf(path,sizeof path,"%s/output-path.txt",dir);FILE*f=fopen(path,"wx");REQUIRE(f,"OUTPUT_TARGET_SAVE_FAILED");int ok=fprintf(f,"%s\n",target)>0&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;REQUIRE(ok,"OUTPUT_TARGET_SAVE_FAILED");
  }
  REQUIRE(full_open_inputs(dir),"WORKSPACE_OR_LUT_INPUT_REJECTED");
 }
 REQUIRE(firmware(),"NX500_1_12_REQUIRED");
 REQUIRE(fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV),"ORIGINAL_BUILD_FINGERPRINT_MISMATCH");
 REQUIRE(fingerprint("/usr/lib/libudd5.so",ALLOC_UDD_BYTES,ALLOC_UDD_FNV)&&fingerprint("/usr/lib/libtint-util.so",ALLOC_TINT_BYTES,ALLOC_TINT_FNV)&&fingerprint("/usr/lib/libmm-type.so",ALLOC_TYPE_BYTES,ALLOC_TYPE_FNV),"EP_LIBRARY_FINGERPRINT_MISMATCH");
 pid=find_camera();REQUIRE(pid,"CAMERA_PROCESS_NOT_UNIQUE");
 snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,O_RDWR|O_CLOEXEC);REQUIRE(procfd>=0,"PROCESS_MEMORY_READ_WRITE_PERMISSION_REQUIRED");
 REQUIRE(load_maps(dir,"begin")&&app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof alloc_cap_guards[0])&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof alloc_shell_guards[0])&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof alloc_libc_guards[0])&&guards(udd_base,alloc_udd_guards,sizeof alloc_udd_guards/sizeof alloc_udd_guards[0])&&guards(tint_base,alloc_tint_guards,sizeof alloc_tint_guards/sizeof alloc_tint_guards[0])&&guards(type_base,alloc_type_guards,sizeof alloc_type_guards/sizeof alloc_type_guards[0]),"RUNTIME_CODE_OR_MAP_GUARD_FAILED");
 REQUIRE(read_word(shell_base+0x1e33c+15*8+4,&ptr)&&ptr==shell_base+ALLOC_ENTRY,"NATIVE_APP_DISPATCH_CHANGED");
 REQUIRE(read_word(cap_base+0xb3350,&ptr)&&region_ok(ptr,0x18,1,0),"CAPTURE_CONTEXT_UNAVAILABLE");
 {unsigned char flag=0;REQUIRE(read_process(ptr+0x10,&flag,1)&&flag==1,"NATIVE_REQUEST_SERIALIZATION_NOT_ENABLED");}
 REQUIRE(read_word(cap_base+0xb33dc,&ptr)&&read_word(ptr,&f)&&f==cap_base+0xb0240,"NATIVE_RECEIVER_CONTEXT_UNAVAILABLE");
 REQUIRE(read_word(PB_POINTER_VA,&initial_ui)&&map_metadata(),"PHYSICAL_METADATA_PREFLIGHT_FAILED");
 queue=queue_id();REQUIRE(queue>=0,"NATIVE_COMMAND_QUEUE_BUSY_OR_UNAVAILABLE");
 tid=find_worker(queue,dir);REQUIRE(tid>0,thread_candidates>1?"NATIVE_SHELL_THREAD_NOT_UNIQUE":"NATIVE_SHELL_THREAD_NAME_NOT_FOUND");
 fprintf(log_file,"CONTEXT pid=%d tid=%d cap_base=0x%08x shell_base=0x%08x queue=%d receive_buffer=0x%08x syscall_sp=0x%08x\n",pid,tid,cap_base,shell_base,queue,receive_buffer,original_sp);fflush(log_file);
 REQUIRE(shared_adopt(dir),shared_adopt_error);
 REQUIRE(snapshot(dir),allocator_snapshot_reason);
 REQUIRE(sw_allocation_in_table(&shared_lease,baseline,ALLOC_BLOCKS,free_before),"SHARED_ALLOCATION_PARTITION_NOT_VERIFIED");
 patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;
 REQUIRE(read_word(patches[0].address,&patches[0].original)&&read_word(patches[1].address,&patches[1].original),"CALL_BOUNDARY_READ_FAILED");
 REQUIRE(patches[0].original==0xe92d4008&&patches[1].original==0xe8bd85f8,"CALL_BOUNDARY_INSTRUCTION_MISMATCH");
 REQUIRE(watchdog_start(),"ROLLBACK_WATCHDOG_UNAVAILABLE");
 snprintf(path,sizeof path,"%s/READY",dir);{FILE *ready=fopen(path,"w");REQUIRE(ready,"READY_CREATE_FAILED");fputs("SHARED192_BORROW_ONLY; NV16_NATIVE_LUT_JPEG_OR_GUARDED_RETURN\n",ready);fclose(ready);}event("READY");r6_timing("READY_NO_DIAGNOSTIC_SLEEP","EVENT",0,1);
 attach_attempted=1;REQUIRE(!stop_requested&&ptrace(PTRACE_ATTACH,tid,0,0)==0,"NATIVE_WORKER_ATTACH_REFUSED");attached=1;
 trace_phase="initial_attach";REQUIRE_TRACE(wait_stop(3000,SIGSTOP),"NATIVE_WORKER_ATTACH_STOP_FAILED");
 REQUIRE(other_threads_running(),"OTHER_NATIVE_THREADS_STOPPED");
 REQUIRE(verify_parked(dir,"parked",queue,0),"NATIVE_MSGRCV_REGISTERS_NOT_CONFIRMED");parked_registers_verified=1;
 REQUIRE(arm_patch(0),"ENTRY_BREAKPOINT_NOT_INSTALLED");
 alloc_make_message(&message,(uint32_t)getpid());
 REQUIRE(queue_id()==queue&&msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT)==0,"CONTROLLED_COMMAND_NOT_QUEUED");
 shell_message_queued=1;trace_phase="command_entry";
 REQUIRE_TRACE(resume(),"CONTROLLED_COMMAND_CONTINUE_FAILED");
 REQUIRE_TRACE(wait_stop(5000,SIGTRAP),"CONTROLLED_COMMAND_ENTRY_STOP_FAILED");
 REQUIRE_TRACE(verify_breakpoint(0),"CONTROLLED_COMMAND_BREAKPOINT_REJECTED");
 REQUIRE(get_regs(&regs),"CONTROLLED_COMMAND_REGISTERS_UNAVAILABLE");
 REQUIRE(own_command(&message,&regs),"CONTROLLED_COMMAND_CONTEXT_MISMATCH");entry_intercepted=call_boundary_active=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;
 REQUIRE(save_regs(dir,"entry",&regs)&&restore_patch(0)&&arm_patch(1),"CALL_BOUNDARY_PREPARATION_FAILED");
 snprintf(path,sizeof path,"%s/allocator.csv",dir);csv=fopen(path,"w");REQUIRE(csv,"CSV_CREATE_FAILED");fputs("elapsed_ms,stage,stable,va,pa,index,free_blocks,entry_word0,span_blocks,entry_word2,entry_word3,entry_crc32\n",csv);
 /* Consume the verified shell command without invoking an allocator API. */
 regs=call_original;regs.r[0]=0;regs.r[15]=shell_base+ALLOC_RETURN;regs.r[17]=UINT32_MAX;
 REQUIRE(set_regs(&regs),"BORROWED_COMMAND_SKIP_RETURN_FAILED");
 row("borrowed_shared160",index_owned);event("SHARED_FRONT_ALLOCATION_BORROWED_NO_REQUEST");
 uint64_t sample_stamp=ns();pattern_nonce=(uint32_t)sample_stamp^(uint32_t)(sample_stamp>>32)^(uint32_t)getpid();
 if(!shared_release_only&&!ep_preflight(dir)){
  REQUIRE(!api_inflight&&!native_signal&&trace_stopped&&!stop_requested&&still_owned(),"EP_PREFLIGHT_FAILED_NOT_AT_SAFE_RETURN");
  event("EP_PREFLIGHT_FAILED_RETURNING_OWNED_MEMORY");
 }
 if(!shared_release_only&&ep_contract_verified&&!ep_sync_preflight(dir)){
  REQUIRE(!api_inflight&&!native_signal&&trace_stopped&&!stop_requested&&still_owned(),"EP_SYNC_PREFLIGHT_FAILED_NOT_AT_SAFE_RETURN");
  event("EP_SYNC_PREFLIGHT_FAILED_RETURNING_OWNED_MEMORY");
 }
 if(!shared_release_only&&ep_contract_verified&&sync_preflight_verified){
  REQUIRE(shared_claim(dir),"SHARED_WORKSPACE_CLAIM_FAILED");
  (void)cq_capture(dir,"BEFORE_EP");
  p7_copy_control(dir);
  REQUIRE(!p7_quarantined,"JPEG_MEMORY_QUARANTINED_COLD_RESTART");
  REQUIRE(!api_inflight&&!native_signal&&trace_stopped&&!stop_requested&&still_owned(),"COPY_TEST_NOT_AT_SAFE_RETURN");
 }
 REQUIRE(still_owned()&&shared_front_live(),"SHARED_OWNER_CHANGED_AFTER_PIPELINE");
 if(shared_borrowed)(void)cq_capture(dir,"AFTER_EP");
 REQUIRE(shared_finish(p7_verified),"SHARED_WORKSPACE_TERMINAL_LEASE_NOT_VERIFIED");
 REQUIRE(shared_release_once(dir),reason);
 (void)cq_capture(dir,"AFTER_RELEASE_CALL");
 REQUIRE(allocator_cache_fence_once(dir),reason);
 REQUIRE(p7_resume(),"SHARED_RELEASED_CAMERA_RESUME_NOT_VERIFIED");
 REQUIRE(read_word(PB_POINTER_VA,&f)&&f==initial_ui,"UNEXPECTED_NATIVE_UI_BUFFER_CHANGE");
 regs=call_original;regs.r[0]=0;regs.r[15]=shell_base+ALLOC_RETURN;regs.r[17]=UINT32_MAX;
 REQUIRE(set_regs(&regs),"COMPLETED_RELEASE_REGISTER_RESTORE_FAILED");
 REQUIRE(cleanup_trace(),"CALL_BOUNDARY_RESTORE_OR_DETACH_FAILED");
 REQUIRE(stabilize_worker(dir,queue),"NATIVE_WORKER_DID_NOT_RETURN_TO_RECEIVE");idle_registers_verified=1;
 REQUIRE(shared_release_confirm(dir),reason);
 reason=shared_release_only?"SHARED192_ABORT_RELEASE_VERIFIED":!ep_contract_verified?ep_error:(!sync_preflight_verified?sync_error:(!p7_verified?p7_error:"NATIVE_LUT_JPEG_SHARED192_VERIFIED_RELEASED"));
 rc=shared_released&&(shared_release_only||(ep_contract_verified&&sync_preflight_verified&&p7_verified))?0:1;

finish:
 alarm(0);
 /* Always commit an UNKNOWN checkpoint before cleanup can wait or park. No
  * safe classification is published until all cleanup/release evidence exists. */
 if(!controller_report(dir,rc,"UNKNOWN_REQUIRE_RESTART"))rc=1;
 if(!cleanup_trace()&&rc==0){rc=1;reason="CLEANUP_INCOMPLETE";}
 if(csv){if(fflush(csv)||fsync(fileno(csv)))rc=1;if(fclose(csv))rc=1;csv=NULL;}
 if(pid>0&&procfd>=0)load_maps(dir,"end");
 if(!controller_report(dir,rc,controller_classification()))rc=1;
 event(reason);fprintf(log_file,"RESULT exit=%d native_calls=%u owned=%u returned=%u restored=%u detached=%u\n",rc,native_calls,ownership_verified,return_verified,code_restored,detached_ok);fclose(log_file);fclose(trace_file);
 if(shared_alias)munmap((void*)shared_alias,SW_BYTES);if(shared_sma>=0)close(shared_sma);if(manager_map)munmap((void*)manager_map,0xb2000);if(root_map)munmap((void*)root_map,4096);if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);free(baseline);free(baseline_again);return rc;
#endif
}
