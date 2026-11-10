/* SPDX-License-Identifier: AGPL-3.0-only. Linux side of bounded IPC experiment. */
#include "pause_guardian.h"
#include "front_readiness_host.inc"
static int wait_ready(void){
 uint64_t until=ns()+6000000000ULL;struct p39_result a,b;
 while(!stop_requested&&ns()<until){
  if(!safe_call()||!result_copy(&a)||!result_copy(&b))return 0;
  if(!memcmp(&a,&b,sizeof a)){observed_result=a;if(a.w[3]>=100)return 0;if(a.w[3]==P39_READY&&p48_read_reply(P48_PHASE_READY)){
   if(a.w[0]!=P39_MAGIC||a.w[2]!=nonce||(int32_t)a.w[4]<0||a.w[18]!=1||!slot_original())return 0;
   ready_verified=hook_result_verified=hook_restored=hook_native_restore_verified=hook_ram_restored=1;hook_installed=0;slot_pointer_writes++;return 1;
  }}pause_ms(5);
 }return 0;
}
static int leave_native_worker(const char *dir,int queue){
 struct alloc_regs r;if(!call_boundary_active||api_inflight||!trace_stopped||native_signal)return 0;
 r=call_original;r.r[0]=0;r.r[15]=shell_base+ALLOC_RETURN;r.r[17]=0xffffffffu;if(!set_regs(&r)||!cleanup_trace())return 0;call_boundary_active=0;
 if(!detached_ok||!stabilize_worker(dir,queue))return 0;idle_registers_verified=1;return 1;
}
static int camera_threads_stopped(void){
 char path[128],line[512];snprintf(path,sizeof path,"/proc/%d/task",pid);DIR *d=opendir(path);if(!d)return 0;struct dirent *e;unsigned count=0,other=0;
 while((e=readdir(d))){int candidate=atoi(e->d_name);if(candidate<=1)continue;snprintf(path,sizeof path,"/proc/%d/task/%d/stat",pid,candidate);if(!file_line(path,line,sizeof line)){other++;continue;}char *p=strrchr(line,')');if(!p||p[1]!=' '){other++;continue;}count++;if(p[2]!='T'&&p[2]!='t')other++;}
 closedir(d);return count>5&&!other;
}
static int resume_camera(void){
 if(p48_parked_camera)return 1;
 int ok=1;if(camera_paused){if(kill(pid,SIGCONT))ok=0;else{camera_paused=0;camera_resumes++;event("CAMERA_SIGCONT_SENT");uint64_t until=ns()+2000000000ULL;while(ns()<until){if(other_threads_running()){camera_resumed_verified=1;break;}pause_ms(10);}if(!camera_resumed_verified)ok=0;}}
 if(pause_watch_pipe>=0){char c='D';if(write(pause_watch_pipe,&c,1)!=1)ok=0;close(pause_watch_pipe);pause_watch_pipe=-1;}
 if(pause_watch_pid>0){int status;pid_t got;do{got=waitpid(pause_watch_pid,&status,0);}while(got<0&&errno==EINTR);if(got!=pause_watch_pid||!WIFEXITED(status)||WEXITSTATUS(status))ok=0;pause_watch_pid=-1;}return ok;
}
#ifndef P48_PARK_STOPPED
#define P48_PARK_STOPPED camera_threads_stopped
#endif
static int p48_park_uncertain_command(void){
 p48_parked_camera=1;int ok=pid>1&&kill(pid,SIGSTOP)==0;camera_paused=1;
 if(attached&&!trace_stopped){sig_atomic_t was=stop_requested;stop_requested=0;wait_stop(3000,SIGSTOP);stop_requested=was;}
 /* Stop the watchdog with a distinct terminal code, even on recovery errors.
  * Its forked copy cannot observe the parent's sticky state. */
 if(watch_pipe>=0){if(!watchdog_mode('P'))ok=0;close(watch_pipe);watch_pipe=-1;}
 if(watch_pid>0){int st=0;pid_t got;do{got=waitpid(watch_pid,&st,0);}while(got<0&&errno==EINTR);if(got!=watch_pid||!WIFEXITED(st)||WEXITSTATUS(st))ok=0;watch_pid=-1;}
 for(unsigned i=0;i<2;i++)if(!restore_patch(i))ok=0;code_restored=!patches[0].armed&&!patches[1].armed;
 if(attached){if(!trace_stopped||ptrace(PTRACE_DETACH,tid,0,(void*)(uintptr_t)SIGSTOP))ok=0;else{attached=trace_stopped=0;detached_ok=1;}}
 uint64_t end=ns()+3000000000ULL;int held=0;while(ns()<end){if(P48_PARK_STOPPED()){held=1;break;}pause_ms(10);}if(!held)ok=0;
 char text[256];snprintf(text,sizeof text,"NXP48RESTARTREQUIRED1\nreason=UNVERIFIED_SHELL_MESSAGE\ncamera_group_stopped=%u\npatches_restored=%u\nno_auto_resume=1\ncold_restart_required=1\n",held,code_restored);if(!p48_atomic_text(batch_directory,"P48_RESTART_REQUIRED",text))ok=0;
 event(ok?"UNVERIFIED_SHELL_MESSAGE_PARKED_COLD_RESTART_REQUIRED":"UNVERIFIED_SHELL_MESSAGE_HOLD_UNCONFIRMED_COLD_RESTART_REQUIRED");return ok;
}
static int pause_camera(void){
 if(attached||!detached_ok||!idle_registers_verified)return 0;
 int channel[2];if(pipe2(channel,O_CLOEXEC))return 0;int child=fork();if(child<0){close(channel[0]);close(channel[1]);return 0;}
 if(!child){close(channel[1]);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGALRM,SIG_DFL);_exit(p39_pause_guardian(channel[0],pid,90000));}
 close(channel[0]);pause_watch_pipe=channel[1];pause_watch_pid=child;
 if(kill(pid,SIGSTOP))return 0;camera_paused=1;uint64_t until=ns()+3000000000ULL;
 while(!stop_requested&&ns()<until){if(camera_threads_stopped()){event("CAMERA_GROUP_STOP_VERIFIED");return 1;}pause_ms(10);}return 0;
}
static int wait_done(void){
 uint64_t wait_started=ns(),until=wait_started+60000000000ULL;struct p39_result a,b;uint32_t last_observed[5];unsigned have_observation=0;
 while(!stop_requested&&ns()<until){
  if(!result_copy(&a)||!result_copy(&b))return 0;
  if(!memcmp(&a,&b,sizeof a)){
   uint32_t current[5]={a.w[3],a.w[12],a.w[14],a.w[22],a.w[23]};
   if(!have_observation||memcmp(current,last_observed,sizeof current)){
    fprintf(log_file,"RTOS_PHASE_OBSERVED phase=%u frame_irq=%u dma_irq=%u source_fnv=%08x target_fnv=%08x sampled=1 elapsed_ms=%u wait_ms=%llu\n",current[0],current[1],current[2],current[3],current[4],ms(),(unsigned long long)((ns()-wait_started)/1000000u));
    memcpy(last_observed,current,sizeof current);have_observation=1;
   }
   observed_result=a;if(a.w[3]==P39_DONE&&p48_read_reply(P48_PHASE_DONE))return 1;if(a.w[3]>=100)return 0;
  }pause_ms(5);
 }result_copy(&observed_result);return 0;
}
#ifndef P39_CAMERA_STOPPED
#define P39_CAMERA_STOPPED camera_threads_stopped
#define P39_SLOT_ORIGINAL slot_original
#endif
_Alignas(4) static unsigned char observed_stop[P39_STOP_BYTES];
static char stop_hash[65];
_Alignas(4) static unsigned char observed_diagnostics[P39_DIAG_BYTES];
static char diagnostics_hash[65];
static uint32_t luma_changed_bytes,chroma_changed_bytes,chroma_rows_written,chroma_incomplete_rows,chroma_prefix_ok;
static uint32_t proof_snapshot_saved,measured_nonzero_bytes,guard_changed_bytes,first_guard_change;

static void measure_buffers(void){
#ifdef NXL_FAST_PIPELINE
 if(real_measured||!real_ready)return;guard_changed_bytes=0;
 const unsigned bounds[]={P39_SOURCE_OFFSET+P39_SOURCE_BYTES,P39_TARGET_OFFSET-16384,P39_TARGET_OFFSET+P39_TARGET_BYTES};
 for(unsigned k=0;k<3;k++)for(unsigned i=0;i<16384;i++)guard_changed_bytes+=cap_alias[bounds[k]+i]!=0x6d;
 cap_canaries_verified=!guard_changed_bytes;real_measured=1;return;
#else
 if(real_measured||!real_ready)return;unsigned begin=ms();real_tiles_init();
 uint32_t source_after_fnv=0;alias_digest(P39_SOURCE_OFFSET,P39_SOURCE_BYTES,source_after_sha,&source_after_fnv);
 input_unchanged=!strcmp(source_after_sha,p48_source->layout_sha256)&&source_after_fnv==source_fnv;
 uint32_t h=2166136261u;measured_nonzero_bytes=0;luma_changed_bytes=chroma_changed_bytes=0;
 output_rows_written=chroma_rows_written=chroma_incomplete_rows=0;chroma_prefix_ok=1;
 cap_canaries_verified=1;guard_changed_bytes=0;first_guard_change=0xffffffffu;
 for(unsigned plane=0;plane<2;plane++){
  struct sha256 s;sha256_init(&s);unsigned char digest[32];
  for(unsigned y=0;y<P39_H;y++){
   memcpy(real_scratch,(const void*)(cap_alias+P39_TARGET_OFFSET+(plane?P39_UV_OFFSET:0)+y*P39_TARGET_STRIDE),P39_TARGET_STRIDE);
   sha256_update(&s,real_scratch,P39_TARGET_STRIDE);h=fnv_more(h,real_scratch,P39_TARGET_STRIDE);
   unsigned row_changed=0;
   for(unsigned col=0;col<14;col++){
    struct p39_tile *tile=&tiles[(y/P39_TILE_H)*14+col];unsigned count=0;
    for(unsigned x=0;x<tile->w;x++){unsigned char v=real_scratch[tile->x+x];count+=v!=0xa5;measured_nonzero_bytes+=v!=0;}
    tile->changed[plane]+=count;tile->rows[plane]+=count>=tile->w/2;row_changed+=count;
   }
   if(!plane){luma_changed_bytes+=row_changed;output_rows_written+=row_changed>=P39_W/2;}
   else{chroma_changed_bytes+=row_changed;chroma_rows_written+=row_changed>=P39_W/2;if(row_changed&&row_changed<P39_W/2)chroma_incomplete_rows++;}
  }
  sha256_final(&s,digest);digest_hex(digest,plane_sha[plane]);
  if(!plane)for(unsigned at=P39_PLANE_BYTES;at<P39_UV_OFFSET;){unsigned n=P39_UV_OFFSET-at;if(n>sizeof real_scratch)n=sizeof real_scratch;
   memcpy(real_scratch,(const void*)(cap_alias+P39_TARGET_OFFSET+at),n);h=fnv_more(h,real_scratch,n);
   for(unsigned k=0;k<n;k++)if(real_scratch[k]!=0xa5){guard_changed_bytes++;if(first_guard_change==0xffffffffu)first_guard_change=P39_TARGET_OFFSET+at+k;}at+=n;}
 }
 target_fnv=h;output_changed_bytes=luma_changed_bytes+chroma_changed_bytes;
 for(unsigned i=0;i<42;i++)for(unsigned plane=0;plane<2;plane++)tiles_written[plane]+=tiles[i].rows[plane]==tiles[i].h&&tiles[i].changed[plane]>=tiles[i].w*tiles[i].h*3/4;
 for(unsigned i=0;i<P39_WINDOW_BYTES;i++){
  if((i>=SLA_OFFSET&&i<SLA_OFFSET+SLA_BYTES)||(i>=P49_PD_OFFSET&&i<P49_PD_OFFSET+P49_PD_BYTES)||(i>=P48_IDLE_HANDOFF_OFFSET&&i<P48_PREVIOUS_IDLE_OFFSET+256)||(!p48_case_index&&i>=P48_INITIAL_STOP_OFFSET&&i<P48_INITIAL_STOP_OFFSET+P48_INITIAL_STOP_BYTES)||(i>=P41_OFFSET+256&&i<P41_OFFSET+P41_BYTES)||(i>=P48_CC_OFFSET&&i<P48_CC_OFFSET+P48_CC_BYTES)||(i>=P48_BATCH_CONTROL_OFFSET&&i<P48_BATCH_FROZEN_OFFSET+P48_BATCH_FROZEN_BYTES)||(i>=P39_RESULT_OFFSET&&i<P39_RESULT_OFFSET+256)||(i>=P39_DIAG_OFFSET&&i<P39_DIAG_OFFSET+P39_DIAG_BYTES)||(i>=P39_STOP_OFFSET&&i<P39_STOP_OFFSET+P39_STOP_BYTES)||i>=P39_SOURCE_OFFSET)continue;
  if(cap_alias[i]!=expected[i]){guard_changed_bytes++;if(first_guard_change==0xffffffffu)first_guard_change=i;}
 }
 const unsigned guards[]={P39_SOURCE_OFFSET+P39_SOURCE_BYTES,P39_TARGET_OFFSET-16384,P39_TARGET_OFFSET+P39_TARGET_BYTES};
 for(unsigned k=0;k<3;k++)for(unsigned i=0;i<16384;i++)if(cap_alias[guards[k]+i]!=0x6d){guard_changed_bytes++;if(first_guard_change==0xffffffffu)first_guard_change=guards[k]+i;}
 cap_canaries_verified=!guard_changed_bytes;real_measured=1;frame_measured_ms=ms()-begin;
#endif
}
static int verify_result(void){
 if(!p48_snapshot_valid((const unsigned char*)cap_alias)||!p48_read_reply(P48_PHASE_DONE))return 0;
 if(!ready_verified||!task_quiescent_verified||!P39_SLOT_ORIGINAL())return 0;
 measure_buffers();
 if(!p48_stop_record_valid((const uint32_t*)(cap_alias+P39_STOP_OFFSET),(const uint32_t*)(cap_alias+P48_INITIAL_STOP_OFFSET),nonce,response[1],p48_case_index+1)||!p39_diagnostics_valid((const uint32_t*)(cap_alias+P39_DIAG_OFFSET),nonce,response[1]))return 0;
#ifdef NXL_FAST_PIPELINE
 if(!p39_result_valid(&observed_result,response[1],nonce,source_fnv,observed_result.w[23])||!real_ready||!cap_canaries_verified)return 0;
#else
 if(!p39_result_valid(&observed_result,response[1],nonce,source_fnv,target_fnv)||!real_ready||!real_measured||tiles_written[0]!=42||tiles_written[1]!=42||output_rows_written!=P39_H||chroma_rows_written!=P39_H||!measured_nonzero_bytes||!input_unchanged||!cap_canaries_verified)return 0;
#endif
 copy_verified=dma_write_verified=1;
 memcpy(p48_wb_copy,(const void*)(cap_alias+P41_OFFSET),P41_BYTES);memcpy(p48_cc_copy,(const void*)(cap_alias+P48_CC_OFFSET),P48_CC_BYTES);
 memcpy(gt_copy,(const void*)(cap_alias+GT_OFFSET),GT_AUDIT_BYTES);
 if(gt_copy[7]!=((const uint32_t*)p48_wb_copy)[47]||!gt_audit_valid(gt_copy,nonce,((const uint32_t*)p48_cc_copy)[63]))return 0;
 p48_canonical_copied=1;
 memcpy(expected+P41_OFFSET,(const void*)(cap_alias+P41_OFFSET),P41_BYTES);
 memcpy(expected+P48_CC_OFFSET,(const void*)(cap_alias+P48_CC_OFFSET),P48_CC_BYTES);
 memcpy(expected+SLA_OFFSET,(const void*)(cap_alias+SLA_OFFSET),SLA_BYTES);
 memcpy(expected+P49_PD_OFFSET,(const void*)(cap_alias+P49_PD_OFFSET),P49_PD_BYTES);
 memcpy(expected+P48_BATCH_CONTROL_OFFSET,(const void*)(cap_alias+P48_BATCH_CONTROL_OFFSET),P48_BATCH_CONTROL_BYTES+P48_BATCH_FROZEN_BYTES);
 memcpy(expected+P48_IDLE_HANDOFF_OFFSET,(const void*)(cap_alias+P48_IDLE_HANDOFF_OFFSET),512);
 memcpy(expected+P48_INITIAL_STOP_OFFSET,(const void*)(cap_alias+P48_INITIAL_STOP_OFFSET),P48_INITIAL_STOP_BYTES);
 memcpy(expected+P39_STOP_OFFSET,(const void*)(cap_alias+P39_STOP_OFFSET),P39_STOP_BYTES);
 memcpy(expected+P39_RESULT_OFFSET,&observed_result,sizeof observed_result);
 memcpy(expected+P39_DIAG_OFFSET,(const void*)(cap_alias+P39_DIAG_OFFSET),P39_DIAG_BYTES);
 memcpy(expected+P39_TARGET_PROOF_OFFSET,(const void*)(cap_alias+P39_TARGET_OFFSET),P39_WINDOW_BYTES);return 1;
}
static int save_bytes(const char *dir,const char *name,const void *data,unsigned n){
 char path[1200];snprintf(path,sizeof path,"%s/%s",dir,name);FILE *f=fopen(path,"wx");if(!f)return 0;
 int ok=fwrite(data,1,n,f)==n&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static int save_plane(const char *dir,unsigned plane){
 char path[1200];snprintf(path,sizeof path,"%s/p39_output.%s",dir,plane?"uv8":"y8");FILE *f=fopen(path,"wx");if(!f)return 0;
 struct sha256 s;sha256_init(&s);int ok=1;
 for(unsigned at=0;at<P39_PLANE_BYTES;){unsigned n=P39_PLANE_BYTES-at;if(n>sizeof real_scratch)n=sizeof real_scratch;
  memcpy(real_scratch,(const void*)(cap_alias+P39_TARGET_OFFSET+(plane?P39_UV_OFFSET:0)+at),n);sha256_update(&s,real_scratch,n);
  if(fwrite(real_scratch,1,n,f)!=n){ok=0;break;}at+=n;}
 unsigned char digest[32];char hex[65];sha256_final(&s,digest);digest_hex(digest,hex);
 if(real_measured&&strcmp(hex,plane_sha[plane]))ok=0;
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;return ok;
}
static int save_proof(const char *dir){
#ifdef NXL_FAST_PIPELINE
 if(!cap_alias||!p49_go_sent)return 1;
 source_iq_evidence_saved=save_bytes(dir,"source-cc-audit.bin",(const void*)(cap_alias+P48_CC_OFFSET),P48_CC_BYTES)&&save_bytes(dir,"source-wb-audit.bin",(const void*)(cap_alias+P41_OFFSET),P41_BYTES)&&save_bytes(dir,"gamma-test-audit.bin",(const void*)(cap_alias+GT_OFFSET),GT_AUDIT_BYTES);
 (void)save_bytes(dir,"frozen-config-audit.bin",(const void*)(cap_alias+P48_BATCH_FROZEN_OFFSET),P48_BATCH_FROZEN_BYTES);
 /* An evidence I/O error is reported by the coordinator after workspace
  * return; it must not invalidate an already verified DMA retirement. */
 if(!strcmp(reason,"P48_NATIVE_FRAME_COMPLETE"))return 1;
 /* Failure-only control records: retain the native completion/retirement
  * evidence without copying the intermediate image or full CAP to SD. */
 unsigned char control[1024];int ok=1;
 const unsigned offsets[]={P39_RESULT_OFFSET,P39_STOP_OFFSET,P48_INITIAL_STOP_OFFSET,P48_BATCH_CONTROL_OFFSET,P48_IDLE_HANDOFF_OFFSET};
 const unsigned lengths[]={256,P39_STOP_BYTES,P48_INITIAL_STOP_BYTES,P48_BATCH_CONTROL_BYTES,512};
 const char*names[]={"failed-native-result.bin","failed-native-stop.bin","failed-native-initial-stop.bin","failed-native-batch.bin","failed-native-idle.bin"};
 for(unsigned k=0;k<sizeof offsets/sizeof *offsets;k++){
  if(lengths[k]>sizeof control)return 0;
  for(unsigned i=0;i<lengths[k];i++)control[i]=cap_alias[offsets[k]+i];
  if(!save_bytes(dir,names[k],control,lengths[k]))ok=0;
 }return ok;
#else
 if(!cap_alias)return 1;unsigned char *observed=malloc(P39_PROOF_BYTES);if(!observed)return 0;
 memcpy(observed,(const void*)cap_alias,P39_WINDOW_BYTES);memcpy(observed+P39_TARGET_PROOF_OFFSET,(const void*)(cap_alias+P39_TARGET_OFFSET),P39_WINDOW_BYTES);
 memcpy(p48_wb_copy,observed+P41_OFFSET,P41_BYTES);memcpy(p48_cc_copy,observed+P48_CC_OFFSET,P48_CC_BYTES);memcpy(p48_control_copy,observed+P48_BATCH_CONTROL_OFFSET,P48_BATCH_CONTROL_BYTES);memcpy(p48_frozen_copy,observed+P48_BATCH_FROZEN_OFFSET,P48_BATCH_FROZEN_BYTES);memcpy(p48_initial_stop_copy,observed+P48_INITIAL_STOP_OFFSET,P48_INITIAL_STOP_BYTES);memcpy(p48_idle_copy,observed+P48_IDLE_HANDOFF_OFFSET,256);memcpy(p48_previous_idle_copy,observed+P48_PREVIOUS_IDLE_OFFSET,256);p48_canonical_copied=1;
 memcpy(&observed_result,observed+P39_RESULT_OFFSET,sizeof observed_result);memcpy(observed_diagnostics,observed+P39_DIAG_OFFSET,sizeof observed_diagnostics);memcpy(observed_stop,observed+P39_STOP_OFFSET,sizeof observed_stop);
 unsigned char digest[32];digest_bytes(observed_stop,sizeof observed_stop,digest);digest_hex(digest,stop_hash);digest_bytes(observed_diagnostics,sizeof observed_diagnostics,digest);digest_hex(digest,diagnostics_hash);digest_bytes(expected,P39_PROOF_BYTES,digest);digest_hex(digest,cap_expected_hash);digest_bytes(observed,P39_PROOF_BYTES,digest);digest_hex(digest,cap_observed_hash);
 int snapshot_ok=!dma_write_verified||p48_snapshot_valid(observed);
 int ok=save_bytes(dir,"p39_cap_expected.bin",expected,P39_PROOF_BYTES)&&save_bytes(dir,"p39_cap_observed.bin",observed,P39_PROOF_BYTES)&&save_bytes(dir,"p39_diagnostics.bin",observed_diagnostics,sizeof observed_diagnostics)&&save_bytes(dir,"p39_stop.bin",observed_stop,sizeof observed_stop);
 if(!save_bytes(dir,"p41_wb_audit.bin",p48_wb_copy,P41_BYTES)||!save_bytes(dir,"p48_cc_audit.bin",p48_cc_copy,P48_CC_BYTES)||!save_bytes(dir,"p48_batch_control.bin",p48_control_copy,P48_BATCH_CONTROL_BYTES)||!save_bytes(dir,"p48_frozen.bin",p48_frozen_copy,P48_BATCH_FROZEN_BYTES))ok=0;
 if(!save_bytes(dir,"p48_initial_stop.bin",p48_initial_stop_copy,P48_INITIAL_STOP_BYTES)||!save_bytes(dir,"p48_idle_handoff.bin",p48_idle_copy,256)||!save_bytes(dir,"p48_previous_idle.bin",p48_previous_idle_copy,256))ok=0;
 if(!save_bytes(dir,"p49_param_dma_audit.bin",observed+P49_PD_OFFSET,P49_PD_BYTES)||!save_bytes(dir,"still_lifecycle_audit.bin",observed+SLA_OFFSET,SLA_BYTES))ok=0;
 if(real_ready){if(!save_plane(dir,0)||!save_plane(dir,1))ok=0;}
 if(real_ready){
  unsigned char guards[3*16384];const unsigned at[]={P39_SOURCE_OFFSET+P39_SOURCE_BYTES,P39_TARGET_OFFSET-16384,P39_TARGET_OFFSET+P39_TARGET_BYTES};
  for(unsigned k=0;k<3;k++)memcpy(guards+k*16384,(const void*)(cap_alias+at[k]),16384);
  if(!save_bytes(dir,"p39_boundary_guards.bin",guards,sizeof guards)||!save_bytes(dir,"p39_plane_gap.bin",(const void*)(cap_alias+P39_TARGET_OFFSET+P39_PLANE_BYTES),P39_UV_OFFSET-P39_PLANE_BYTES))ok=0;
 }
 char path[1200];snprintf(path,sizeof path,"%s/p39_tiles.csv",dir);FILE *f=fopen(path,"wx");if(!f)ok=0;
 else{fputs("index,column,row,x,y,width,height,y_changed,uv_changed,y_rows,uv_rows\n",f);
  for(unsigned i=0;i<42;i++)fprintf(f,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",i,i%14,i/14,tiles[i].x,tiles[i].y,tiles[i].w,tiles[i].h,tiles[i].changed[0],tiles[i].changed[1],tiles[i].rows[0],tiles[i].rows[1]);
  if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;}
 ok=ok&&snapshot_ok;proof_snapshot_saved=(unsigned)ok;free(observed);return ok;
#endif
}
static int queued_console(const char *dir){
 uint64_t until=ns()+4000000000ULL;uint32_t tail=0;unsigned bytes=0,text_bytes=0;int ok=0;
 while(!stop_requested&&ns()<until){if(capture_console_text(queued_console_before,&tail,&bytes,&text_bytes)==1&&p33_monitor_state((const char*)console_text)>=0){ok=1;break;}pause_ms(20);}
 char path[1200];snprintf(path,sizeof path,"%s/p39_liveness_after.txt",dir);FILE *f=fopen(path,"wx");if(!f)return 0;if(fwrite(console_text,1,text_bytes,f)!=text_bytes)ok=0;if(fclose(f))ok=0;snprintf(path,sizeof path,"%s/p39_liveness_after.txt.raw",dir);f=fopen(path,"wx");if(!f)return 0;if(fwrite(console_raw,1,bytes,f)!=bytes)ok=0;if(fclose(f))ok=0;
 if(ok){console_outputs_confirmed++;console_bytes+=bytes;}return ok;
}
static int write_result(const char *dir,int rc,int liveness){
#ifdef NXL_FAST_PIPELINE
 char path[1200];snprintf(path,sizeof path,"%s/result.json",dir);FILE*f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"format\":\"NX_FAST_FRONT1\",\"exit\":%d,\"reason\":\"%s\",\"pa\":%u,\"va\":%u,\"nonce\":%u,\"cap_allocation_bytes\":%u,\"native_calls\":%u,\"ISP_completed\":%u,\"liveness_after\":%d,\"middle_pixel_files\":0,\"full_frame_audit\":false,",rc,reason,response[1],response[0],nonce,ownership_verified?P39_CAP_BYTES:0,native_calls,p49_native_done,liveness);
 const uint32_t *ba=p48_canonical_copied?(const uint32_t*)p48_wb_copy:NULL;
 const uint32_t *ca=p48_canonical_copied?(const uint32_t*)p48_cc_copy:NULL;
 fprintf(f,"\"readonly_guard_rejected\":%s,\"rtos_guard_verified_count\":%u,\"rtos_guard_mappings\":%u,\"rtos_guard_unmapped\":%u,\"rtos_guard_cross_page\":%u,",front_guard_rejected?"true":"false",rtos_guard_samples,rtos_guard_mappings,rtos_guard_unmapped,rtos_guard_crosspage);
 fprintf(f,"\"source_iq_evidence_saved\":%s,",source_iq_evidence_saved?"true":"false");
 fprintf(f,"\"source_cc_requested\":true,\"source_cc_submitted\":%s,\"source_cc_submitted_before_original_config_load\":%s,\"source_cc_hardware_pixel_effect_verified\":false,\"extra_parameter_reload_calls\":0,\"gamma_mode\":%u,\"gamma_setter_called\":%u,\"gamma_target_readback_match\":%u,\"gamma_restored\":%u,\"original_config_loads\":%u,\"gamma_pixel_effect_verified\":false,",ca&&ca[3]&&ca[68]&&!ca[67]?"true":"false",p48_canonical_copied&&gt_copy[GT_BEFORE_LOAD]?"true":"false",p48_canonical_copied?gt_copy[3]:99u,p48_canonical_copied?gt_copy[GT_CALLED]:0u,p48_canonical_copied?gt_copy[GT_TARGET_MATCH]:0u,p48_canonical_copied?gt_copy[GT_RESTORED]:0u,p48_canonical_copied?gt_copy[GT_CONFIG_LOADS]:0u);
 fprintf(f,"\"black_level_method\":\"native-WB-offset-profile-binding\",\"black_rggb\":[%u,%u,%u,%u],\"black_level_profile\":%u,\"black_level_submitted\":%s,\"black_offset_after_setter_match\":%u,\"black_offset_prestart_match\":%u,\"black_offset_poststart_match\":%u,\"black_offset_postcomplete_match\":%u,\"black_level_pixel_effect_verified\":false}\n",p41_info[16],p41_info[17],p41_info[18],p41_info[19],p41_info[62],ba&&ba[187]&&!ba[67]?"true":"false",ba&&(ba[96]==ba[160]&&ba[97]==ba[161]),ba&&(ba[112]==ba[160]&&ba[113]==ba[161]),ba&&(ba[228]==ba[160]&&ba[229]==ba[161]),ba&&(ba[128]==ba[160]&&ba[129]==ba[161]));
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
#else
 char path[1200];snprintf(path,sizeof path,"%s/result.json",dir);FILE *f=fopen(path,"wx");if(!f)return 0;const uint32_t *w=observed_result.w;unsigned configurations=0,attempts=0;for(unsigned v=1;v<=P39_CONFIG_VARIANTS;v++){const uint32_t *d=(const uint32_t*)observed_diagnostics+v*P39_DIAG_WORDS;if(d[0]==P39_DIAG_MAGIC&&d[3]==1){attempts++;configurations+=d[5]==0;}}
 fprintf(f,"{\n\"format\":\"NXIPCOWNED1\",\"version\":\"0.3.6-demo50\",\"exit\":%d,\"reason\":\"%s\",\"failure_stage\":\"%s\",\n",rc,reason,failure_stage);
 fprintf(f,"\"native_stop_reused\":%s,\"native_stop_calls\":%u,\n",p48_case_index?"true":"false",((const uint32_t*)(p48_control_copy+0x100u))[P48_R_STOP_CALLS]);
 fprintf(f,"\"case_index\":%u,\"case_id\":\"%s\",\"case_sha256\":\"%s\",\"plan_sha256\":\"%s\",\"source_wb_enabled\":%s,\"source_cc_enabled\":%s,\"wb_visibility_warning\":%s,\n",p48_case_index,p48_case->id,p48_case_hashes[p48_case_index],P48_PLAN_SHA256,p48_case->wb?"true":"false",p48_case->cc?"true":"false",(p48_reply[P48_R_FLAGS]&P48_REPLY_WB_VISIBILITY_WARNING)?"true":"false");
 fprintf(f,"\"source_cc_reloaded\":false,\"source_cc_requested\":true,\"wb_packet_schema\":3,\"source_origin_x\":0,\"source_origin_y\":0,\"cc_profile\":\"source-C0-18-signed11-before-config-load\",\n");
 const uint32_t *iq=p41_info;
 fprintf(f,"\"parameter_test\":\"P48_FROZEN_TEMPLATE_OR_SOURCE_WB_CC\",\"srw_iso\":%u,\"wb_q8_rggb\":[%u,%u,%u,%u],\"wb_audit_file\":\"p41_wb_audit.bin\",\"color_matrices_applied\":false,\"iso_noise_parameters_applied\":false,\n",iq[4],iq[8],iq[9],iq[10],iq[11]);
 const uint32_t *wa=p48_canonical_copied?(const uint32_t*)p48_wb_copy:NULL;
 fprintf(f,"\"wb_readback_verified\":%s,\"wb_secondary_setter_calls\":0,\"wb_mismatch_blocks_render\":false,\"wb_restore_before_native_cleanup\":true,\"native_wb_entry\":\"0x8017d33c\",\"native_wb_apply_status\":%u,\"native_wb_restore_status\":%u,\"wb_after_setter_match\":%u,\"wb_prestart_match\":%u,\"wb_after_start_match\":%u,\"wb_after_completion_match\":%u,\"wb_restore_readback_match\":%u,\"wb_secondary_observed_stable\":%u,\n",wa&&p43_wb_verified(wa,nonce)?"true":"false",wa?wa[67]:0,wa?wa[69]:0,wa?wa[75]:0,wa?wa[76]:0,wa?wa[78]:0,wa?wa[77]:0,wa?wa[66]:0,wa?wa[79]:0);
 fprintf(f,"\"source_original_name\":\"%s\",\"source_fixture_name\":\"%s\",\"collector_ready_protocol\":\"EARLY_ATOMIC\",\n",p48_source->asset_file,p48_source->asset_file);
 fprintf(f,"\"pa\":%u,\"va\":%u,\"nonce\":%u,\"cap_allocation_bytes\":%u,\"proof_bytes\":2097152,\"proof_windows\":[[0,1048576],[67108864,1048576]],\"free_before\":%u,\"free_owned\":%u,\"ownership_verified\":%u,\"ready_verified\":%u,\"input_unchanged\":%u,\"native_ipc_dma_writeback_verified\":%u,\"output_bytes\":%u,\"output_changed_bytes\":%u,\"output_rows_written\":%u,\"source_fnv\":%u,\"target_fnv\":%u,\"active_region_canaries_verified\":%u,\n",response[1],response[0],nonce,ownership_verified?P39_CAP_BYTES:0,free_before,free_owned,ownership_verified,ready_verified,input_unchanged,dma_write_verified,dma_write_verified?2*P39_PLANE_BYTES:0,output_changed_bytes,output_rows_written,source_fnv,target_fnv,cap_canaries_verified);
 fprintf(f,"\"output_payload_bytes\":%u,\"output_span_bytes\":%u,\"plane_stride\":%u,\"plane_capacity\":%u,\"chroma_offset\":%u,\"output_width\":%u,\"output_height\":%u,\"tile_columns\":14,\"tile_rows\":3,\"tile_width\":464,\"tile_height\":1472,\"output_nonzero_bytes\":%u,\"guard_changed_bytes\":%u,\"first_guard_change\":%u,\"canonical_result_from_proof\":%s,\n",2*P39_PLANE_BYTES,P39_TARGET_BYTES,P39_TARGET_STRIDE,P39_PLANE_BYTES,P39_UV_OFFSET,P39_W,P39_H,measured_nonzero_bytes,guard_changed_bytes,first_guard_change,proof_snapshot_saved?"true":"false");
 fprintf(f,"\"configuration_variants\":[\"YCC_native_fmt1\"],\"active_variant\":0,\"native_configurations_completed\":%u,\"native_configuration_attempts\":%u,\"luma_changed_bytes\":%u,\"chroma_changed_bytes\":%u,\"chroma_rows_written\":%u,\"chroma_incomplete_rows\":%u,\"chroma_prefix_ok\":%u,\n",configurations,attempts,luma_changed_bytes,chroma_changed_bytes,chroma_rows_written,chroma_incomplete_rows,chroma_prefix_ok);
 fprintf(f,"\"real_source_ready\":%u,\"rows_decoded\":%u,\"rows_reference_matched\":%u,\"layout_rows\":%u,\"source_sha256\":\"%s\",\"raw_bayer_sha256\":\"%s\",\"staged_before_sha256\":\"%s\",\"staged_after_sha256\":\"%s\",\"luma_sha256\":\"%s\",\"chroma_sha256\":\"%s\",\"luma_tiles_written\":%u,\"chroma_tiles_written\":%u,\"task_quiescent_verified\":%u,\"prepare_ms\":%u,\"measurement_ms\":%u,\n",real_ready,real_front.matched,real_front.matched,real_front.stored,real_front.source_hash,real_front.raw_hash,source_before_sha,source_after_sha,plane_sha[0],plane_sha[1],tiles_written[0],tiles_written[1],task_quiescent_verified,frame_prepare_ms,frame_measured_ms);
 fprintf(f,"\"diagnostic_bytes\":%u,\"diagnostic_sha256\":\"%s\",\"diagnostic_snapshots\":[",P39_DIAG_BYTES,diagnostics_hash);
 for(unsigned k=0;k<P39_DIAG_SLOTS;k++){const uint32_t *d=(const uint32_t*)observed_diagnostics+k*P39_DIAG_WORDS;fprintf(f,"%s[",k?",":"");for(unsigned i=0;i<P39_DIAG_WORDS;i++)fprintf(f,"%s%u",i?",":"",d[i]);fputs("]",f);}fputs("],\n",f);
 fprintf(f,"\"native_stop_completion_verified\":%u,\"native_stop_gate_ready_for_isp\":%u,\"native_stop_record_sha256\":\"%s\",\"native_stop_record_words\":[",p39_stop_ack_valid((const uint32_t*)observed_stop,nonce,response[1])?1u:0u,p48_stop_record_valid((const uint32_t*)observed_stop,(const uint32_t*)p48_initial_stop_copy,nonce,response[1],p48_case_index+1)?1u:0u,stop_hash);for(unsigned i=0;i<64;i++)fprintf(f,"%s%u",i?",":"",((const uint32_t*)observed_stop)[i]);fputs("],\n",f);
 fprintf(f,"\"native_result_words\":[");for(unsigned i=0;i<64;i++)fprintf(f,"%s%u",i?",":"",w[i]);fprintf(f,"],\n");
 fprintf(f,"\"native_calls\":%u,\"mcb_sends\":%u,\"native_packet_readbacks\":%u,\"cache_frame_attempts\":%u,\"console_outputs_confirmed\":%u,\"hook_restored\":%u,\"hook_native_restore_verified\":%u,\"temporary_dispatch_pointer_writes\":%u,\"original_code_restored\":%u,\"detached\":%u,\"idle_registers_verified\":%u,\"api_inflight_at_exit\":%u,\"camera_stopped_at_exit\":%u,\"camera_resumes\":%u,\"camera_resumed_verified\":%u,\"liveness_after\":%d,\n",native_calls,send_calls,native_packet_reads,cache_sends,console_outputs_confirmed,hook_restored,hook_native_restore_verified,slot_pointer_writes,code_restored,detached_ok,idle_registers_verified,api_inflight,camera_paused,camera_resumes,camera_resumed_verified,liveness);
 fprintf(f,"\"mmu_source_section\":%u,\"mmu_slot_section\":%u,\"rtos_code_guards_verified\":%u,\"cap_expected_sha256\":\"%s\",\"cap_observed_sha256\":\"%s\",\"elapsed_ms\":%u,\"allocation_retained_for_handoff\":%u,\"allocation_retained_until_restart\":0,\"cold_restart_required\":%u,\"isp_jobs_submitted\":%u,\"srw_decode_jobs\":%u,\"ep_jobs\":0,\"lut_jobs\":0,\"jpeg_outputs\":0,\"pixel_correctness_verified\":false,\"yuv_output_verified\":false,\"new_mcb_receivers\":0,\"original_rtos_text_writes\":0\n}\n",mmu_source,mmu_slot,rtos_readonly_guards,cap_expected_hash,cap_observed_hash,ms(),ownership_verified,ownership_verified,((w[3]>=6&&w[3]<=7)||(w[3]>=109&&w[3]<=113))?1:0,real_front.verified?1u:0u);
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
#endif
}
