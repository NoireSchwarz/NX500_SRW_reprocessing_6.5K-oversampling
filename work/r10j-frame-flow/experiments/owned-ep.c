/* SPDX-License-Identifier: AGPL-3.0-only.
 * One explicitly owned original EP reference, spanning the experimental
 * three-stage job. Fresh process for each edge; unknown returns are held.
 * No CAP, Still Init/Finalize, semaphore repair or direct kernel writes.
 */
#define NXL_INIT_MAIN reference_cycle_unused
#define watchdog_start reference_watchdog_unused
#include "native-init.c"
#undef watchdog_start
#include <sys/stat.h>
#include "selector-confirm.h"
static char owner_root[1100],owner_job[1100],owner_token[33];
static unsigned owned_baseline,selector_before,ep_fd,sma_fd_owned,reference_owned,release_attempted;
static uint64_t camera_ticks;
static struct esc_result selector_confirmation;
static uint32_t selector_ioctl_return=0xffffffffu;
static const char*selector_idle_detail="NOT_CHECKED";
/* Any lost helper may have an unconfirmed queued command/native call. Stop
 * before restoring text; never let the original command run as a fallback. */
static int watchdog_start(void){
 int p[2],owner=getpid();if(pipe2(p,O_CLOEXEC))return 0;int child=fork();if(child<0){close(p[0]);close(p[1]);return 0;}
 if(!child){
  close(p[1]);struct pollfd f={p[0],POLLIN|POLLHUP,0};int z;do{z=poll(&f,1,P7_WATCHDOG_MS);}while(z<0&&errno==EINTR);char c=0;
  int normal=z>0&&(f.revents&POLLIN)&&read(p[0],&c,1)==1&&(c=='D'||c=='P');
  if(!normal){
   int stopped=pid>1&&!kill(pid,SIGSTOP);dprintf(fileno(log_file),"EP_WATCHDOG_UNKNOWN_CAMERA_HELD=%d\n",stopped);
   /* Text repair is allowed only after every task is observed stopped. */
   unsigned tasks=0;for(unsigned n=0;stopped&&n<200;n++){if(p7_task_states(1,&tasks))break;pause_ms(10);if(n==199)stopped=0;}
   if(stopped)for(unsigned i=0;i<2;i++){uint32_t word;if(pread(procfd,&word,4,(off_t)patches[i].address)==4&&word==ALLOC_TRAP)pwrite(procfd,&patches[i].original,4,(off_t)patches[i].address);}
   if(owner>1&&getppid()==owner)kill(owner,SIGTERM);
  }
  close(p[0]);_exit(normal?0:1);
 }close(p[0]);watch_pipe=p[1];watch_pid=child;return 1;
}
static int owner_file(const char*p,char*b,size_t cap){
 int f=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat s;size_t n=0;
 if(f<0)return 0;int ok=!fstat(f,&s)&&S_ISREG(s.st_mode)&&s.st_size>0&&(uint64_t)s.st_size<cap;
 while(ok&&n<(size_t)s.st_size){ssize_t z=read(f,b+n,(size_t)s.st_size-n);if(z<0&&errno==EINTR)continue;if(z<=0){ok=0;break;}n+=(size_t)z;}
 if(close(f))ok=0;if(ok)b[n]=0;return ok;
}
static int owner_save(const char*p,const char*b){
 int f=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;size_t n=strlen(b),at=0;int ok=1;
 while(at<n){ssize_t z=write(f,b+at,n-at);if(z<0&&errno==EINTR)continue;if(z<=0){ok=0;break;}at+=(size_t)z;}
 if(fsync(f))ok=0;if(close(f))ok=0;return ok;
}
static int owner_identity(int p,int*parent,uint64_t*ticks){
 char path[80],b[4096];snprintf(path,sizeof path,"/proc/%d/stat",p);if(!file_line(path,b,sizeof b))return 0;
 char*q=strrchr(b,')');if(!q||q[1]!=' '||q[2]=='Z'||q[2]=='X')return 0;q+=2;
 for(unsigned field=3;field<=22;field++){
  char*end=strchr(q,' ');if(!end)return 0;*end=0;
  if(field==4){char*e;long v=strtol(q,&e,10);if(*e||v<0||v>2147483647)return 0;*parent=(int)v;}
  if(field==22){char*e;unsigned long long v=strtoull(q,&e,10);if(*e||!v)return 0;*ticks=v;return 1;}
  q=end+1;
 }return 0;
}
static int owner_authorized(const char*root){
 const char*j=getenv("NX51_JOB"),*t=getenv("NX51_SESSION_TOKEN"),*a=getenv("NX51_INIT_REPORT"),*p=getenv("NX51_PROGRESS_TOKEN");
 if(!j||!t||!a||!p||strlen(t)!=32||strlen(p)!=32||strcmp(root,a)||strlen(j)>1000||strncmp(j,"/mnt/mmc/NXDEMO51/JOB_",sizeof "/mnt/mmc/NXDEMO51/JOB_"-1)||strstr(j,".."))return 0;
 for(unsigned i=0;i<32;i++)if(!((t[i]>='0'&&t[i]<='9')||(t[i]>='a'&&t[i]<='f'))||!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;
 char expected[1200];snprintf(expected,sizeof expected,"/mnt/mmc/NXDEMO51/INIT_AUTO_%s",p);if(strcmp(expected,root))return 0;
 strcpy(owner_root,root);strcpy(owner_job,j);strcpy(owner_token,t);
 char lease[512],want[512],path[80],exe[1024];unsigned supervisor=0;unsigned long long ticks=0;int parent=0;uint64_t actual=0;
 if(!owner_file("/tmp/nx-lut-lite/NX51.active.lease",lease,sizeof lease)||sscanf(lease,"pid=%u\nstart_ticks=%llu\n",&supervisor,&ticks)!=2||supervisor<2||supervisor>2147483647||!ticks)return 0;
 snprintf(want,sizeof want,"pid=%u\nstart_ticks=%llu\ntoken=%s\nmode=normal\nend=NX51LEASE2\n",supervisor,ticks,t);
 if(strcmp(lease,want)||!owner_identity((int)supervisor,&parent,&actual)||actual!=ticks)return 0;
 snprintf(path,sizeof path,"/proc/%u/exe",supervisor);ssize_t n=readlink(path,exe,sizeof exe-1);if(n<=0||n>=(ssize_t)sizeof exe)return 0;exe[n]=0;
 if(strcmp(exe,"/opt/usr/nx-lut-lite/bin/nxl-session.arm"))return 0;
 int found=0,current=getpid();for(unsigned depth=0;depth<8;depth++){if(current==(int)supervisor){found=1;break;}if(!owner_identity(current,&parent,&actual))break;current=parent;}
 if(!found)return 0;
 int f=open("/mnt/mmc/NXDEMO51/run.lock",O_RDWR|O_NOFOLLOW|O_CLOEXEC);struct stat s;if(f<0)return 0;
 int ok=!fstat(f,&s)&&S_ISREG(s.st_mode),z=flock(f,LOCK_EX|LOCK_NB),busy=z<0&&(errno==EWOULDBLOCK||errno==EAGAIN);if(!z)flock(f,LOCK_UN);close(f);return ok&&busy;
}
static int owner_hold_text(unsigned closing){
 int n=snprintf(init_hold_text,sizeof init_hold_text,"NXEPOWNED1\nreport=%s\njob=%s\ntoken=%s\ncamera_pid=%d\ncamera_ticks=%llu\nudd_base=%u\nbaseline=%u\nclose_attempted=%u\nend=NXEPOWNEDEND1\n",owner_root,owner_job,owner_token,pid,(unsigned long long)camera_ticks,udd_base,owned_baseline,closing);
 return n>0&&n<(int)sizeof init_hold_text;
}
static int owner_match(const char*p,const char*want){char b[4096];return owner_file(p,b,sizeof b)&&!strcmp(b,want);}
static int owner_hold_change(void){
 if(!owner_match(init_hold_path,init_hold_text))return 0;int f=open(init_hold_path,O_WRONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;
 int ok=owner_hold_text(1)&&!ftruncate(f,0)&&write(f,init_hold_text,strlen(init_hold_text))==(ssize_t)strlen(init_hold_text)&&!fsync(f);if(close(f))ok=0;return ok;
}
static int owner_receipt_text(char*b,size_t n){
 int z=snprintf(b,n,"NXEPACQUIRED1\nreport=%s\njob=%s\ntoken=%s\ncamera_pid=%d\ncamera_ticks=%llu\nudd_base=%u\nbaseline=%u\nacquired_count=%u\nep_fd=%u\nsma_fd=%u\nselector_before=%u\nselector_initialized=%u\ncode_restored=1\ndetached=1\nworker_idle=1\nCAP_requests=0\nend=NXEPACQUIREDEND1\n",owner_root,owner_job,owner_token,pid,(unsigned long long)camera_ticks,udd_base,owned_baseline,owned_baseline+1,ep_fd,sma_fd_owned,selector_before,selector_before?selector_before:5);
 return z>0&&(size_t)z<n;
}
static int owner_context(void){
 char target[256],path[100];
 if(!read_word(udd_base+0x5556c,&ep_fd)||!read_word(udd_base+EP_SMA_FD_DATA,&sma_fd_owned)||!ep_device_target((int)ep_fd,target))return 0;
 snprintf(path,sizeof path,"/proc/%d/fd/%u",pid,sma_fd_owned);ssize_t n=readlink(path,target,sizeof target-1);if(n<=0||n>=(ssize_t)sizeof target)return 0;target[n]=0;return !strcmp(target,"/dev/d5_sma");
}
static int owner_kernel_guards(void){
 for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof *ep_kernel_guards;i++){uint32_t w[8];if(!init_kernel_read(ep_kernel_guards[i].va,w,32)||memcmp(w,ep_kernel_guards[i].words,32))return 0;}
 /* Original selector ioctl branch, including both stores and error clear. */
 static const uint32_t addresses[]={0xc02cd37cu,0xc02cdb18u,0xc02cdb38u};
 extern const uint32_t owner_selector_guards[3][8];
 for(unsigned i=0;i<3;i++){uint32_t w[8];if(!init_kernel_read(addresses[i],w,32)||memcmp(w,owner_selector_guards[i],32))return 0;}
 return 1;
}
static int owner_driver_idle(void){
 uint32_t error=~0u;if(!init_idle())return 0;if(!init_kernel_read(0xc07554c4u,&error,4)){init_idle_error="EP_DRIVER_ERROR_UNREADABLE";return 0;}if(error){init_idle_error="EP_DRIVER_ERROR_PRESENT";return 0;}return 1;
}
static int owner_selector_read(void*unused,struct esc_observation*s){
 (void)unused;uint32_t fd=~0u,sma=~0u;
 int ok=init_kernel_read(0xc07555c4u,&s->selector,4)&&init_kernel_read(0xc07554c4u,&s->error,4)&&read_word(udd_base+EP_OPEN_COUNT_DATA,&s->references)&&read_word(udd_base+0x5556cu,&fd)&&read_word(udd_base+EP_SMA_FD_DATA,&sma);
 s->context_matches=ok&&fd==ep_fd&&sma==sma_fd_owned;return ok;
}
static int owner_selector_idle(void*unused){(void)unused;int ok=owner_driver_idle();selector_idle_detail=init_idle_error;int result=ok?1:!strcmp(init_idle_error,"EP_CONTEXT_CHANGED_DURING_IDLE_CHECK")?2:0;if(log_file)fprintf(log_file,"EP_SELECTOR_IDLE result=%d reason=%s\n",result,selector_idle_detail);return result;}
static void owner_selector_pause(void*unused,unsigned ms){(void)unused;pause_ms(ms);}
static void owner_selector_note(void*unused,unsigned sample,const struct esc_observation*s,int ok){
 (void)unused;fprintf(log_file,"EP_SELECTOR_READBACK sample=%u elapsed_ms=%llu read_ok=%d selector=%u error=%u references=%u expected_references=%u context_matches=%u\n",sample,(unsigned long long)((ns()-started)/1000000u),ok,s->selector,s->error,s->references,owned_baseline+1,s->context_matches);fflush(log_file);
}
#include "owned-ep-guards.h"
#include "owned-peer-audit.h"
#ifndef NXL_OWNED_MAIN
#define NXL_OWNED_MAIN main
#endif
int NXL_OWNED_MAIN(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXEPOWNED1 R9g durable EP peer audit; typed admission rejection; native reference/selector/return unchanged");return 0;}
 int releasing=argc==3&&!strcmp(argv[1],"--release"),acquiring=argc==3&&!strcmp(argv[1],"--acquire");
 if((!releasing&&!acquiring)||!owner_authorized(argv[2]))return 77;
 char dir[1200],path[1400],receipt[4096];snprintf(dir,sizeof dir,"%s/ep-%s",owner_root,releasing?"close":"open");if(mkdir(dir,0700))return 2;
 int rc=1,queue=-1,safe=0;uint32_t count=0,saved_scratch=0;unsigned scratch_saved=0,scratch_written=0;struct alloc_regs regs;struct alloc_message message;
 snprintf(path,sizeof path,"%s/probe.log",dir);log_file=fopen(path,"wx");snprintf(path,sizeof path,"%s/trace_events.txt",dir);trace_file=fopen(path,"wx");if(!log_file||!trace_file)return 2;
 started=ns();reason="EP_OWNED_PREFLIGHT";
#define OWN_REQUIRE(c,m) do{if(!(c)){reason=m;goto done;}}while(0)
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);signal(SIGALRM,stop);alarm(45);
 OWN_REQUIRE(firmware()&&fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV)&&fingerprint("/usr/lib/libudd5.so",ALLOC_UDD_BYTES,ALLOC_UDD_FNV),"EP_ORIGINAL_FIRMWARE_IDENTITY_FAILED");
 pid=find_camera();int parent=0;OWN_REQUIRE(pid&&owner_identity(pid,&parent,&camera_ticks),"EP_CAMERA_IDENTITY_UNAVAILABLE");
 snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,O_RDWR|O_CLOEXEC);
 OWN_REQUIRE(procfd>=0&&load_maps(dir,"begin")&&app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof *alloc_cap_guards)&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof *alloc_shell_guards)&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof *alloc_libc_guards)&&guards(udd_base,alloc_udd_guards,sizeof alloc_udd_guards/sizeof *alloc_udd_guards)&&guards(udd_base,owner_ioctl_guard,1),"EP_LOADED_CONTEXT_CHANGED");
 devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);OWN_REQUIRE(devfd>=0&&init_ready(dir,"before")>=0,"EP_RAM_UNREADABLE");
 OWN_REQUIRE(init_idle(),init_idle_error);OWN_REQUIRE(owner_driver_idle(),"EP_DRIVER_ERROR_OR_UNREADABLE");OWN_REQUIRE(owner_kernel_guards(),"EP_ORIGINAL_KERNEL_CODE_CHANGED");
 OWN_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&epri_entry_refcount(count,releasing),"EP_REFERENCE_COUNT_UNAVAILABLE");
 if(releasing){
  OWN_REQUIRE(count>0&&owner_context(),"EP_OWNED_REFERENCE_DISAPPEARED");owned_baseline=count-1;
  snprintf(path,sizeof path,"%s/ep-open/EP.acquired",owner_root);int matched=0;
  for(unsigned i=0;i<3;i++){selector_before=i==0?0:i==1?3:5;if(owner_receipt_text(receipt,sizeof receipt)&&owner_match(path,receipt)){matched=1;break;}}
  OWN_REQUIRE(matched&&owner_hold_text(0)&&owner_match(init_hold_path,init_hold_text),"EP_OWNED_RECEIPT_OR_HOLD_MISMATCH");init_hold_owned=reference_owned=1;
 }else{
  owned_baseline=count;OWN_REQUIRE(init_kernel_read(0xc07555c4u,&selector_before,4)&&(selector_before==0||selector_before==3||selector_before==5),"EP_SELECTOR_CONTEXT_UNPROVEN");
  OWN_REQUIRE(owner_hold_text(0)&&owner_save(init_hold_path,init_hold_text),"EP_PREVIOUS_RETURN_UNCONFIRMED");init_hold_owned=1;
 }
 OWN_REQUIRE(owner_peer_check(dir),owner_peer_reason);
 uint32_t dispatch;OWN_REQUIRE(read_word(shell_base+0x1e33c+15*8+4,&dispatch)&&dispatch==shell_base+ALLOC_ENTRY,"EP_NATIVE_DISPATCH_CHANGED");
 queue=queue_id();OWN_REQUIRE(queue>=0,"EP_NATIVE_QUEUE_UNAVAILABLE");tid=find_worker(queue,dir);OWN_REQUIRE(tid>0,"EP_NATIVE_THREAD_UNAVAILABLE");
 patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;
 OWN_REQUIRE(read_word(patches[0].address,&patches[0].original)&&read_word(patches[1].address,&patches[1].original)&&patches[0].original==0xe92d4008&&patches[1].original==0xe8bd85f8,"EP_CALL_BOUNDARY_CHANGED");
 OWN_REQUIRE(watchdog_start(),"EP_WATCHDOG_UNAVAILABLE");attach_attempted=1;
 OWN_REQUIRE(!ptrace(PTRACE_ATTACH,tid,0,0),"EP_ATTACH_FAILED");attached=1;
 OWN_REQUIRE(wait_stop(3000,SIGSTOP)&&other_threads_running()&&verify_parked(dir,"parked",queue,0)&&arm_patch(0),"EP_WORKER_NOT_PARKED");
 alloc_make_message(&message,(uint32_t)getpid());OWN_REQUIRE(queue_id()==queue&&!msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT),"EP_COMMAND_NOT_QUEUED");shell_message_queued=1;
 OWN_REQUIRE(resume()&&wait_stop(4000,SIGTRAP)&&verify_breakpoint(0)&&get_regs(&regs)&&own_command(&message,&regs),"EP_COMMAND_CONTEXT_UNCONFIRMED");
 entry_intercepted=call_boundary_active=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;
 struct alloc_regs consumed=call_original;consumed.r[0]=0;consumed.r[15]=shell_base+ALLOC_RETURN;consumed.r[17]=UINT32_MAX;
 OWN_REQUIRE(set_regs(&consumed),"EP_OWNED_COMMAND_CONSUME_FAILED");
 OWN_REQUIRE(restore_patch(0)&&arm_patch(1),"EP_BOUNDARY_PREPARATION_FAILED");
 OWN_REQUIRE(owner_driver_idle()&&read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&count==owned_baseline+(releasing?1u:0u),"EP_STATE_CHANGED_BEFORE_CALL");
 if(releasing){
  OWN_REQUIRE(owner_hold_change(),"EP_CLOSE_INTENT_SAVE_FAILED");release_attempted=1;trace_phase="owned_ep_close";
  OWN_REQUIRE(invoke3(udd_base+0x1bde0u,0,0,0,5000)&&get_regs(&regs),"EP_CLOSE_RETURN_UNKNOWN");
  OWN_REQUIRE(!regs.r[0]&&read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&count==owned_baseline,"EP_CLOSE_REFCOUNT_UNCONFIRMED");init_closed=1;
 }else{
  trace_phase="owned_ep_open";nxl_live_progress("AUTO_EP_ACQUIRE","RUNNING","Acquiring one original EP reference for the complete test");
  OWN_REQUIRE(invoke3(udd_base+0x1bcc0u,0,0,0,5000)&&get_regs(&regs),"EP_OPEN_RETURN_UNKNOWN");
  OWN_REQUIRE(!regs.r[0]&&read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&count==owned_baseline+1,"EP_OPEN_REFCOUNT_UNCONFIRMED");init_opened=reference_owned=1;
  OWN_REQUIRE(owner_context()&&owner_driver_idle(),"EP_OPEN_CONTEXT_UNCONFIRMED");
  if(!selector_before){
   /* Exact original encoder prologue: ioctl(ep,0xc0046816,&zero).
    * This establishes driver routing; no direct state/MMIO write or job. */
   uint32_t zero=0,got=~0u;OWN_REQUIRE(region_ok(receive_buffer+48,4,1,0)&&read_word(receive_buffer+48,&saved_scratch),"EP_SELECTOR_SCRATCH_UNAVAILABLE");scratch_saved=1;scratch_written=1;
   OWN_REQUIRE(pwrite(procfd,&zero,4,(off_t)(receive_buffer+48))==4&&read_word(receive_buffer+48,&got)&&got==0,"EP_SELECTOR_ARGUMENT_UNCONFIRMED");
   trace_phase="original_encoder_selector_ioctl";
   OWN_REQUIRE(invoke3(udd_base+0x87f4u,ep_fd,0xc0046816u,receive_buffer+48,3000)&&get_regs(&regs),"EP_SELECTOR_RETURN_UNKNOWN");
   selector_ioctl_return=regs.r[0];OWN_REQUIRE(!selector_ioctl_return,"EP_SELECTOR_IOCTL_REJECTED");
   const struct esc_ops observe={NULL,owner_selector_read,owner_selector_idle,owner_selector_pause,owner_selector_note};
   OWN_REQUIRE(esc_confirm(&observe,owned_baseline+1,&selector_confirmation),esc_reason(selector_confirmation.status));
  }
 }
 OWN_REQUIRE(init_ready(dir,"after")>=0,"EP_AFTER_RAM_UNREADABLE");rc=0;reason=releasing?"OWNED_EP_REFERENCE_RETURNED":"OWNED_EP_REFERENCE_READY_FOR_CONFIGURATION";
done:
 alarm(0);
 if(api_inflight||(shell_message_queued&&!entry_intercepted)){
  p7_quarantined=1;if(pid>1)kill(pid,SIGSTOP);reason="EP_UNKNOWN_COMMAND_OR_CALL_COLD_RESTART_REQUIRED";rc=1;
 }
 if(scratch_saved&&scratch_written&&!api_inflight&&trace_stopped){uint32_t got;if(pwrite(procfd,&saved_scratch,4,(off_t)(receive_buffer+48))!=4||!read_word(receive_buffer+48,&got)||got!=saved_scratch){rc=1;reason="EP_SCRATCH_RESTORE_UNCONFIRMED";}}
 /* Only a known acquired reference, never an unknown call, is unwound. */
 if(acquiring&&rc&&reference_owned&&!release_attempted&&!api_inflight&&trace_stopped&&!native_signal&&owner_driver_idle()&&read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&count==owned_baseline+1){
  if(owner_hold_change()){release_attempted=1;trace_phase="rollback_owned_reference";if(invoke3(udd_base+0x1bde0u,0,0,0,5000)&&get_regs(&regs)&&!regs.r[0]&&read_word(udd_base+EP_OPEN_COUNT_DATA,&count)&&count==owned_baseline)init_closed=1;}
 }
 if(!cleanup_trace()){rc=1;reason="EP_TRACE_CLEANUP_UNCONFIRMED";}
 if(tid&&detached_ok&&!api_inflight&&stabilize_worker(dir,queue))init_stable=1;
 safe=!api_inflight&&(!attach_attempted||(code_restored&&detached_ok&&init_stable));
 if(!safe){rc=1;reason="EP_NATIVE_RETURN_NOT_RETIRED";}
 if(!rc&&acquiring){
  snprintf(path,sizeof path,"%s/EP.acquired",dir);
  if(!reference_owned||!owner_receipt_text(receipt,sizeof receipt)||!owner_save(path,receipt)){rc=1;reason="EP_ACQUIRE_RECEIPT_SAVE_FAILED_REFERENCE_HELD";}
 }
 if(init_hold_owned&&safe&&((releasing&&init_closed)||(acquiring&&rc&&(!native_calls||init_closed)))){
  if(!init_hold_end()){rc=1;reason="EP_HOLD_RETIREMENT_UNCONFIRMED";}
 }
 if(!rc&&releasing){
  snprintf(path,sizeof path,"%s/EP.returned",dir);snprintf(receipt,sizeof receipt,"NXEPRETURNED1\nreport=%s\njob=%s\ntoken=%s\nbaseline=%u\nfinal_count=%u\ncode_restored=1\ndetached=1\nworker_idle=1\nCAP_requests=0\nend=NXEPRETURNEDEND1\n",owner_root,owner_job,owner_token,owned_baseline,owned_baseline);
  if(!owner_save(path,receipt)){rc=1;reason="EP_RETURN_RECEIPT_SAVE_FAILED";}
 }
 snprintf(path,sizeof path,"%s/result.json",dir);FILE*f=fopen(path,"wx");if(f){fprintf(f,"{\"format\":\"NXEPOWNED1\",\"exit\":%d,\"reason\":\"%s\",\"owned_baseline\":%u,\"reference_acquired\":%u,\"reference_returned\":%u,\"native_calls\":%u,\"peer_scans\":%u,\"peer_pids\":%u,\"peer_rejected_pid\":%u,\"peer_audit_saved\":%u,\"peer_reject_kind\":\"%s\",\"selector_before\":%u,\"selector_ioctl_return\":%u,\"selector_confirmation\":%u,\"selector_samples\":%u,\"selector_pauses\":%u,\"selector_first\":%u,\"selector_after\":%u,\"selector_verify_reason\":\"%s\",\"idle_detail\":\"%s\",\"CAP_requests\":0,\"Still_Init\":0,\"Still_Finalize\":0,\"code_restored\":%u,\"detached\":%u,\"worker_idle\":%u,\"api_inflight\":%u}\n",rc,reason,owned_baseline,reference_owned,init_closed,native_calls,p7_peer_scans,p7_peer_pids,owner_peer_pid,owner_peer_saved,owner_peer_kind,selector_before,selector_ioctl_return,selector_confirmation.status,selector_confirmation.samples,selector_confirmation.pauses,selector_confirmation.first.selector,selector_confirmation.last.selector,esc_reason(selector_confirmation.status),selector_idle_detail,code_restored,detached_ok,init_stable,api_inflight);if(fflush(f)||fsync(fileno(f))||fclose(f))rc=1;}else rc=1;
 char status_reason[160];snprintf(status_reason,sizeof status_reason,"%s\n",reason);
 snprintf(path,sizeof path,"%s/STATUS.reason",dir);if(!owner_save(path,status_reason))rc=1;
 if(procfd>=0)close(procfd);if(devfd>=0)close(devfd);if(log_file)fclose(log_file);if(trace_file)fclose(trace_file);return rc;
#undef OWN_REQUIRE
}
