/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../../common/progress_child_env.h"
static const char*p7_stage_failure(const struct bitb_result*r){
 if(r->failed_stage==1&&r->error==BJ_TILE_FENCE&&r->fresh_completion)return "LDC_TILE_FENCE_OBSERVED";
 if(r->failed_stage==1&&r->error==BJ_TILE_COVERAGE)return "LDC_FULL_FRAME_SAMPLE_MISMATCH";
 if(r->failed_stage==1&&r->error==BJ_DMA_DRAIN)return "LDC_DMA_STOP_ACK_UNVERIFIED";
 if(r->failed_stage==1&&r->submission_attempted&&r->error==BJ_WAIT&&!r->fresh_completion&&(r->wait_return==0||r->wait_errno==ETIMEDOUT))return "LDC_COMPLETION_TIMEOUT";
 if(r->failed_stage==1)return r->error==BJ_COMPARE&&!r->submission_attempted?"LDC_CONFIGURATION_READBACK_FAILED":"LDC_WORKER_REPORTED_FAILURE";
 if(r->failed_stage==2)return "LUT_WORKER_REPORTED_FAILURE";
 if(r->failed_stage==3)return "JPEG_WORKER_REPORTED_FAILURE";
 return "PIPELINE_WORKER_REPORTED_FAILURE";
}
/* The hardware worker includes streamed checks of 96 MiB, not just DMA time.
 * Wait across harmless signals against one fixed monotonic deadline. A timeout
 * never authorizes camera resume or buffer reuse while work is unconfirmed. */
static int p7_wait_terminal(int fd,unsigned timeout_ms){
 uint64_t end=ns()+(uint64_t)timeout_ms*1000000u;
 while(!stop_requested){
  uint64_t now=ns();if(now>=end)return 0;
  int remaining=(int)((end-now+999999u)/1000000u);
  struct pollfd p={fd,POLLIN|POLLHUP,0};int rc=poll(&p,1,remaining);
  if(rc<0){if(errno==EINTR)continue;return 0;}
  if(!rc)return 0;
  if(!(p.revents&(POLLIN|POLLHUP)))return 0;
  char c=0;ssize_t n=read(fd,&c,1);
  if(n<0&&errno==EINTR)continue;
  return n==1&&c=='D';
 }
 return 0;
}
static int p7_task_states(int want_stopped,unsigned *count){
 char path[160],line[512];snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *d=opendir(path);if(!d)return 0;
 unsigned total=0;int ok=1;struct dirent *e;
 while((e=readdir(d))){int n=atoi(e->d_name);if(n<=1)continue;snprintf(path,sizeof path,"/proc/%d/task/%d/stat",pid,n);
  if(!file_line(path,line,sizeof line)){ok=0;break;}char *p=strrchr(line,')');if(!p||p[1]!=' '){ok=0;break;}
  if(n!=tid){int stopped=p[2]=='T'||p[2]=='t';if(stopped!=want_stopped){ok=0;break;}}else if(p[2]!='t'&&p[2]!='T'){ok=0;break;}total++;
 }closedir(d);*count=total;return ok&&total>=6;
}
static int p7_resume(void){
 if(!p7_shared||!p7_shared->camera_paused)return 1;
 p7_resume_window=1;if(kill(pid,SIGCONT)){p7_resume_window=0;return 0;}p7_shared->camera_paused=0;__sync_synchronize();p7_resumes++;
 uint64_t end=ns()+2000000000u;unsigned n=0;while(ns()<end){if(p7_task_states(0,&n)){p7_resumed_verified++;return 1;}pause_ms(10);}return 0;
}
static int p7_pause(void){
 unsigned count=0;if(!p7_task_states(0,&count))return 0;
 p7_shared->camera_paused=1;__sync_synchronize();if(kill(pid,SIGSTOP))return 0;
 uint64_t end=ns()+2000000000u;while(ns()<end&&!stop_requested){if(p7_task_states(1,&count)){p7_pauses++;return 1;}pause_ms(10);}return 0;
}
static void p7_watch_recover(void){
 if(!p7_shared||!p7_shared->camera_paused)return;
 for(unsigned i=0;i<150;i++){
  (void)p7_shared->job_pid;
  if(p7_shared->worker_done){
   char path[80],name[80];snprintf(path,sizeof path,"/proc/%d/comm",pid);
   if(file_line(path,name,sizeof name)&&!strcmp(name,"di-camera-app")&&kill(pid,SIGCONT)==0){p7_shared->camera_paused=0;__sync_synchronize();dprintf(fileno(log_file),"WATCHDOG_CAMERA_RESUMED\n");}return;
  }
  struct timespec t={0,100000000};nanosleep(&t,NULL);
 }
 dprintf(fileno(log_file),"WATCHDOG_WORKER_STILL_ACTIVE: camera remains stopped; owned memory not recycled; cold restart required\n");
}
static void p7_peer_reject(unsigned scan,int candidate,const char*why,const char*value){
 if(p7_peer_audit_bytes>=sizeof p7_peer_audit)return;
 int n=snprintf(p7_peer_audit+p7_peer_audit_bytes,sizeof p7_peer_audit-p7_peer_audit_bytes,"scan=%u pid=%d rejected=%s detail=%s\n",scan,candidate,why,value?value:"");
 if(n>0){size_t available=sizeof p7_peer_audit-p7_peer_audit_bytes;p7_peer_audit_bytes+=(size_t)n<available?(size_t)n:available-1;}
}
static int p7_other_ep_users(int child){
 DIR *proc=opendir("/proc");if(!proc)return 0;struct dirent *e;int ok=1;unsigned scan=++p7_peer_scans;
 while((e=readdir(proc))){int candidate=atoi(e->d_name);if(candidate<=0||candidate==pid||candidate==getpid()||candidate==child||candidate==watch_pid)continue;
  char path[180],line[512];unsigned long pages=0;
  snprintf(path,sizeof path,"/proc/%d/statm",candidate);
  if(!file_line(path,line,sizeof line)||sscanf(line,"%lu",&pages)!=1){if(kill(candidate,0)==0){p7_peer_reject(scan,candidate,"STATM_UNREADABLE","");ok=0;break;}continue;}
  if(!pages){
   if(p7_peer_audit_bytes+100>=sizeof p7_peer_audit){ok=0;break;}
   int len=snprintf(p7_peer_audit+p7_peer_audit_bytes,sizeof p7_peer_audit-p7_peer_audit_bytes,"scan=%u pid=%d no_userspace_MM=1\n",scan,candidate);if(len>0)p7_peer_audit_bytes+=(size_t)len;continue;
  }
  snprintf(path,sizeof path,"/proc/%d/maps",candidate);FILE *maps=fopen(path,"r");if(maps){
   while(fgets(line,sizeof line,maps)){unsigned lo,hi,off;char perms[8];
    if(strstr(line,"/dev/drime5_ep")){p7_peer_reject(scan,candidate,"EP_MAPPING",line);ok=0;break;}
    if(strstr(line,"/dev/mem")&&sscanf(line,"%x-%x %7s %x",&lo,&hi,perms,&off)==4&&off<0x2082c000u&&(uint64_t)off+hi-lo>0x20820000u){p7_peer_reject(scan,candidate,"EP_DEVMEM_MAPPING",line);ok=0;break;}
   }if(ferror(maps)){p7_peer_reject(scan,candidate,"MAPS_READ_ERROR","");ok=0;}fclose(maps);if(!ok)break;
  }else {int alive=kill(candidate,0)==0;if(alive){p7_peer_reject(scan,candidate,"MAPS_UNREADABLE","");ok=0;break;}continue;}
  snprintf(path,sizeof path,"/proc/%d/fd",candidate);DIR *fds=opendir(path);if(!fds){if(kill(candidate,0)==0){p7_peer_reject(scan,candidate,"FDS_UNREADABLE","");ok=0;break;}continue;}struct dirent *fd;
  while((fd=readdir(fds))){if(fd->d_name[0]=='.')continue;char link[512],target[256];snprintf(link,sizeof link,"%s/%s",path,fd->d_name);ssize_t n=readlink(link,target,sizeof target-1);if(n<0)continue;target[n]=0;if(!strcmp(target,"/dev/drime5_ep")){p7_peer_reject(scan,candidate,"EP_FD",link);ok=0;break;}}
  closedir(fds);p7_peer_pids++;
  if(p7_peer_audit_bytes+120>=sizeof p7_peer_audit){ok=0;break;}
  int len=snprintf(p7_peer_audit+p7_peer_audit_bytes,sizeof p7_peer_audit-p7_peer_audit_bytes,"scan=%u pid=%d maps_readable=1 fd_directory_readable=1 EP_conflict=%u\n",scan,candidate,!ok);if(len>0)p7_peer_audit_bytes+=(size_t)len;
  if(!ok)break;
 }closedir(proc);return ok;
}
static int p7_save(const char *path,const void *bytes,size_t n){FILE *f=fopen(path,"wb");if(!f)return 0;int ok=fwrite(bytes,1,n,f)==n;if(fclose(f))ok=0;return ok;}
static int p7_read(const char *path,void *bytes,size_t n){FILE *f=fopen(path,"rb");if(!f)return 0;int ok=fread(bytes,1,n,f)==n&&fgetc(f)==EOF;fclose(f);return ok;}
#include "pipeline_files.inc"

#ifndef NXL_ALLOC_SIMULATION
static int p7_copy_control(const char *dir){
 const char *worker="/opt/usr/nx-lut-lite/bin/nxl-hw-worker.arm";char config[1200],result_path[1200],go_text[20],event_text[20];int go[2]={-1,-1},events[2]={-1,-1},child=-1,status=0;int ok=0,terminal=0;const char*admission_error=NULL;
 struct bitb_request q={BITB_REQUEST_MAGIC,BITB_WIRE_VERSION,response[1],BITB_BYTES,pattern_nonce,(uint32_t)pid,response[0],0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));
 p7_error="WORKER_PREPARATION_FAILED";
 if(!fingerprint(worker,BITB_WORKER_BYTES,BITB_WORKER_FNV))goto done;
 snprintf(config,sizeof config,"%s/copy_request.bin",dir);snprintf(result_path,sizeof result_path,"%s/copy_worker.bin",dir);
 if(!p7_save(config,&q,sizeof q)||pipe2(go,O_CLOEXEC)||pipe2(events,O_CLOEXEC))goto done;
 r6_timing("WORKER_LAUNCH","BEGIN",0,0);child=fork();if(child<0)goto done;
 if(!child){
  close(go[1]);close(events[0]);if(fcntl(go[0],F_SETFD,0)||fcntl(events[1],F_SETFD,0))_exit(126);
  snprintf(go_text,sizeof go_text,"%d",go[0]);snprintf(event_text,sizeof event_text,"%d",events[1]);
  char *args[]={(char*)worker,config,result_path,(char*)dir,go_text,event_text,NULL};char operation_env[64];char *env[5];
  nxl_progress_child_env(env,operation_env,sizeof operation_env,getenv("NX51_PROGRESS_TOKEN"));
  execve(worker,args,env);_exit(127);
 }
 p7_shared->job_pid=(uint32_t)child;__sync_synchronize();
 p7_peer_audit_bytes=(size_t)snprintf(p7_peer_audit,sizeof p7_peer_audit,"camera_pid=%d controller_pid=%d worker_pid=%d\n",pid,(int)getpid(),child);close(go[0]);go[0]=-1;close(events[1]);events[1]=-1;
 {struct pollfd p={events[0],POLLIN|POLLHUP,0};char c=0;if(poll(&p,1,60000)<=0||read(events[0],&c,1)!=1||c!='R')goto done;}p7_worker_ready=1;r6_timing("WORKER_READY","EVENT",0,1);
 if(!shared_front_live()){p7_error="SHARED_OWNER_EXPIRED";goto done;}
 if(!p7_other_ep_users(child)){p7_error="PEER_EP_ACCESS_UNCONFIRMED";goto done;}
 /* Verify STOP/CONT and a harmless original getter before authorizing the synthetic hardware job. */
 event("COPY_PAUSE_REHEARSAL_BEGIN");if(!p7_pause()){p7_error="CAMERA_PAUSE_REHEARSAL_FAILED";goto done;}
 if(!p7_resume()){p7_error="CAMERA_RESUME_REHEARSAL_FAILED";goto done;}
 trace_phase="copy_resume_getter";struct alloc_regs r;
 if(!invoke(udd_base+EP_OPEN_COUNT_FN,0,3000)||!get_regs(&r)||r.r[0]!=ep_open_count){p7_error="NATIVE_WORKER_AFTER_RESUME_UNCONFIRMED";goto done;}p7_rehearsal_verified=1;
 if(!still_owned()||stop_requested||!p7_other_ep_users(child)){p7_error="COPY_CONTEXT_CHANGED_BEFORE_PAUSE";goto done;}
 event("COPY_CAMERA_PAUSE_BEGIN");if(!p7_pause()){p7_error="CAMERA_PAUSE_FAILED";goto done;}
 if(!shared_front_live()){p7_error="SHARED_OWNER_EXPIRED";goto done;}
 if(!p7_other_ep_users(child)){p7_error="PEER_EP_ACCESS_UNCONFIRMED_AFTER_PAUSE";goto done;}
 {unsigned char raw[EP_KERNEL_STATE_BYTES];uint32_t mutex[3];struct ep_sync_observation state;
  if(!ep_kernel_read(EP_KERNEL_STATE_VA,raw,sizeof raw)||!ep_kernel_read(EP_KERNEL_MUTEX_VA,mutex,sizeof mutex)){p7_error="COPY_KERNEL_STATE_UNAVAILABLE";goto done;}
  ep_sync_decode(raw,mutex,&state);if(ep_sync_observation_error(&state)||!still_owned()){p7_error="COPY_STATE_BUSY_AFTER_PAUSE";goto done;}
 }
 if(write(go[1],"G",1)!=1){p7_error="WORKER_GO_FAILED";goto done;}p7_go_sent=1;r6_timing("WORKER_GO_SENT","EVENT",0,1);
 {unsigned started_wait=ms();if(p7_wait_terminal(events[0],P7_WORKER_MS)){terminal=1;}
  fprintf(log_file,"WORKER_TERMINAL_WAIT elapsed_ms=%u limit_ms=%u terminal=%u stop_requested=%u\n",ms()-started_wait,P7_WORKER_MS,terminal,(unsigned)stop_requested);fflush(log_file);}
 r6_timing("WORKER_DONE_OBSERVED","EVENT",0,terminal);if(!terminal){p7_error="WORKER_DID_NOT_CONFIRM_CLEANUP";goto done;}
 /* Resume is delayed until the result record proves cleanup. */
 p7_error="WORKER_RESULT_UNAVAILABLE";
done:
 if(!p7_go_sent)admission_error=p7_error;
 if(go[1]>=0){if(!p7_go_sent)write(go[1],"C",1);close(go[1]);go[1]=-1;}
 if(child>0){uint64_t end=ns()+10000000000ull;while(ns()<end){pid_t got=waitpid(child,&status,WNOHANG);if(got==child){p7_worker_exited=1;p7_worker_exit=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);break;}if(got<0&&errno!=EINTR)break;pause_ms(20);}}

 if(go[0]>=0)close(go[0]);if(go[1]>=0)close(go[1]);if(events[0]>=0)close(events[0]);if(events[1]>=0)close(events[1]);
 if(child>0&&p7_worker_exited&&p7_read(result_path,&p7_result,sizeof p7_result)&&p7_result.magic==BITB_RESULT_MAGIC&&p7_result.version==BITB_WIRE_VERSION&&p7_result.bytes==sizeof p7_result&&p7_result.request_crc==q.crc&&p7_result.pa==q.pa&&p7_result.crc==bitb_crc(&p7_result,offsetof(struct bitb_result,crc))){
  p7_result_valid=1;p7_safe_to_release=p7_result.safe_to_release;
  if(p7_safe_to_release){p7_shared->worker_done=1;__sync_synchronize(); /* Keep UI paused until the shared CAP is released. */}

  ok=!p7_worker_exit&&!p7_result.error&&p7_result.safe_to_release&&p7_result.ready&&p7_result.paused_contract&&p7_rehearsal_verified&&p7_result.idle_verified&&p7_result.inactive_start_verified&&p7_result.cold_restart_required&&p7_result.local_reset_calls==6&&p7_result.translations==3*TEST_TRANSLATIONS&&p7_result.matches==3*TEST_TRANSLATIONS&&p7_result.code_guards==NXL_EP_NATIVE_CODE_GUARDS&&p7_result.rtos_guards==NXL_EP_RTOS_CODE_GUARDS&&p7_result.engine_jobs==3&&p7_result.ldc_hardware_jobs==1&&p7_result.lut_hardware_jobs==1&&p7_result.fresh_completion&&p7_result.source_ok&&p7_result.target_ok&&p7_result.guards_ok&&p7_result.restore_verified&&p7_result.owner_stable&&p7_result.owner_records==2&&p7_result.route_verified&&!p7_result.route_flags&&p7_verify_files(dir,&q)&&p7_resumed_verified==1&&p7_shared->camera_paused;
  if(ok)p7_error="";else if(admission_error)p7_error=admission_error;else if(p7_result.error)p7_error=p7_stage_failure(&p7_result);else p7_error="JPEG422_POSTCONDITION_NOT_VERIFIED";
 }
 if(!p7_go_sent&&p7_worker_exited){p7_safe_to_release=1;p7_shared->worker_done=1;if(!p7_resume())p7_safe_to_release=0;}
 if(p7_go_sent&&!p7_safe_to_release){char hold[1200];snprintf(hold,sizeof hold,"%s/P50_RESTART_REQUIRED",dir);p7_save(hold,"UNVERIFIED_HARDWARE_CLEANUP\n",sizeof "UNVERIFIED_HARDWARE_CLEANUP\n"-1);}
 p7_quarantined=p7_go_sent&&(!p7_worker_exited||!p7_result_valid||!p7_safe_to_release);
 {char audit[1200];snprintf(audit,sizeof audit,"%s/copy_peer_audit.txt",dir);if(!p7_save(audit,p7_peer_audit,p7_peer_audit_bytes)&&ok){ok=0;p7_error="COPY_PEER_AUDIT_SAVE_FAILED";}}
 fprintf(log_file,"WORKER_ADMISSION go_sent=%u primary_error=%s worker_error=%u worker_exit=%u\n",p7_go_sent,p7_error?p7_error:"",p7_result.error,p7_worker_exit);fflush(log_file);
 p7_verified=ok;event(ok?"LUT_TO_JPEG_FILES_AND_COMPLETIONS_VERIFIED":p7_error);return ok;
}
#else
#include "bitb_sim_control.inc"
#endif
