/* Supervised production completion ends at native idle. Read-only camera
 * and device access; no ptrace attach, native command, clock/IRQ/UI writes. */
#include "../../common/shared_workspace_io.h"
#include "../../common/allocator_fresh_read.h"
#include "../../common/progress_emit.h"
#include "session_rearm_backend.h"
#include "../../hardware/src/ep_kernel_guards.h"
struct ni_sample {struct oem_snapshot camera;struct r7_backend_sample backend;uint32_t dispatcher,slot[2],callbacks[80],free_count[3],table_crc[2];};
static struct ni_sample ni_first,ni_last;
static struct sw_lease ni_lease;
static unsigned ni_samples,ni_guards,ni_pool_verified,ni_still_verified,ni_backend_verified,ni_callbacks_verified;
static int ni_stable(const struct ni_sample*a,const struct ni_sample*b){
 return !memcmp(&a->camera,&b->camera,sizeof a->camera)&&a->dispatcher==b->dispatcher&&!memcmp(a->slot,b->slot,sizeof a->slot)&&
  !memcmp(a->callbacks,b->callbacks,sizeof a->callbacks)&&!memcmp(a->free_count,b->free_count,sizeof a->free_count)&&r7_backend_retirement_stable(&a->backend,&b->backend);
}
static int ni_ep_word(uint32_t at,uint32_t*out){
 int allowed=at==0x20821020u||at==0x20821024u;
 for(unsigned i=0;i<12;i++)if(at==0x2082a000u+r7_jpeg_offsets[i])allowed=1;
 return allowed&&mapped_read(at,out,4,4,"IDLE_EP_MMIO_WORD");
}
static int ni_backend_read(struct r7_backend_sample*s){
 memset(s,0,sizeof *s);s->kernel_readable=ram_ro_bytes(EP_KERNEL_STATE_VA-EP_KERNEL_DELTA,s->kernel,sizeof s->kernel)&&ram_ro_bytes(EP_KERNEL_MUTEX_VA-EP_KERNEL_DELTA,s->mutex,sizeof s->mutex);
 s->rtos_readable=ram_ro_bytes(0x817d8aacu,s->rtos,sizeof s->rtos)&&ram_ro32(0x817d8c98u,&s->owner);
 s->top_readable=ni_ep_word(0x20821020u,s->clock)&&ni_ep_word(0x20821024u,s->clock+1);
 if(s->top_readable&&!(s->clock[0]&0x80u)&&!(s->clock[1]&0x1e0000u)){
  s->jpeg_readable=1;for(unsigned i=0;i<12;i++)if(!ni_ep_word(0x2082a000u+r7_jpeg_offsets[i],s->jpeg+i))s->jpeg_readable=0;
 }
 const char*error=r7_backend_retirement_error(s);if(error){reason=error;return 0;}return 1;
}
static int ni_code_guards(void){
 if(!physical_guards())return 0;
 uint32_t word[8];for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof *ep_kernel_guards;i++)if(!ram_ro_bytes(ep_kernel_guards[i].va-EP_KERNEL_DELTA,word,sizeof word)||memcmp(word,ep_kernel_guards[i].words,sizeof word))return 0;
 return 1;
}
static int ni_workspace(void){
 const char*base=strrchr(directory,'/');const char*prefix="/restore-camera-pipeline-";
 if(!base||strncmp(base,prefix,strlen(prefix)))return 0;const char*attempt=base+strlen(prefix);
 if(!*attempt||*attempt=='0'||strspn(attempt,"0123456789")!=strlen(attempt))return 0;
 char p[1200],folder[1200];struct sw_lease mirror;
 if(snprintf(folder,sizeof folder,"%s/jpeg/attempt_%s/hardware",parent_job,attempt)>=(int)sizeof folder||!no_links(folder))return 0;
 if(snprintf(p,sizeof p,"%s/workspace-released.bin",folder)>=(int)sizeof p||!sw_file_read(p,&ni_lease,sizeof ni_lease)||!sw_valid(&ni_lease)||ni_lease.phase!=SW_RELEASED)return 0;
 if(snprintf(p,sizeof p,"%s/shared-workspace.bin",folder)>=(int)sizeof p||!sw_file_read(p,&mirror,sizeof mirror)||!sw_same_allocation(&ni_lease,&mirror)||mirror.phase!=SW_FRONT_READY)return 0;
 return 1;
}
static int ni_take(struct ni_sample*s,uint32_t*a,uint32_t*b,int queue){
 memset(s,0,sizeof *s);ni_pool_verified=ni_still_verified=ni_backend_verified=ni_callbacks_verified=0;
 if(!snapshot_state(&last_state)||!oem_preflight(&last_state)){reason="NATIVE_IDLE_STILL_OR_DMA_NOT_IDLE";return 0;}s->camera=last_state;ni_still_verified=1;
 unsigned char code[132];if(!ram_ro_bytes(0x801c9c50u,code,sizeof code)||sw_checksum(code,sizeof code)!=0x3dda5d7eu||!ram_ro_bytes(0x8100f7b0u,s->slot,sizeof s->slot)||s->slot[0]!=0x800e454cu||s->slot[1]){reason="NATIVE_IDLE_DISPATCH_OR_SLOT_CHANGED";return 0;}s->dispatcher=sw_checksum(code,sizeof code);
 if(!ram_ro_bytes(FR_TASK+4u,s->callbacks,160)||!ram_ro_bytes(FR_TASK+0x644u,s->callbacks+40,160)){reason="NATIVE_IDLE_CALLBACKS_UNREADABLE";return 0;}
 for(unsigned i=0;i<80;i++)if((uint64_t)s->callbacks[i]>=ni_lease.pa&&(uint64_t)s->callbacks[i]<(uint64_t)ni_lease.pa+ni_lease.capacity){reason="NATIVE_IDLE_CALLBACK_REFERENCES_RELEASED_WORKSPACE";return 0;}ni_callbacks_verified=1;
 if(!ni_backend_read(&s->backend))return 0;ni_backend_verified=1;
 if(!af_pool_head(devfd,s->free_count)||!af_read_words(devfd,AF_TABLE_PA,a,AR_TABLE_WORDS)||!af_pool_head(devfd,s->free_count+1)||!af_read_words(devfd,AF_TABLE_PA,b,AR_TABLE_WORDS)||!af_pool_head(devfd,s->free_count+2)||s->free_count[0]!=s->free_count[1]||s->free_count[1]!=s->free_count[2]||memcmp(a,b,AR_TABLE_WORDS*4u)||!sw_range_returned(&ni_lease,a,45056u,s->free_count[2])){reason="NATIVE_IDLE_WORKSPACE_RETURN_NOT_CURRENTLY_VERIFIED";return 0;}
 s->table_crc[0]=sw_checksum(a,AR_TABLE_WORDS*4u);s->table_crc[1]=sw_checksum(b,AR_TABLE_WORDS*4u);ni_pool_verified=1;
 if(queue_id()!=queue){reason="NATIVE_IDLE_COMMAND_QUEUE_CHANGED_OR_BUSY";return 0;}return 1;
}
static int ni_report(int rc){
 char path[1200];snprintf(path,sizeof path,"%s/result.json",directory);FILE*f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"format\":\"NXOEMIDLE1\",\"classification\":\"%s\",\"reason\":\"%s\",\"exit\":%d,\"readonly\":true,\"authenticated\":%u,\"original_code_verified\":%u,\"stable_samples\":%u,\"workspace_return_verified\":%u,\"still_idle\":%u,\"backend_idle\":%u,\"callbacks_retired\":%u,\"pa\":%u,\"nonce\":%u,\"free_blocks\":%u,\"native_calls\":0,\"request_submitted\":0,\"ptrace_attaches\":0,\"signal_sends\":0,\"MMIO_writes\":0,\"UI_flag_writes\":0,\"CAP_allocations\":0,\"CAP_releases\":0,\"liveview_start_calls\":0,\"elapsed_ms\":%u}\n",rc?"IDLE_UNCONFIRMED":"IDLE_VERIFIED",reason,rc,authenticated,ni_guards,ni_samples,ni_pool_verified,ni_still_verified,ni_backend_verified,ni_callbacks_verified,ni_lease.pa,ni_lease.nonce,ni_last.free_count[2],ms());
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;if(!ok||rc)return ok;
 char hash[65],text[1100];if(!digest_file(path,hash))return 0;
 snprintf(text,sizeof text,"NXOEMIDLE1\nclassification=IDLE_VERIFIED\nreadonly=1\nnative_calls=0\nrequest_submitted=0\nworkspace_return_verified=1\nstill_idle=1\nbackend_idle=1\ncallbacks_retired=1\nstable_samples=3\noriginal_code_verified=1\nno_liveview_start=1\nresult_sha256=%s\nend=NXOEMIDLEEND1\n",hash);snprintf(path,sizeof path,"%s/idle-complete.txt",directory);return save_text(path,text);
}
static int native_idle_finish(void){
 int rc=1,queue=-1;uint32_t*a=NULL,*b=NULL,*previous_table=NULL;char p[1200];stage("READONLY_NATIVE_IDLE_FINISH");nxl_live_progress("PIPE_IDLE_VERIFY","RUNNING","Checking returned workspace, callbacks and native idle state");
#define NI_REQUIRE(test,error) do{if(!(test)){reason=(error);goto done;}}while(0)
 NI_REQUIRE(authenticated&&!manual_mode&&!api_inflight&&!attached&&!native_calls&&!submitted,"NATIVE_IDLE_CONTEXT_INVALID");
 NI_REQUIRE(normal_process_view()&&firmware(),"NATIVE_IDLE_PROCESS_OR_FIRMWARE_INVALID");
 NI_REQUIRE(fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV),"NATIVE_IDLE_ORIGINAL_BUILD_MISMATCH");
 NI_REQUIRE(ni_workspace(),"NATIVE_IDLE_RELEASE_RECEIPT_INVALID");
 pid=find_camera();NI_REQUIRE(pid>1,"NATIVE_IDLE_CAMERA_NOT_UNIQUE");snprintf(p,sizeof p,"/proc/%d/mem",pid);procfd=open(p,O_RDONLY|O_CLOEXEC);NI_REQUIRE(procfd>=0,"NATIVE_IDLE_CAMERA_READ_DENIED");devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);NI_REQUIRE(devfd>=0,"NATIVE_IDLE_PHYSICAL_READ_DENIED");
 NI_REQUIRE(load_maps(directory,"before")&&app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof *alloc_cap_guards)&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof *alloc_shell_guards)&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof *alloc_libc_guards)&&ni_code_guards(),"NATIVE_IDLE_CODE_GUARDS_FAILED");ni_guards=1;
 queue=queue_id();NI_REQUIRE(queue>=0,"NATIVE_IDLE_COMMAND_QUEUE_BUSY");
 a=malloc(AR_TABLE_WORDS*4u);b=malloc(AR_TABLE_WORDS*4u);previous_table=malloc(AR_TABLE_WORDS*4u);NI_REQUIRE(a&&b&&previous_table,"NATIVE_IDLE_SNAPSHOT_MEMORY_UNAVAILABLE");
 uint64_t begin=ns(),previous=begin;NI_REQUIRE(begin,"NATIVE_IDLE_CLOCK_UNAVAILABLE");
 for(unsigned i=0;i<3;i++){
  uint64_t now=ns();NI_REQUIRE(now>=previous&&now-begin<3000000000ULL&&!stop_requested,"NATIVE_IDLE_CHECK_INTERRUPTED_OR_TIMED_OUT");previous=now;
  NI_REQUIRE(ni_take(&ni_last,a,b,queue),reason);
  if(!i)ni_first=ni_last;else NI_REQUIRE(ni_stable(&ni_first,&ni_last)&&!memcmp(previous_table,a,AR_TABLE_WORDS*4u),"NATIVE_IDLE_STATE_CHANGED");memcpy(previous_table,a,AR_TABLE_WORDS*4u);ni_samples++;
  if(i<2)pause_ms(20);
 }
 NI_REQUIRE(!native_calls&&!submitted&&!attached&&!api_inflight,"NATIVE_IDLE_UNEXPECTED_NATIVE_ACTIVITY");reason="NATIVE_IDLE_AND_WORKSPACE_RETURN_VERIFIED";rc=0;
done:
 snprintf(p,sizeof p,"%s/idle-first.bin",directory);(void)sw_file_new(p,&ni_first,sizeof ni_first);snprintf(p,sizeof p,"%s/idle-final.bin",directory);(void)sw_file_new(p,&ni_last,sizeof ni_last);
 if(!ni_report(rc))rc=1;event(reason);free(a);free(b);free(previous_table);if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);
 if(fflush(snapshots)||fsync(fileno(snapshots))||fclose(snapshots))rc=1;snapshots=NULL;
 if(fclose(log_file)||fclose(trace_file))rc=1;if(fsync(report_fd)||close(report_fd))rc=1;return rc;
#undef NI_REQUIRE
}
