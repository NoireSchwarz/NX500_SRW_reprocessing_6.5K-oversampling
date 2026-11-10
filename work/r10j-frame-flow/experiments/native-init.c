/* SPDX-License-Identifier: AGPL-3.0-only. Experimental original EP lifecycle.
 * No CAP request, Still Finalize, semaphore put, queue drain or kernel writes.
 * Full cold Still initialization is NOT claimed by this experiment. */
#define main shared_pipeline_reference_main
#include "../hardware/src/nxl-allocprobe.c"
#undef main
#include "../core/src/front_readiness.h"
#include <sys/file.h>
#include "../common/progress_emit.h"
#include "../common/allocator_fresh_read.h"
#include "../core/src/rearm_front.h"
#include "ep-reference-idle.h"
static uint32_t init_idle_raw[EP_KERNEL_STATE_BYTES/4u],init_idle_mutex[3],init_idle_references,init_idle_retained;
static unsigned init_opened,init_closed,init_before,init_after,init_returned,init_stable;
static const char *init_hold_path="/tmp/nx-lut-lite/auto-init-held";
static char init_hold_text[1200];static unsigned init_hold_owned,init_final,init_close_attempted;
static unsigned init_ram_reads,init_ram_failures,init_last_address,init_last_words;
static int init_last_errno,init_last_signal;
static const char *init_readiness_error="READINESS_CAPTURE_FAILED",*init_idle_error="INIT_IDLE_UNPROVEN";
static int init_cleanup_safe(void){
 if(api_inflight)return 0;
 if(!attach_attempted)return !native_calls;
 if(!code_restored||!detached_ok||!init_stable)return 0;
 return !native_calls||(init_opened&&init_closed&&init_final==init_before);
}
static int init_hold_begin(const char*dir){
 mkdir("/tmp/nx-lut-lite",0700);
 int n=snprintf(init_hold_text,sizeof init_hold_text,"NXINITHOLD1\nreport=%s\ninitial_ep_count=%u\nend=NXINITHOLDEND1\n",dir,init_before);
 if(n<=0||n>=(int)sizeof init_hold_text)return 0;
 int fd=open(init_hold_path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;init_hold_owned=1;
 int ok=write(fd,init_hold_text,(size_t)n)==n&&!fsync(fd);if(close(fd))ok=0;return ok;
}
static int init_hold_end(void){
 if(!init_hold_owned)return 1;
 int fd=open(init_hold_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;char b[1200];ssize_t n=read(fd,b,sizeof b);int ok=n==(ssize_t)strlen(init_hold_text)&&!memcmp(b,init_hold_text,(size_t)n);if(close(fd))ok=0;
 return ok&&!unlink(init_hold_path);
}
static int init_return_receipt(const char*dir){
 if(!init_opened||!init_closed||!init_stable||!code_restored||!detached_ok||api_inflight||init_final!=init_before)return 0;
 char path[1200];snprintf(path,sizeof path,"%s/EP.complete",dir);FILE*f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"NXINITRET2\nreport=%s\nreference_acquired=1\nreference_returned=1\ninitial_count=%u\nfinal_count=%u\ncode_restored=1\ndetached=1\nworker_idle=1\napi_inflight=0\nCAP_requests=0\nend=NXINITRETEND2\n",dir,init_before,init_final);
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static int init_ram_words(uint32_t at,uint32_t*out,unsigned words){
 init_ram_reads++;init_last_address=at;init_last_words=words;init_last_errno=init_last_signal=0;
 if(!out||!words||words>1024u||(at&3u)||at<0x80000000u||(uint64_t)at+4u*words>0x94000000ULL){init_last_errno=EINVAL;init_ram_failures++;return 0;}
 errno=0;oem_ro_signal=0;
 int ok=af_read_words(devfd,at,out,words);
 if(!ok){init_last_errno=errno?errno:EFAULT;init_last_signal=oem_ro_signal;init_ram_failures++;}
 if(log_file){fprintf(log_file,"INIT_RAM_READ address=0x%08x words=%u method=fresh_readonly_mmap result=%d errno=%d signal=%d\n",at,words,ok,init_last_errno,init_last_signal);fflush(log_file);}
 return ok;
}
static int init_read(void *unused,uint32_t at,uint32_t*out,unsigned words){
 (void)unused;return at>=0x81000000u&&init_ram_words(at,out,words);
}
static int init_kernel_read(uint32_t va,uint32_t*out,unsigned bytes){
 if(!bytes||(bytes&3u)||va<0xc0008000u||(uint64_t)va+bytes>0xc0800000ULL)return 0;
 return init_ram_words(va-EP_KERNEL_DELTA,out,bytes/4u);
}
static int init_ready(const char*dir,const char*phase){
 unsigned failures_before=init_ram_failures;
 struct fr_snapshot a,b;fr_take(&a,init_read,NULL);pause_ms(30);fr_take(&b,init_read,NULL);
 int same=!memcmp(a.field,b.field,sizeof a.field),complete=a.read_mask==0xffffu&&b.read_mask==0xffffu;
 int io_failed=init_ram_failures!=failures_before;
 const char*classification=io_failed?"UNREADABLE":!same?"UNSTABLE":!complete?"INCOMPLETE_CONTEXT":b.failure_mask?"NOT_READY":"READY";
 char path[1200];snprintf(path,sizeof path,"%s/readiness-%s.json",dir,phase);FILE*f=fopen(path,"wx");if(!f){init_readiness_error="READINESS_REPORT_OPEN_FAILED";return -1;}
 fprintf(f,"{\"format\":\"NXINIT_READINESS2\",\"classification\":\"%s\",\"RAM_read_method\":\"FRESH_READONLY_MMAP_WORDS\",\"stable_pair\":%d,\"failure_mask\":%u,\"read_mask\":%u,\"ram_reads\":%u,\"ram_read_failures\":%u,\"last_read_address\":%u,\"last_read_errno\":%d,\"last_read_signal\":%d,\"fields\":[",classification,same,b.failure_mask,b.read_mask,init_ram_reads,init_ram_failures,init_last_address,init_last_errno,init_last_signal);
 for(unsigned i=0;i<FR_FIELDS;i++)fprintf(f,"%s{\"name\":\"%s\",\"address\":%u,\"value\":%u,\"readable\":%u,\"passed\":%u}",i?",":"",fr_descriptors[i].name,b.field[i].address,b.field[i].value,b.field[i].readable,b.field[i].passed);
 fputs("]}\n",f);int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;
 if(!ok){init_readiness_error="READINESS_REPORT_WRITE_FAILED";return -1;}
 if(io_failed){init_readiness_error="READINESS_RAM_UNREADABLE";return -1;}
 if(!same){init_readiness_error="READINESS_STATE_UNSTABLE";return -1;}
 return complete&&!b.failure_mask;
}
static int init_idle_once(void){
 unsigned failures_before=init_ram_failures;
 struct fr_snapshot fr;fr_take(&fr,init_read,NULL);unsigned needs_init=0;
 if(fr.read_mask!=0xffffu){init_idle_error=init_ram_failures!=failures_before?"STILL_IDLE_RAM_UNREADABLE":"STILL_CONTEXT_NOT_CONSTRUCTED";return 0;}
 if(!r7_front_retired(&fr,&needs_init)){init_idle_error="STILL_NOT_RETIRED";return 0;}
 uint32_t ep_state,raw[EP_KERNEL_STATE_BYTES/4u],mutex[3],references=0,retained=0;
 if(!init_read(NULL,0x8142d154u+3364u,&ep_state,1)||!init_kernel_read(EP_KERNEL_STATE_VA,raw,sizeof raw)||!init_kernel_read(EP_KERNEL_MUTEX_VA,mutex,sizeof mutex)){init_idle_error="EP_IDLE_RAM_UNREADABLE";return 0;}
 if(!read_word(udd_base+EP_OPEN_COUNT_DATA,&references)){init_idle_error="EP_REFERENCE_COUNT_UNREADABLE";return 0;}
 const char*error=epri_error(raw,mutex,references,&retained);
 if(log_file){
  fprintf(log_file,"INIT_EP_IDLE references=%u masks=%08x,%08x,%08x,%08x,%08x error=%u retained_unowned_mask=0x%08x classification=%s\n",references,raw[0x64/4],raw[0x68/4],raw[0x6c/4],raw[0x70/4],raw[0x74/4],raw[(0xc07554c4u-EP_KERNEL_STATE_VA)/4u],retained,error?error:"QUIET_REFERENCE_CONTEXT");
  for(unsigned i=0;i<21;i++){unsigned off=(EP_KERNEL_ACC_ARRAY_VA+i*12u-EP_KERNEL_STATE_VA)/4u;fprintf(log_file,"INIT_EP_QUEUE selector=%u done=%u next=%08x prev=%08x pipeline_owned=%u\n",i,raw[off],raw[off+1],raw[off+2],(EPRI_PIPELINE_ACC_MASK>>i)&1u);}fflush(log_file);
 }
 if(error){init_idle_error=error;return 0;}
 memcpy(init_idle_raw,raw,sizeof raw);memcpy(init_idle_mutex,mutex,sizeof mutex);init_idle_references=references;init_idle_retained=retained;
 if(log_file){fprintf(log_file,"INIT_IDLE still_retired=1 needs_initialization=%u ep_task_state=%u ep_selected=0x%08x kernel_mutex_idle=1 interrupt_mask=0 unowned_completions_preserved=0x%08x\n",needs_init,ep_state,fr.field[11].value,retained);fflush(log_file);}
 /* Preview CS0 can be active, as in the proven renderer. Its EP task state
  * number is context; Still ownership, counters and driver transactions are
  * checked independently. No page/return-address/state-number whitelist. */
 init_idle_error="IDLE_VERIFIED";return 1;
}
static int init_idle(void){
 if(!init_idle_once())return 0;uint32_t first[EP_KERNEL_STATE_BYTES/4u],mutex[3],references=init_idle_references;
 memcpy(first,init_idle_raw,sizeof first);memcpy(mutex,init_idle_mutex,sizeof mutex);pause_ms(20);if(!init_idle_once())return 0;
 if(memcmp(first,init_idle_raw,sizeof first)||memcmp(mutex,init_idle_mutex,sizeof mutex)||references!=init_idle_references){init_idle_error="EP_CONTEXT_CHANGED_DURING_IDLE_CHECK";return 0;}return 1;
}
/* Read-only reuse decision. Recheck readiness around the idle observations;
 * the cold retired-owner state is idle but must never be reported as ready. */
static int init_readonly_result(int ready,int require_idle,const char*dir){
 if(require_idle){
  if(init_before>=32){reason="EP_REFERENCE_COUNT_OUT_OF_RANGE";return 1;}
  if(!init_idle()){reason=init_idle_error;return 1;}
  int final_ready=init_ready(dir,"idle-final");
  if(final_ready<0){reason=init_readiness_error;return 1;}
  if(final_ready!=ready){reason="INITIALIZATION_STATE_CHANGED_DURING_IDLE_CHECK";return 1;}
 }
 reason=ready?(require_idle?"STILL_READY_AND_IDLE_NO_INITIALIZER_CALLED":"STILL_READY_NO_INITIALIZER_CALLED"):(require_idle?"STILL_COLD_AND_IDLE_NO_INITIALIZER_CALLED":"STILL_NOT_READY_NO_INITIALIZER_CALLED");
 return ready?0:42;
}
#ifndef NXL_INIT_MAIN
#define NXL_INIT_MAIN main
#endif
int NXL_INIT_MAIN(int argc,char**argv){
 int probe_idle=argc==3&&!strcmp(argv[1],"--ready-idle");
 int cycle=argc==3&&!strcmp(argv[1],"--ep-cycle"),rc=1,queue=-1,lease=-1;const char*dir=argc==3?argv[2]:"";
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXINIT_TEST3 ORIGINAL_EP_OPEN_CLOSE; FULL_COLD_STILL_NOT_PROVEN; ZERO_CAP; FRESH_READONLY_MMAP; READ_FAILURE_IS_NOT_COLD; OWNED_RETURN_RECEIPT; BOOT_LOCAL_UNKNOWN_HOLD; REFCOUNT_SCOPED_IDLE_R8K");return 0;}
 if(argc!=3||(!cycle&&!probe_idle&&strcmp(argv[1],"--readiness"))||strncmp(dir,"/mnt/mmc/NXDEMO51/INIT_",sizeof "/mnt/mmc/NXDEMO51/INIT_"-1)||strstr(dir,"..")||strlen(dir)>1000)return 2;
 if(mkdir(dir,0700))return 2;char path[1200];struct alloc_regs regs;struct alloc_message message;uint32_t word;
 started=ns();reason="INIT_TEST_PREFLIGHT";snprintf(path,sizeof path,"%s/probe.log",dir);log_file=fopen(path,"wx");snprintf(path,sizeof path,"%s/trace_events.txt",dir);trace_file=fopen(path,"wx");if(!log_file||!trace_file)return 2;
#define INIT_REQUIRE(c,m) do{if(!(c)){reason=m;goto done;}}while(0)
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);signal(SIGALRM,stop);alarm(45);
 lease=open("/mnt/mmc/NXDEMO51/run.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);INIT_REQUIRE(lease>=0&&!flock(lease,LOCK_EX|LOCK_NB),"WORKFLOW_BUSY");
 INIT_REQUIRE(firmware(),"NX500_1_12_REQUIRED");
 INIT_REQUIRE(fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV)&&fingerprint("/usr/lib/libudd5.so",ALLOC_UDD_BYTES,ALLOC_UDD_FNV),"ORIGINAL_FIRMWARE_IDENTITY_FAILED");
 pid=find_camera();INIT_REQUIRE(pid,"CAMERA_PROCESS_NOT_UNIQUE");snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,(cycle?O_RDWR:O_RDONLY)|O_CLOEXEC);INIT_REQUIRE(procfd>=0&&load_maps(dir,"begin")&&app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof *alloc_cap_guards)&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof *alloc_shell_guards)&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof *alloc_libc_guards)&&guards(udd_base,alloc_udd_guards,sizeof alloc_udd_guards/sizeof *alloc_udd_guards),"ORIGINAL_CONTEXT_GUARD_FAILED");
 devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);INIT_REQUIRE(devfd>=0,"READONLY_DEVMEM_UNAVAILABLE");
 int before_ready=init_ready(dir,"before");INIT_REQUIRE(before_ready>=0,init_readiness_error);
 INIT_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&init_before),"EP_COUNT_UNAVAILABLE");
 if(!cycle){rc=init_readonly_result(before_ready,probe_idle,dir);goto done;}
 INIT_REQUIRE(init_before<32,"EP_REFERENCE_COUNT_OUT_OF_RANGE");
 INIT_REQUIRE(init_idle(),init_idle_error);
 INIT_REQUIRE(p7_other_ep_users(-1),"OTHER_EP_USER_PRESENT");
 INIT_REQUIRE(read_word(shell_base+0x1e33c+15*8+4,&word)&&word==shell_base+ALLOC_ENTRY,"NATIVE_APP_DISPATCH_CHANGED");
 queue=queue_id();INIT_REQUIRE(queue>=0,"NATIVE_QUEUE_UNAVAILABLE");tid=find_worker(queue,dir);INIT_REQUIRE(tid>0,"NATIVE_THREAD_NOT_UNIQUE");
 patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;
 INIT_REQUIRE(read_word(patches[0].address,&patches[0].original)&&read_word(patches[1].address,&patches[1].original)&&patches[0].original==0xe92d4008&&patches[1].original==0xe8bd85f8,"ORIGINAL_CALL_BOUNDARY_CHANGED");
 INIT_REQUIRE(init_hold_begin(dir),"AUTO_INIT_HELD_OR_MARKER_UNAVAILABLE");
 INIT_REQUIRE(watchdog_start(),"WATCHDOG_UNAVAILABLE");attach_attempted=1;
 INIT_REQUIRE(ptrace(PTRACE_ATTACH,tid,0,0)==0,"ATTACH_FAILED");attached=1;trace_phase="init_attach";
 INIT_REQUIRE(wait_stop(3000,SIGSTOP)&&other_threads_running()&&verify_parked(dir,"parked",queue,0)&&arm_patch(0),"NATIVE_WORKER_NOT_PARKED");
 alloc_make_message(&message,(uint32_t)getpid());INIT_REQUIRE(queue_id()==queue&&!msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT),"COMMAND_NOT_QUEUED");shell_message_queued=1;
 trace_phase="init_entry";INIT_REQUIRE(resume()&&wait_stop(4000,SIGTRAP)&&verify_breakpoint(0)&&get_regs(&regs)&&own_command(&message,&regs),"COMMAND_CONTEXT_UNCONFIRMED");
 entry_intercepted=call_boundary_active=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;
 INIT_REQUIRE(restore_patch(0)&&arm_patch(1),"CALL_BOUNDARY_PREPARATION_FAILED");
 /* d5_ep_open/close are matched original refcount operations. No reset/IRQ job. */
 INIT_REQUIRE(init_idle()&&read_word(udd_base+EP_OPEN_COUNT_DATA,&word)&&word==init_before,"STATE_CHANGED_BEFORE_INIT");
 nxl_live_progress("AUTO_EP_OPEN","RUNNING","Calling original EP open once");
 trace_phase="original_ep_open";INIT_REQUIRE(invoke3(udd_base+0x1bcc0u,0,0,0,5000)&&get_regs(&regs),"ORIGINAL_EP_OPEN_DID_NOT_RETURN");init_returned=1;
 INIT_REQUIRE(!regs.r[0]&&read_word(udd_base+EP_OPEN_COUNT_DATA,&init_after)&&init_after==init_before+1,"ORIGINAL_EP_OPEN_REFCOUNT_UNCONFIRMED");init_opened=1;
 INIT_REQUIRE(init_ready(dir,"during-open")>=0,init_readiness_error);
 INIT_REQUIRE(init_idle(),init_idle_error);
 nxl_live_progress("AUTO_EP_CLOSE","RUNNING","Returning the one acquired EP reference");
 trace_phase="original_ep_close";init_close_attempted=1;INIT_REQUIRE(invoke3(udd_base+0x1bde0u,0,0,0,5000)&&get_regs(&regs),"ORIGINAL_EP_CLOSE_DID_NOT_RETURN");
 INIT_REQUIRE(!regs.r[0]&&read_word(udd_base+EP_OPEN_COUNT_DATA,&word)&&word==init_before,"ORIGINAL_EP_CLOSE_REFCOUNT_UNCONFIRMED");init_closed=1;init_final=word;
 INIT_REQUIRE(init_ready(dir,"after")>=0,init_readiness_error);
 reason="ORIGINAL_EP_CYCLE_VERIFIED_STILL_OWNER_NOT_FORCED";rc=0;
done:
 alarm(0);
 /* An unconfirmed native return is never followed by a compensating call. */
 if(init_opened&&!init_closed&&!init_close_attempted&&!api_inflight&&trace_stopped&&!native_signal&&init_idle()&&read_word(udd_base+EP_OPEN_COUNT_DATA,&word)&&word==init_before+1){
  trace_phase="rollback_owned_ep_reference";init_close_attempted=1;if(invoke3(udd_base+0x1bde0u,0,0,0,5000)&&get_regs(&regs)&&!regs.r[0]&&read_word(udd_base+EP_OPEN_COUNT_DATA,&word)&&word==init_before){init_closed=1;init_final=word;}
 }
 if(!cleanup_trace()){rc=1;reason="INIT_TEST_TRACE_CLEANUP_UNCONFIRMED";}
 if(tid&&detached_ok&&!api_inflight&&stabilize_worker(dir,queue))init_stable=1;
 if(cycle&&(!init_opened||!init_closed||!init_stable))rc=1;
 if(init_hold_owned&&init_cleanup_safe()){
  if(!init_hold_end()){rc=1;reason="INIT_HOLD_RETIREMENT_UNCONFIRMED";}
 }
 if(cycle&&!rc){
  nxl_live_progress("AUTO_EP_RETURN","RUNNING","Original code, reference count and command worker verified");
  if(!init_return_receipt(dir))rc=1;
 }
 snprintf(path,sizeof path,"%s/result.json",dir);FILE*f=fopen(path,"wx");if(f){fprintf(f,"{\"format\":\"NXAUTO_INIT_TEST1\",\"experimental\":true,\"fully_initialized\":false,\"exit\":%d,\"reason\":\"%s\",\"native_calls\":%u,\"CAP_requests\":0,\"CAP_releases\":0,\"Still_Init_calls\":0,\"Still_Finalize_calls\":0,\"semaphore_put_calls\":0,\"ep_before\":%u,\"ep_after_open\":%u,\"ep_open_returned\":%u,\"ep_reference_acquired\":%u,\"ep_reference_returned\":%u,\"code_restored\":%u,\"detached\":%u,\"worker_idle_verified\":%u,\"api_inflight\":%u}\n",rc,reason,native_calls,init_before,init_after,init_returned,init_opened,init_closed,code_restored,detached_ok,init_stable,api_inflight);if(fflush(f)||fsync(fileno(f)))rc=1;if(fclose(f))rc=1;}else rc=1;
 snprintf(path,sizeof path,"%s/STATUS.reason",dir);FILE*status_file=fopen(path,"wx");if(status_file){if(fprintf(status_file,"%s\n",reason)<0||fflush(status_file)||fsync(fileno(status_file)))rc=1;if(fclose(status_file))rc=1;}else rc=1;
 if(log_file){fprintf(log_file,"INIT_RESULT exit=%d reason=%s ram_reads=%u read_failures=%u last_address=0x%08x last_errno=%d last_signal=%d\n",rc,reason,init_ram_reads,init_ram_failures,init_last_address,init_last_errno,init_last_signal);fflush(log_file);}
 if(lease>=0)close(lease);if(procfd>=0)close(procfd);if(devfd>=0)close(devfd);if(log_file)fclose(log_file);if(trace_file)fclose(trace_file);return rc;
#undef INIT_REQUIRE
}
