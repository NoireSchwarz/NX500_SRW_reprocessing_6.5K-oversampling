/* SPDX-License-Identifier: AGPL-3.0-only. One OEM start submission; no CAP alloc/free.
 * Borrow only the verified original shell call frame. No Wait, MMIO writes or
 * UI flag repair. Unknown calls retain a camera stop and task-owned lock holds.
 */
#include "native_allocator.inc"
#include <sys/stat.h>
#include <sys/file.h>
#include "sha256.h"
#include "front_readiness.h"
#include "bridge_core.h"
#include "bridge_guards.h"
#include "oem_restore_contract.h"
#include "oem_restore_guards.h"
#include "wb_contract.h"
#include "oem_ro_read.h"
#include "../../common/session_lease_paths.h"
#include "../../common/cmdq_evidence.h"
#define OEM_VERSION "NXL_OEM_RESTORE=0.4.0-demo51-422; OEM_LIVEVIEW_RESTORE_R5; RO_MMIO_WORD_READ_R7; READONLY_IDLE_FINISH_R8A"
#define OEM_ROOT "/mnt/mmc/NXDEMO51"
#define OEM_RUN "/tmp/nx-lut-lite"
static struct oem_snapshot first_state,last_state;
static struct oem_finish completion;
static unsigned idle_mode,authenticated,manual_mode,manual_locks[2],guards_ok,stack_saved,stack_written,stack_restored;
static unsigned original_command_pending,submitted,api_return_verified,hold_verified,hold_attempted;
static unsigned concurrent_poison,handle_verified,ring_valid,ring_before,ring_after,new_log_bytes,completion_stable;
static uint32_t saved_stack[2],stack_address,api_result=UINT32_MAX;
static int supervisor_pid,manual_run_fd=-1,report_fd=-1,group_held;
static const char *directory,*classification="SAFE_NO_SUBMIT";
static char parent_job[1024],lease_token[64],report_path[1200];
static volatile unsigned char *ring_map;
static unsigned char ring_bytes[P33_CAPTURE_BYTES];
static char ring_text[P33_CAPTURE_BYTES+1];
static FILE *snapshots;
static const char *read_stage="report_setup";
static const char *last_read_stage="none",*last_fault_stage="none";
static unsigned read_failures,read_faults,last_read_address,last_read_width,last_read_bytes;
static int last_fault_signal,last_fault_code;
static unsigned last_fault_address;
static void stage(const char *s){read_stage=s;if(log_file){fprintf(log_file,"STAGE %s submitted=%u api_inflight=%u command_pending=%u\n",s,submitted,api_inflight,original_command_pending);fflush(log_file);}if(trace_file){fprintf(trace_file,"STAGE %s\n",s);fflush(trace_file);}}
static int read_begin(const char *kind,uint32_t a,unsigned bytes,unsigned width){last_read_stage=read_stage;last_read_address=a;last_read_width=width;last_read_bytes=bytes;fprintf(log_file,"READ_BEGIN stage=%s kind=%s address=0x%08x width=%u bytes=%u\n",read_stage,kind,a,width,bytes);return !fflush(log_file);}
static int read_end(int ok,const struct oem_ro_fault *f){if(!ok)read_failures++;if(f&&f->signal){read_faults++;last_fault_stage=read_stage;last_fault_signal=f->signal;last_fault_code=f->code;last_fault_address=last_read_address+f->offset;fprintf(log_file,"READ_FAULT signal=%d code=%d address=0x%08x width=%u stage=%s\n",f->signal,f->code,last_fault_address,last_read_width,read_stage);}fprintf(log_file,"READ_END address=0x%08x readable=%d errno=%d\n",last_read_address,ok,errno);return !fflush(log_file)&&ok;}
static int path_safe(const char *s){if(!s||s[0]!='/'||strlen(s)>1000||strstr(s,".."))return 0;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||strchr("/_-.",*s)))return 0;return 1;}
static int regular_file(const char *s){struct stat st;return !lstat(s,&st)&&S_ISREG(st.st_mode);}
static int no_links(const char *s){char b[1200];if(!path_safe(s))return 0;snprintf(b,sizeof b,"%s",s);for(char *p=b+1;;p++){if(*p&&*p!='/')continue;char c=*p;*p=0;struct stat st;if(lstat(b,&st))return 0;if(S_ISLNK(st.st_mode)){if(strcmp(b,"/mnt/mmc"))return 0;char *r=realpath(b,NULL);int ok=r&&!strcmp(r,"/opt/storage/sdcard");free(r);if(!ok)return 0;struct stat t;if(lstat("/opt",&t)||!S_ISDIR(t.st_mode)||lstat("/opt/storage",&t)||!S_ISDIR(t.st_mode)||lstat("/opt/storage/sdcard",&t)||!S_ISDIR(t.st_mode))return 0;}else if(!S_ISDIR(st.st_mode))return 0;*p=c;if(!c)break;}return 1;}
static int read_text(const char *path,char *b,size_t n){int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size<n;if(ok){ssize_t z=read(fd,b,n-1);ok=z>=0&&z==st.st_size;if(ok)b[z]=0;}close(fd);return ok;}
static int save_text(const char *path,const char *s){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;size_t n=strlen(s);int ok=write(fd,s,n)==(ssize_t)n&&!fsync(fd);if(close(fd))ok=0;return ok;}
static int process_identity(int p,int *parent,uint64_t *ticks){char path[128],text[2048];snprintf(path,sizeof path,"/proc/%d/stat",p);if(!file_line(path,text,sizeof text))return 0;char *r=strrchr(text,')');if(!r||r[1]!=' ')return 0;char *q=r+2,*end;for(unsigned f=3;f<=22;f++){if(!*q)return 0;if(f==3){q++;continue;}while(*q==' ')q++;unsigned long long v=strtoull(q,&end,10);if(end==q)return 0;if(f==4)*parent=(int)v;if(f==22){*ticks=v;return v>0;}q=end;}return 0;}
static int lock_owner_matches(const char *name,int owner,const char *job){char path[1200],buf[2048],want[64];snprintf(path,sizeof path,OEM_RUN"/%s",name);if(!no_links(path))return 0;snprintf(path,sizeof path,OEM_RUN"/%s/pid",name);if(!read_text(path,buf,sizeof buf))return 0;snprintf(want,sizeof want,"%d\n",owner);if(strcmp(buf,want))return 0;snprintf(path,sizeof path,OEM_RUN"/%s/NX51_OWNER",name);if(!read_text(path,buf,sizeof buf))return 0;char line_job[1100];snprintf(line_job,sizeof line_job,"job=%s\n",job);if(strncmp(buf,"NX51LOCK1\n",10)||strncmp(buf+10,line_job,strlen(line_job)))return 0;char *boot=buf+10+strlen(line_job);if(strncmp(boot,"boot=",5)||!boot[5]||strchr(boot,'\n')!=buf+strlen(buf)-1)return 0;snprintf(path,sizeof path,OEM_RUN"/%s/RESTART_REQUIRED",name);struct stat held;return lstat(path,&held)!=0&&errno==ENOENT;}
static int authenticate_supervised(void){
 stage("AUTH_ENVIRONMENT");
 const char *root=getenv("NX51_SESSION_ROOT"),*token=getenv("NX51_SESSION_TOKEN"),*job=getenv("NX51_JOB");if(!root||strcmp(root,OEM_ROOT)||!token||strlen(token)!=32||strspn(token,"0123456789abcdef")!=32||!job||!path_safe(job)||strncmp(job,OEM_ROOT"/JOB_",strlen(OEM_ROOT"/JOB_"))||strchr(job+strlen(OEM_ROOT)+1,'/'))return 0;
 stage("AUTH_PATHS");if(!no_links(root)||!no_links(job))return 0;int pp=0,gp=0;uint64_t pt=0,gt=0;if(!process_identity(getppid(),&pp,&pt)||!process_identity(pp,&gp,&gt))return 0;
 stage("AUTH_RAM_LEASE");char b[512],want[512],path[1200];snprintf(path,sizeof path,"%s",NX51_LEASE_PATH);if(!read_text(path,b,sizeof b))return 0;snprintf(want,sizeof want,"pid=%d\nstart_ticks=%llu\ntoken=%s\nmode=normal\nend=NX51LEASE2\n",pp,(unsigned long long)gt,token);if(strcmp(b,want))return 0;
 stage("AUTH_SUPERVISOR_FLOCK");int fd=open(OEM_ROOT"/run.lock",O_RDWR|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat runst;if(fstat(fd,&runst)||!S_ISREG(runst.st_mode)){close(fd);return 0;}int locked=flock(fd,LOCK_EX|LOCK_NB);int busy=locked<0&&(errno==EAGAIN||errno==EWOULDBLOCK);if(!locked)flock(fd,LOCK_UN);close(fd);if(!busy)return 0;
 stage("AUTH_WORKFLOW_OWNERS");if(!lock_owner_matches("engine.lock",getppid(),job)||!lock_owner_matches("trace.lock",getppid(),job))return 0;
 stage("AUTH_REPORT_NAMESPACE");supervisor_pid=pp;snprintf(parent_job,sizeof parent_job,"%s",job);snprintf(lease_token,sizeof lease_token,"%s",token);
 snprintf(path,sizeof path,"%s/restore-camera-front-",job);
 if(strncmp(directory,path,strlen(path))){snprintf(path,sizeof path,"%s/restore-camera-pipeline-",job);if(strncmp(directory,path,strlen(path)))return 0;}
 const char *attempt=directory+strlen(path);if(!*attempt||*attempt=='0'||strspn(attempt,"0123456789")!=strlen(attempt))return 0;return 1;
}
static int take_manual_locks(void){
 if(!no_links(OEM_RUN))return 0;
 if(strncmp(directory,OEM_ROOT"/recovery/RESTORE_",strlen(OEM_ROOT"/recovery/RESTORE_")))return 0;
 manual_run_fd=open(OEM_ROOT"/run.lock",O_RDWR|O_NOFOLLOW|O_CLOEXEC);struct stat runst;if(manual_run_fd<0||fstat(manual_run_fd,&runst)||!S_ISREG(runst.st_mode)||flock(manual_run_fd,LOCK_EX|LOCK_NB))return 0;
 const char *n[2]={"engine.lock","trace.lock"};for(unsigned i=0;i<2;i++){char p[1200],b[256];snprintf(p,sizeof p,OEM_RUN"/%s",n[i]);if(mkdir(p,0700))return 0;manual_locks[i]=1;snprintf(p,sizeof p,OEM_RUN"/%s/NXOEM_OWNER",n[i]);snprintf(b,sizeof b,"NXOEMLOCK1\npid=%d\n",getpid());if(!save_text(p,b))return 0;snprintf(p,sizeof p,OEM_RUN"/%s/pid",n[i]);snprintf(b,sizeof b,"%d\n",getpid());if(!save_text(p,b))return 0;}return 1;
}
static int normal_process_view(void){
 FILE *self=fopen("/proc/self/status","r");if(!self)return 0;fclose(self);DIR *d=opendir("/proc");if(!d)return 0;struct dirent *e;int ok=1;while((e=readdir(d))){int p=atoi(e->d_name);if(p<=1||p==getpid())continue;char path[128],comm[64];snprintf(path,sizeof path,"/proc/%d/comm",p);if(!file_line(path,comm,sizeof comm))continue;
 if(!strcmp(comm,"dfmsd")||!strcmp(comm,"nxl-demo-front.")||!strcmp(comm,"nxl-demo-post.a")||!strcmp(comm,"nxl-hw-controll")||!strcmp(comm,"nxl-hw-worker.a")||(!strcmp(comm,"nxl-session.arm")&&p!=supervisor_pid)){ok=0;break;}}
 closedir(d);return ok;
}
static int mapped_read(uint32_t a,void *out,unsigned bytes,unsigned width,const char *kind){
 if(!read_begin(kind,a,bytes,width))return 0;struct oem_ro_fault fault={0};
 void *p=mmap(NULL,4096,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)(a&~4095u));if(p==MAP_FAILED)return read_end(0,NULL);
 int ok=oem_ro_guarded_load((volatile unsigned char*)p+(a&4095u),out,bytes,width,&fault);if(munmap(p,4096))ok=0;return read_end(ok,&fault);
}
static int ram_ro_bytes(uint32_t a,void *out,unsigned bytes){if(!oem_ram_window(a,bytes))return 0;return mapped_read(a,out,bytes,1,"RAM_CODE");}
static int ram_ro32(uint32_t a,uint32_t *out){return !(a&3u)&&ram_ro_bytes(a,out,4);}
static int mmio_ro32(uint32_t a,uint32_t *out){if(!oem_mmio_address(a))return 0;return mapped_read(a,out,4,4,"MMIO_WORD");}
static int mmio_ro_words(uint32_t a,uint32_t *out,unsigned words){if(!words||words>6)return 0;for(unsigned i=0;i<words;i++)if(!oem_mmio_address(a+4*i))return 0;for(unsigned i=0;i<words;i++)if(!mmio_ro32(a+4*i,out+i))return 0;return 1;}
static int ring_read(unsigned offset,void *out,unsigned bytes,unsigned width){
 if(!ring_map||offset>P33_CONSOLE_BYTES||bytes>P33_CONSOLE_BYTES-offset||(width==4&&bytes!=4))return 0;
 while(bytes){unsigned n=4096-((P33_CONSOLE_BASE+offset)&4095u);if(n>bytes)n=bytes;struct oem_ro_fault fault={0};if(!read_begin("CONSOLE_RAM",P33_CONSOLE_BASE+offset,n,width))return 0;int ok=oem_ro_guarded_load(ring_map+offset,out,n,width,&fault);if(!read_end(ok,&fault))return 0;offset+=n;out=(unsigned char*)out+n;bytes-=n;}return 1;
}
static int ring_word(unsigned offset,uint32_t *out){return !(offset&3u)&&ring_read(offset,out,4,4);}
static int fr_adapter(void *x,uint32_t a,uint32_t *out,unsigned words){(void)x;return ram_ro_bytes(a,out,words*4);}
static int snapshot_state(struct oem_snapshot *s){
 stage("SNAPSHOT_RAM_HEADER");memset(s,0,sizeof *s);unsigned char v=0;struct fr_snapshot fr;
 if(!read_word(OEM_UI_RUNNING,&s->ui_running)||!read_word(OEM_EXPECTED,&s->expected)||!read_word(OEM_HANDLE,&s->handle)||!ram_ro32(OEM_PROCESSOR,&s->processor)||!oem_processor_valid(s->processor)||!ram_ro32(s->processor,&s->vtable)||!ram_ro_bytes(s->processor+65,&v,1))return 0;s->state=v;if(!ram_ro_bytes(OEM_REQUEST,&v,1))return 0;s->request=v;
 if(!submitted&&(s->ui_running!=1||s->expected||!s->handle||s->vtable!=OEM_VTABLE||s->state!=1||s->request)){
  fprintf(snapshots,"{\"ms\":%u,\"header_only\":1,\"ui\":%u,\"expected\":%u,\"handle\":%u,\"processor\":%u,\"vtable\":%u,\"state\":%u,\"request\":%u,\"still_idle\":0,\"dma_idle\":0}\n",ms(),s->ui_running,s->expected,s->handle,s->processor,s->vtable,s->state,s->request);return !fflush(snapshots)&&!read_failures;
 }
 stage("SNAPSHOT_STILL_RAM");fr_take(&fr,fr_adapter,NULL);s->still_idle=!fr.failure_mask;
 static const uint32_t dma_addr[]={0x8142a6f0u,0x8142a780u,0x20810308u,0x20810310u,0x20810314u,0x20810380u,0x20810384u,0x20810388u,0x20810390u,0x20810408u,0x2081040cu,0x20810418u,0x2081045cu,0x20810468u};
 stage("SNAPSHOT_DMA_WORDS");s->dma_idle=1;uint32_t d[14]={0};for(unsigned i=0;i<14;i++)if(!(i<2?ram_ro32(dma_addr[i],d+i):mmio_ro32(dma_addr[i],d+i)))s->dma_idle=0;s->dma_idle&=(unsigned)oem_dma_idle(d);
 fprintf(snapshots,"{\"ms\":%u,\"ui\":%u,\"expected\":%u,\"handle\":%u,\"processor\":%u,\"vtable\":%u,\"state\":%u,\"request\":%u,\"still_idle\":%u,\"dma_idle\":%u,\"dma\":[",ms(),s->ui_running,s->expected,s->handle,s->processor,s->vtable,s->state,s->request,s->still_idle,s->dma_idle);for(unsigned i=0;i<14;i++)fprintf(snapshots,"%s%u",i?",":"",d[i]);fputs("]",snapshots);
 /* Observe fixed original roots only. Never dereference the CAP pointers.
  * WB/descriptor data is diagnostic and does not authorize restoration. */
 stage("SNAPSHOT_DESCRIPTOR_AND_WB");uint32_t job[6]={0},primary[6]={0},secondary[6]={0};int jr=ram_ro_bytes(0x8153f410u,job,sizeof job),pr=mmio_ro_words(P41_LCAC,primary,6),sr=mmio_ro_words(P41_SRA,secondary,6);
 fprintf(snapshots,",\"still_job_readable\":%d,\"wb_primary_readable\":%d,\"wb_secondary_readable\":%d,\"still_job\":[",jr,pr,sr);for(unsigned i=0;i<6;i++)fprintf(snapshots,"%s%u",i?",":"",job[i]);fputs("],\"wb_primary\":[",snapshots);for(unsigned i=0;i<6;i++)fprintf(snapshots,"%s%u",i?",":"",primary[i]);fputs("],\"wb_secondary\":[",snapshots);for(unsigned i=0;i<6;i++)fprintf(snapshots,"%s%u",i?",":"",secondary[i]);fputs("]}\n",snapshots);return !fflush(snapshots)&&jr&&pr&&sr&&!read_failures;
}
static int physical_guards(void){uint32_t b[8];for(unsigned i=0;i<sizeof oem_rtos_guards/sizeof *oem_rtos_guards;i++)if(!ram_ro_bytes(oem_rtos_guards[i].offset,b,32)||memcmp(b,oem_rtos_guards[i].words,32))return 0;return 1;}
static int handle_in_factory(uint32_t handle){uint32_t root=0,node=0,next=0;if(!handle||!read_word(cap_base+0xb2fdc,&root)||!read_word(root,&node))return 0;uint32_t seen[128];unsigned n=0;while(node&&n<128){if(node==handle)return region_ok(node,8,1,0);for(unsigned i=0;i<n;i++)if(seen[i]==node)return 0;seen[n++]=node;if(!read_word(node+4,&next))return 0;node=next;}return 0;}
static int all_camera_stopped(void){char p[128],b[1024];snprintf(p,sizeof p,"/proc/%d/task",pid);DIR *d=opendir(p);if(!d)return 0;unsigned n=0,bad=0;struct dirent *e;while((e=readdir(d))){int t=atoi(e->d_name);if(t<=1)continue;snprintf(p,sizeof p,"/proc/%d/task/%d/stat",pid,t);if(!file_line(p,b,sizeof b)){bad++;continue;}char *q=strrchr(b,')');if(!q||q[1]!=' '||(q[2]!='T'&&q[2]!='t'))bad++;n++;}closedir(d);return n>5&&!bad;}
static int quarantine_unknown(void){
 hold_attempted=1;group_held=1;int ok=pid>1&&!kill(pid,SIGSTOP);uint64_t until=ns()+3000000000ULL;while(ns()<until&&!all_camera_stopped())pause_ms(10);if(!all_camera_stopped())ok=0;
 if(attached&&!trace_stopped){sig_atomic_t saved=stop_requested;stop_requested=0;trace_phase="quarantine_group_stop";wait_stop(2000,SIGSTOP);stop_requested=saved;}
 /* Deliberately no SETREGS and no stack writes on an unknown call. */
 for(unsigned i=0;i<2;i++)if(!restore_patch(i))ok=0;code_restored=!patches[0].armed&&!patches[1].armed;
 if(attached){if(!trace_stopped||ptrace(PTRACE_DETACH,tid,0,(void*)(uintptr_t)SIGSTOP))ok=0;else{attached=trace_stopped=0;detached_ok=1;}}
 if(pid>1&&kill(pid,SIGSTOP))ok=0;if(!all_camera_stopped())ok=0;
 if(watch_pipe>=0){if(!watchdog_mode('P'))ok=0;close(watch_pipe);watch_pipe=-1;}if(watch_pid>0){int s=0;pid_t p;do{p=waitpid(watch_pid,&s,0);}while(p<0&&errno==EINTR);if(p!=watch_pid||!WIFEXITED(s)||WEXITSTATUS(s))ok=0;watch_pid=-1;}
 hold_verified=(unsigned)ok;return ok;
}
static int restore_stack(void){if(stack_restored||!stack_saved||!stack_written)return 1;if(api_inflight||!attached||!trace_stopped)return 0;uint32_t b[2];if(pwrite(procfd,saved_stack,8,(off_t)stack_address)!=8||!read_process(stack_address,b,8)||memcmp(b,saved_stack,8))return 0;stack_restored=1;stack_written=0;return 1;}
static int invoke6_start(uint32_t handle){
 if(!call_boundary_active||!trace_stopped||native_signal||api_inflight||native_calls||!oem_preflight(&last_state)||!handle_in_factory(handle))return 0;
 stack_address=call_original.r[13];if((stack_address&7u)||!region_ok(stack_address,8,1,0)||!read_process(stack_address,saved_stack,8))return 0;stack_saved=1;uint32_t zero[2]={0},readback[2];
 if(!watchdog_mode('H'))return 0;stack_written=1;if(pwrite(procfd,zero,8,(off_t)stack_address)!=8||!read_process(stack_address,readback,8)||memcmp(readback,zero,8))return 0;
 struct alloc_regs r=alloc_call3(&call_original,cap_base+OEM_IOCTL,handle,OEM_CMD_START,0);r.r[3]=0;
 /* Last state/expected/handle check happens while the borrowed shell is held. */
 if(!snapshot_state(&last_state)||!oem_preflight(&last_state)||last_state.handle!=handle)return 0;
 if(!set_regs(&r)){trace_error="IOCTL_SETREGS_FAILED";return 0;}api_inflight=1;submitted=1;native_calls=1;trace_phase="oem_ioctl_return";
 if(!resume()||!wait_stop(8000,SIGTRAP)||!verify_breakpoint(1)||!at_return(&r))return 0;
 api_inflight=0;api_result=r.r[0];api_return_verified=1;return restore_stack();
}
static int read_fresh_ring(void){
 uint32_t after=0,again=0,one=0,two=0;if(!ring_word(4,&after))return 0;if(!p33_ring_span(ring_after,after,P33_CONSOLE_CAPACITY,&one,&two)||new_log_bytes+one+two>P33_CAPTURE_BYTES)return 0;
 if((one&&!ring_read(8+ring_after,ring_bytes+new_log_bytes,one,1))||(two&&!ring_read(8,ring_bytes+new_log_bytes+one,two,1))||!ring_word(4,&again))return 0;if(again!=after)return -1;
 new_log_bytes+=one+two;ring_after=after;size_t bytes=0;if(!p33_normalize_console(ring_bytes,new_log_bytes,ring_text,sizeof ring_text,&bytes))return 0;completion.fresh_log=(unsigned)oem_fresh_completion(ring_text);return 1;
}
static int observe_completion(void){
 uint64_t until=ns()+12000000000ULL;unsigned saw_live=0;while(!stop_requested&&ns()<until){int got=read_fresh_ring();if(!got)return 0;if(oem_log_conflict(ring_text)){concurrent_poison=1;reason="CONCURRENT_NATIVE_STOP_OR_RESTART_LOG";return 0;}if(!snapshot_state(&last_state)||!oem_runtime_consistent(&last_state,&first_state)){concurrent_poison=1;return 0;}
 if(saw_live&&!oem_live(&last_state)){concurrent_poison=1;return 0;}if(oem_live(&last_state)){saw_live=1;if(got>0&&completion.fresh_log)completion_stable++;else completion_stable=0;}else completion_stable=0;
 completion.stable=completion_stable;if(oem_completion(&last_state,&first_state,&completion))return 1;pause_ms(50);}return 0;
}
static int digest_file(const char *p,char hex[65]){FILE *f=fopen(p,"rb");if(!f)return 0;struct sha256 s;sha256_init(&s);unsigned char b[8192],d[32];size_t n;while((n=fread(b,1,sizeof b,f)))sha256_update(&s,b,n);int ok=!ferror(f)&&!fclose(f);if(ok){sha256_final(&s,d);digest_hex(d,hex);}return ok;}
static int finish_report(int rc){
 snprintf(report_path,sizeof report_path,"%s/result.json",directory);FILE *f=fopen(report_path,"wx");if(!f)return 0;
 fprintf(f,"{\"format\":\"NXOEMRESTORE1\",\"classification\":\"%s\",\"reason\":\"%s\",\"exit\":%d,\"authenticated\":%u,\"manual\":%u,\"guards_verified\":%u,\"handle_verified\":%u,\"native_calls\":%u,\"request_submitted\":%u,\"api_result\":%u,\"api_return_verified\":%u,\"stack_saved\":%u,\"stack_restored\":%u,\"code_restored\":%u,\"detached\":%u,\"api_inflight\":%u,\"hold_attempted\":%u,\"camera_group_held\":%u,\"hold_verified\":%u,\"ui_running\":%u,\"app_expected\":%u,\"real_state\":%u,\"request_flag\":%u,\"fresh_start_completion\":%u,\"completion_stable\":%u,\"concurrent_transition_poison\":%u,\"ring_before\":%u,\"ring_after\":%u,\"new_log_bytes\":%u,\"CAP_allocations\":0,\"CAP_releases\":0,\"MMIO_writes\":0,\"UI_flag_writes\":0,\"Wait_calls\":0,\"elapsed_ms\":%u",classification,reason,rc,authenticated,manual_mode,guards_ok,handle_verified,native_calls,submitted,api_result,api_return_verified,stack_saved,stack_restored,code_restored,detached_ok,api_inflight,hold_attempted,group_held,hold_verified,last_state.ui_running,last_state.expected,last_state.state,last_state.request,completion.fresh_log,completion_stable,concurrent_poison,ring_before,ring_after,new_log_bytes,ms());fprintf(f,",\"read_stage\":\"%s\",\"last_read_stage\":\"%s\",\"last_fault_stage\":\"%s\",\"last_read_address\":%u,\"last_read_width\":%u,\"last_read_bytes\":%u,\"read_failures\":%u,\"read_faults\":%u,\"fault_signal\":%d,\"fault_code\":%d,\"fault_address\":%u}\n",read_stage,last_read_stage,last_fault_stage,last_read_address,last_read_width,last_read_bytes,read_failures,read_faults,last_fault_signal,last_fault_code,last_fault_address);int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;
 if(ok&&!rc){char hex[65],text[1024],p[1200];if(!digest_file(report_path,hex))return 0;snprintf(text,sizeof text,"NXOEMRESTORE1\nclassification=RESTORED\nnative_calls=1\nrequest_submitted=1\napi_return_verified=1\nstack_restored=1\ncode_restored=1\ndetached=1\napi_inflight=0\nui_running=1\napp_expected=0\nreal_state=2\nrequest_flag=1\nfresh_start_completion=1\ncompletion_stable=1\nresult_sha256=%s\nend=NXOEMRESTOREEND1\n",hex);snprintf(p,sizeof p,"%s/restore-complete.txt",directory);ok=save_text(p,text);}
 if(ok&&fsync(report_fd))ok=0;return ok;
}
static int finish_manual_locks(int unknown){
 int ok=1;const char *n[2]={"engine.lock","trace.lock"};
 for(unsigned i=0;i<2;i++)if(manual_locks[i]){
  char p[1200],folder[1200],b[256],want[128];snprintf(folder,sizeof folder,OEM_RUN"/%s",n[i]);snprintf(p,sizeof p,"%s/NXOEM_OWNER",folder);snprintf(want,sizeof want,"NXOEMLOCK1\npid=%d\n",getpid());
  if(!no_links(folder)||!read_text(p,b,sizeof b)||strcmp(b,want)){ok=0;continue;}
  DIR *d=opendir(folder);if(!d){ok=0;continue;}struct dirent *e;int shape=1;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,"..")&&strcmp(e->d_name,"NXOEM_OWNER")&&strcmp(e->d_name,"pid"))shape=0;closedir(d);if(!shape){ok=0;continue;}
  snprintf(p,sizeof p,"%s/pid",folder);snprintf(want,sizeof want,"%d\n",getpid());if(!read_text(p,b,sizeof b)||strcmp(b,want)){ok=0;continue;}
  if(unknown){snprintf(p,sizeof p,"%s/RESTART_REQUIRED",folder);if(!save_text(p,"NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERIFIED\n"))ok=0;snprintf(p,sizeof p,"%s/pid",folder);if(unlink(p))ok=0;}
  else {if(unlink(p))ok=0;snprintf(p,sizeof p,"%s/NXOEM_OWNER",folder);if(unlink(p)||rmdir(folder))ok=0;}
 }
 return ok;
}

#include "native_idle_finish.inc"
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts(OEM_VERSION);return 0;}
 if(argc==2&&!strcmp(argv[1],"--abi")){puts("NXOEMRESTORE_ABI1 cmd=27 args=handle,27,0,0,0,0; one-submit; no-Wait; fresh-completion-required; exits=0,1");return 0;}
#if !defined(__arm__)
 (void)argc;(void)argv;fputs("NX500_ARM_REQUIRED\n",stderr);return 1;
#else
 umask(077);if(argc!=3||(strcmp(argv[1],"--supervised")&&strcmp(argv[1],"--restore-camera")&&strcmp(argv[1],"--supervised-idle"))||!path_safe(argv[2]))return 1;idle_mode=!strcmp(argv[1],"--supervised-idle");manual_mode=!strcmp(argv[1],"--restore-camera");directory=argv[2];if(manual_mode){const char *prefix=OEM_ROOT"/recovery/RESTORE_";if(strncmp(directory,prefix,strlen(prefix))||!directory[strlen(prefix)]||strchr(directory+strlen(prefix),'/'))return 1;}else if(strncmp(directory,OEM_ROOT"/JOB_",strlen(OEM_ROOT"/JOB_")))return 1;char parent[1200];snprintf(parent,sizeof parent,"%s",directory);char *slash=strrchr(parent,'/');if(!slash)return 1;*slash=0;if(!no_links(parent)||mkdir(directory,0700))return 1;
 report_fd=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(report_fd<0)return 1;char p[1200];snprintf(p,sizeof p,"%s/restore.log",directory);log_file=fopen(p,"wx");snprintf(p,sizeof p,"%s/trace_events.txt",directory);trace_file=fopen(p,"wx");snprintf(p,sizeof p,"%s/snapshots.jsonl",directory);snapshots=fopen(p,"wx");started=ns();if(log_file){fputs(OEM_VERSION "\n",log_file);fflush(log_file);}if(trace_file){fputs("OEM_RESTORE_R7 START\n",trace_file);fflush(trace_file);}if(snapshots){fputs("{\"event\":\"OEM_RESTORE_R7_START\"}\n",snapshots);fflush(snapshots);}if(!log_file||!trace_file||!snapshots){fputs("OEM_REPORT_STREAM_OPEN_FAILED\n",stderr);return 1;}stage("REPORT_OPENED");int rc=1,queue=-1;struct alloc_regs regs;struct alloc_message message;
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);
#define REQUIRE(c,m) do{if(!(c)){reason=(m);event(reason);goto done;}}while(0)
 stage("AUTHENTICATION");REQUIRE(manual_mode?take_manual_locks():authenticate_supervised(),"SUPERVISOR_OR_LOCK_AUTH_FAILED");authenticated=1;if(idle_mode)return native_idle_finish();
 stage("PROCESS_VIEW");REQUIRE(normal_process_view(),"FACTORY_MODE_OR_OTHER_NATIVE_WORKER");
 stage("FINGERPRINTS");REQUIRE(firmware(),"NX500_1_12_REQUIRED");REQUIRE(fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV),"OEM_FINGERPRINT_MISMATCH");
 stage("CAMERA_AND_PHYSICAL_FDS");pid=find_camera();REQUIRE(pid>1,"CAMERA_PROCESS_NOT_UNIQUE");snprintf(p,sizeof p,"/proc/%d/mem",pid);procfd=open(p,O_RDWR|O_CLOEXEC);REQUIRE(procfd>=0,"CAMERA_PROCESS_READ_DENIED");devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);REQUIRE(devfd>=0,"PHYSICAL_READONLY_OPEN_FAILED");
 stage("CAMERA_MAPS");REQUIRE(load_maps(directory,"before"),"OEM_RUNTIME_MAPS_FAILED");stage("PROCESS_CODE_GUARDS");REQUIRE(app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof *alloc_cap_guards)&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof *alloc_shell_guards)&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof *alloc_libc_guards)&&guards(cap_base,oem_capture_guards,sizeof oem_capture_guards/sizeof *oem_capture_guards)&&guards(0,oem_app_guards,sizeof oem_app_guards/sizeof *oem_app_guards),"OEM_RUNTIME_GUARDS_FAILED");stage("RTOS_RAM_CODE_GUARDS");REQUIRE(physical_guards(),"OEM_PHYSICAL_CODE_GUARDS_FAILED");guards_ok=1;
 stage("CAPTURE_CONTEXT");uint32_t capture_context=0,receiver=0,receiver_vtable=0;unsigned char serialized=0;REQUIRE(read_word(cap_base+0xb3350,&capture_context)&&region_ok(capture_context,0x18,1,0)&&read_process(capture_context+0x10,&serialized,1)&&serialized==1&&read_word(cap_base+0xb33dc,&receiver)&&read_word(receiver,&receiver_vtable)&&receiver_vtable==cap_base+0xb0240,"CAPTURE_SERIALIZATION_OR_RECEIVER_CONTEXT_INVALID");
 REQUIRE(snapshot_state(&first_state)&&oem_preflight(&first_state),"UI_RTOS_STATE_NOT_ELIGIBLE");last_state=first_state;for(unsigned stable=1;stable<3;stable++){pause_ms(50);REQUIRE(snapshot_state(&last_state)&&oem_preflight(&last_state)&&last_state.handle==first_state.handle&&last_state.processor==first_state.processor,"INITIAL_STATE_NOT_STABLE");}REQUIRE(handle_in_factory(first_state.handle),"ORIGINAL_CAPTURE_HANDLE_NOT_LINKED");handle_verified=1;
 stage("CONSOLE_READONLY_MAP");void *map=mmap(NULL,P33_CONSOLE_BYTES,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)P33_CONSOLE_BASE);REQUIRE(map!=MAP_FAILED,"CONSOLE_READONLY_MAP_FAILED");ring_map=map;uint32_t ring_head=0;REQUIRE(ring_word(4,&ring_before)&&ring_word(0,&ring_head),"CONSOLE_RING_UNREADABLE");ring_after=ring_before;REQUIRE(ring_before<P33_CONSOLE_CAPACITY&&ring_head<P33_CONSOLE_CAPACITY,"CONSOLE_RING_INVALID");ring_valid=1;
 uint32_t dispatch=0;REQUIRE(read_word(shell_base+0x1e33c+15*8+4,&dispatch)&&dispatch==shell_base+ALLOC_ENTRY,"ORIGINAL_APP_DISPATCH_CHANGED");
 stage("NATIVE_WORKER_PREFLIGHT");queue=queue_id();REQUIRE(queue>=0,"NATIVE_COMMAND_QUEUE_BUSY");tid=find_worker(queue,directory);REQUIRE(tid>0,"NATIVE_WORKER_NOT_UNIQUE");patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;REQUIRE(read_word(patches[0].address,&patches[0].original)&&read_word(patches[1].address,&patches[1].original)&&patches[0].original==0xe92d4008u&&patches[1].original==0xe8bd85f8u,"CALL_BOUNDARY_GUARD_FAILED");REQUIRE(watchdog_start(),"WATCHDOG_START_FAILED");
 stage("NATIVE_ATTACH");REQUIRE(!ptrace(PTRACE_ATTACH,tid,0,0),"NATIVE_ATTACH_FAILED");attached=1;trace_phase="restore_attach";REQUIRE(wait_stop(3000,SIGSTOP)&&other_threads_running()&&verify_parked(directory,"parked",queue,0),"NATIVE_PARKED_FRAME_INVALID");parked_registers_verified=1;
 stage("CONTROLLED_COMMAND");REQUIRE(arm_patch(0)&&watchdog_mode('H'),"CONTROLLED_ENTRY_GUARD_FAILED");alloc_make_message(&message,(uint32_t)getpid());original_command_pending=1;if(msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT)){original_command_pending=0;REQUIRE(0,"CONTROLLED_COMMAND_SEND_FAILED");}
 trace_phase="restore_command_entry";REQUIRE(resume()&&wait_stop(5000,SIGTRAP)&&verify_breakpoint(0)&&get_regs(&regs)&&own_command(&message,&regs),"CONTROLLED_COMMAND_NOT_VERIFIED");entry_intercepted=call_boundary_active=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;
 regs=call_original;regs.r[0]=0;regs.r[15]=shell_base+ALLOC_RETURN;regs.r[17]=UINT32_MAX;REQUIRE(set_regs(&regs),"ORIGINAL_ALLOC_COMMAND_SKIP_FAILED");original_command_pending=0;REQUIRE(restore_patch(0)&&arm_patch(1),"RETURN_BOUNDARY_SETUP_FAILED");
 REQUIRE(snapshot_state(&last_state)&&oem_preflight(&last_state)&&last_state.handle==first_state.handle&&last_state.processor==first_state.processor,"STATE_CHANGED_BEFORE_SUBMISSION");
 REQUIRE(ring_word(4,&ring_before),"PRESUBMIT_RING_UNREADABLE");ring_after=ring_before;REQUIRE(ring_before<P33_CONSOLE_CAPACITY,"PRESUBMIT_RING_TAIL_INVALID");(void)cq_capture(directory,"BEFORE_RESTORE");stage("OEM_IOCTL_SUBMIT");REQUIRE(invoke6_start(first_state.handle),"IOCTL_CALL_NOT_RETURNED_OR_STACK_NOT_RESTORED");REQUIRE(api_result==0,"IOCTL_REPORTED_FAILURE");
 stage("RESTORE_ORIGINAL_FRAME");REQUIRE(cleanup_trace()&&stabilize_worker(directory,queue),"CALL_FRAME_DETACH_OR_IDLE_RESTORE_FAILED");idle_registers_verified=1;completion.submitted=submitted;completion.api_return_verified=api_return_verified;completion.stack_restored=stack_restored;completion.code_restored=code_restored;completion.detached=detached_ok;completion.api_inflight=api_inflight;
 stage("EXTERNAL_COMPLETION");REQUIRE(observe_completion(),oem_start_failed(ring_text)?"OEM_START_FAILED_OR_DUMMY_COMPLETION":"FRESH_OEM_START_COMPLETION_NOT_VERIFIED");classification="RESTORED";reason="OEM_LIVEVIEW_RESTORED_FRESH_COMPLETION";rc=0;
 done:
 if(guards_ok&&submitted)(void)cq_capture(directory,"AFTER_RESTORE");
 stage("CLEANUP");if(original_command_pending||api_inflight){classification="UNKNOWN_REQUIRE_RESTART";if(!quarantine_unknown())reason="OEM_RESTORE_HOLD_UNCONFIRMED";}
 else {if(!restore_stack()||!cleanup_trace()){classification="UNKNOWN_REQUIRE_RESTART";reason="OEM_CALL_CLEANUP_INCOMPLETE";rc=1;}else if(submitted&&rc)classification="UNKNOWN_REQUIRE_RESTART";}
 if(!strcmp(classification,"RESTORED")&&(!oem_completion(&last_state,&first_state,&completion)||api_inflight)){classification="UNKNOWN_REQUIRE_RESTART";reason="FINAL_RESTORE_PREDICATE_CHANGED";rc=1;}
 if(ring_valid){snprintf(p,sizeof p,"%s/console-after.bin",directory);int fd=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0||write(fd,ring_bytes,new_log_bytes)!=(ssize_t)new_log_bytes||fsync(fd))rc=1;if(fd>=0)close(fd);}
 if(fflush(snapshots)||fsync(fileno(snapshots))||fclose(snapshots))rc=1;snapshots=NULL;
 if(!finish_manual_locks(strcmp(classification,"SAFE_NO_SUBMIT")&&strcmp(classification,"RESTORED"))){classification="UNKNOWN_REQUIRE_RESTART";reason="OWNED_RESTORE_LOCK_CLEANUP_FAILED";rc=1;}
 if(rc&&!strcmp(classification,"RESTORED")){classification="UNKNOWN_REQUIRE_RESTART";reason="RESTORE_REPORT_WRITE_FAILED";}if(!finish_report(rc))rc=1;
 if(ring_map)munmap((void*)ring_map,P33_CONSOLE_BYTES);if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);if(manual_run_fd>=0)close(manual_run_fd);if(report_fd>=0)close(report_fd);fclose(log_file);fclose(trace_file);return rc?1:0;
#undef REQUIRE
#endif
}
