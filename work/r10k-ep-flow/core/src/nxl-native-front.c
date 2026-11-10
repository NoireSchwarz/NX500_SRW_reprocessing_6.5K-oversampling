/* SPDX-License-Identifier: AGPL-3.0-only.
 * P39: original-MCB entry for one whole real-SRW frame, native14x3 grid.
 * Original Still config/start/IRQ calls; no EP. CAP retained until restart.
 */
#include "native_handoff_context.inc"
#include "srw_iq.h"
#include "cc_contract.h"
static uint32_t p41_info[256];
static uint32_t p48_request[32],p48_reply[64],p48_saved[32];
static unsigned char p48_wb_copy[P41_BYTES],p48_cc_copy[P48_CC_BYTES],p48_control_copy[P48_BATCH_CONTROL_BYTES],p48_frozen_copy[P48_BATCH_FROZEN_BYTES];
_Alignas(4) static unsigned char p48_initial_stop_copy[P48_INITIAL_STOP_BYTES];
_Alignas(4) static unsigned char p48_idle_copy[256],p48_previous_idle_copy[256];
static uint32_t p48_expected_previous_idle[64];
static unsigned p48_canonical_copied,p48_batch_warn,allocation_requests,frames_complete;
static unsigned p49_go_sent,p49_native_done;
static unsigned front_preflight_uninitialized,batch_no_submit,front_capacity_rejected,front_allocation_null,front_guard_rejected;
static const char *batch_directory;
#ifndef NXL_FAST_PIPELINE
static char frame_result_sha[P49_FRAME_COUNT][65],frame_luma_sha[P49_FRAME_COUNT][65],frame_chroma_sha[P49_FRAME_COUNT][65];
#endif
static int p48_progress(const char *stage);
static int camera_threads_stopped(void);
static void nxl_live_progress(const char*,const char*,const char*);
#include "source_select.inc"
#include "gamma_host.inc"
_Static_assert(ALLOC_BYTES==P39_CAP_BYTES && ALLOC_SPAN_BLOCKS*16384u==P39_CAP_BYTES,"native allocation geometry");
static uint32_t nonce,hook_installed,hook_ever_installed,hook_restored,hook_result_verified,copy_verified;
static uint32_t mmu_source,mmu_slot,source_fnv,target_fnv,cache_sends;
static uint32_t publish_cache_frames,publish_ordering_observed,hook_ram_restored,hook_native_restore_verified,slot_pointer_writes;
static volatile uint32_t *slot_page;static int writefd=-1;
static struct p39_result observed_result;
static unsigned camera_paused,camera_resumes,camera_resumed_verified,ready_verified,dma_write_verified,input_unchanged,output_changed_bytes,output_rows_written;
static int pause_watch_pipe=-1,pause_watch_pid=-1;
static uint32_t queued_console_before,queued_status_sent,liveness_attempted;
#include "p48_batch_host.inc"
#include "isp_compare_export.inc"
static int ro_words(uint32_t at,uint32_t *out,unsigned words){
 if(!words||words>32||(at&3)||at<0x80000000u||(uint64_t)at+words*4>0x94000000ULL)return 0;
 uint32_t base=at&~4095u;if((at&4095u)+words*4>4096)return 0;
 void *p=mmap(NULL,4096,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)base);if(p==MAP_FAILED)return 0;
 const volatile uint32_t *v=(const volatile uint32_t*)p+(at&4095u)/4;for(unsigned i=0;i<words;i++)out[i]=v[i];return munmap(p,4096)==0;
}
#include "pipeline_boot_preflight.inc"
#include "front_resource.inc"
#include "../../common/progress_emit.h"
#include "../../common/cmdq_evidence.h"
#include "front_timing.inc"
static int slot_original(void){uint32_t a[2],b[2];return ro_words(P39_SLOT,a,2)&&ro_words(P39_SLOT,b,2)&&!memcmp(a,b,8)&&a[0]==P39_ORIGINAL&&!a[1];}
static int alias_setup(const char *dir){
 if(!safe_call()||!p39_extent(response[1])||!slot_original())return 0;
 uint32_t pte=0x81000000u+4*(response[1]>>20),s=0x81000000u+4*(P39_SLOT>>20);
 if(!ro_words(pte,&mmu_source,1)||!ro_words(s,&mmu_slot,1))return 0;
 char path[1200];snprintf(path,sizeof path,"%s/p39_layout.txt",dir);FILE *audit=fopen(path,"wx");if(!audit)return 0;
 fprintf(audit,"owned_pa=0x%08x bytes=201326592 proof_bytes=2097152 code_bytes=%u slot=0x%08x original=0x%08x\nsource_pte_address=0x%08x value=0x%08x slot_pte_address=0x%08x value=0x%08x\nsource_kind=%d slot_kind=%d TRE=1 PRRR=0x000a8aa4 NMRR=0x44e048e0\n",response[1],P39_CODE_BYTES,P39_SLOT,P39_ORIGINAL,pte,mmu_source,s,mmu_slot,p39_section_kind(mmu_source,response[1]),p39_section_kind(mmu_slot,P39_SLOT));
 if(fclose(audit))return 0;
 if(!p39_section_uncached_executable(mmu_source,response[1])||!p39_section_kind(mmu_slot,P39_SLOT))return 0;
 for(uint32_t base=response[1]&~0xfffffu;base<(uint64_t)response[1]+P39_CAP_BYTES;base+=0x100000u){uint32_t desc;if(!ro_words(0x81000000u+4*(base>>20),&desc,1)||p39_section_kind(desc,base)!=1)return 0;}
 sma_fd=open("/dev/d5_sma",O_RDWR|O_SYNC|O_CLOEXEC);if(sma_fd<0)return 0;
 int flags=fcntl(sma_fd,F_GETFL);if(flags<0||((unsigned)flags&0x101000u)!=0x101000u)return 0;
 void *p=mmap(NULL,P39_CAP_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,sma_fd,(off_t)(uint64_t)response[1]);if(p==MAP_FAILED)return 0;cap_alias=p;
 nonce=(uint32_t)ns()^(uint32_t)getpid()^0x37d04981u;if(!nonce)nonce=1;
 for(unsigned i=0;i<P39_PROOF_BYTES;i++)expected[i]=(unsigned char)((i*37u+(i>>5)+0x53u)^(nonce>>((i&3)*8)));
 memcpy(expected,p39_hook_code,sizeof p39_hook_code);p48_images(expected,response[1],nonce,P39_IRQ_OFFSET,p48_case_index);
 p41_info[2]=nonce;p41_info[63]=p41_checksum(p41_info,63);memcpy(expected+P41_OFFSET,p41_info,P41_BYTES);
 int32_t coefficients[18];for(unsigned k=0;k<18;k++)coefficients[k]=p48_source->c0_cc_packed[k/9][k%9];
 memset(expected+P48_CC_OFFSET,0,P48_CC_BYTES);if(!p48_cc_make_request((uint32_t*)(expected+P48_CC_OFFSET),nonce,p48_case_index+1,p48_case->source,p48_source->iso,p48_case->cc,coefficients))return 0;
 memset(expected+P48_BATCH_CONTROL_OFFSET,0,P48_BATCH_CONTROL_BYTES+P48_BATCH_FROZEN_BYTES+P48_INITIAL_STOP_BYTES+512);
 if(!gt_host_prepare(nonce))return 0;
 for(unsigned i=0;i<P39_PROOF_BYTES;i++)cap_alias[i<P39_WINDOW_BYTES?i:P39_TARGET_OFFSET+i-P39_WINDOW_BYTES]=expected[i];
 __sync_synchronize();
 for(unsigned i=0;i<P39_PROOF_BYTES;i++)if(cap_alias[i<P39_WINDOW_BYTES?i:P39_TARGET_OFFSET+i-P39_WINDOW_BYTES]!=expected[i])return 0;
 p=mmap(NULL,P33_CONSOLE_BYTES,PROT_READ,MAP_SHARED,devfd,(off_t)P33_CONSOLE_BASE);if(p==MAP_FAILED)return 0;console_map=p;
 snprintf(path,sizeof path,"%s/p39_alias_maps.txt",dir);if(!save_copy("/proc/self/maps",path))return 0;
 return volatile_word(console_map)<P33_CONSOLE_CAPACITY&&volatile_word(console_map+4)<P33_CONSOLE_CAPACITY;
}
#include "capture_console.inc"
static int hook_publish(void){
 if(!safe_call()||!publish_ordering_observed||publish_cache_frames!=1||!slot_original())return 0;
 writefd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(writefd<0)return 0;
 void *p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,writefd,(off_t)(uint64_t)(P39_SLOT&~4095u));if(p==MAP_FAILED)return 0;slot_page=p;
 volatile uint32_t *slot=slot_page+(P39_SLOT&4095u)/4;
 if(slot[0]!=P39_ORIGINAL||slot[1])return 0;
 /* Preclean has passed the receiver ordering observation. Publish to RAM,
  * then invalidate stale clean RTOS copies with a second fixed cache frame. */
 hook_ever_installed=hook_installed=1;__sync_synchronize();slot[0]=response[1];slot_pointer_writes++;__sync_synchronize();return slot[0]==response[1]&&!slot[1];
}
static int hook_restore(void){
 if(!hook_ever_installed)return 1;if(!slot_page)return 0;
 volatile uint32_t *slot=slot_page+(P39_SLOT&4095u)/4;
 if(slot[1])return 0;if(slot[0]==response[1]){slot[0]=P39_ORIGINAL;slot_pointer_writes++;__sync_synchronize();}
 if(slot[0]!=P39_ORIGINAL)return 0;hook_installed=0;hook_ram_restored=1;
 /* A Linux RAM repair is not evidence of a clean RTOS D-cache. Code stays
  * allocated until restart; only verify_result may certify native restore. */
 return 1;
}
static int send_cache_frame(unsigned purpose){
 unsigned off=0;uint32_t payload[3];if(purpose>2||!safe_call()||!p39_cache_packet(response[1],nonce,payload)||!p39_packet_extent(packet_count,12,&off))return 0;
 if(purpose==2)payload[0]=p48_batch_entry_token(nonce,p48_case_index+1);
 if(purpose<2){payload[0]=p48_batch_entry_token(nonce,p48_case_index+1)^(purpose?0x37a20002u:0x37a10001u);payload[1]=P39_SLOT_PAGE;payload[2]=4096;}
 if((purpose==0&&hook_ever_installed)||(purpose==1&&(!hook_installed||publish_cache_frames!=1))||(purpose==2&&(!hook_installed||publish_cache_frames!=2)))return 0;
 memcpy(expected+off,payload,12);for(unsigned i=0;i<12;i++)cap_alias[off+i]=expected[off+i];__sync_synchronize();
 struct alloc_regs r;unsigned char readback[12];uint32_t scratch=receive_buffer+48;
 trace_phase="native_cache_packet_readback";
 if(!invoke3(libc_base+ALLOC_MEMCPY,scratch,response[0]+off,12,3000)||!get_regs(&r)||r.r[0]!=scratch||!read_process(scratch,readback,12)||memcmp(readback,payload,12))return 0;
 native_packet_reads++;
 trace_phase="original_cache_attribute_send";packet_count++;cache_sends++;
 if(!invoke4(mcb_base+P33_MCB_SEND,0x81,399,12,response[0]+off,3000)||!get_regs(&r)||r.r[0])return 0;
 send_calls++;if(purpose<2)publish_cache_frames++;
 fprintf(command_log,"CACHE_FRAME purpose=%u category=129 id=399 bytes=12 nonce=%08x source=%08x length=%u; send_return_is_not_completion\n",purpose,payload[0],payload[1],payload[2]);fflush(command_log);return safe_call();
}
static int result_copy(struct p39_result *r){
 if(!cap_alias)return 0;for(unsigned i=0;i<sizeof *r;i++)((unsigned char*)r)[i]=cap_alias[P39_RESULT_OFFSET+i];__sync_synchronize();return 1;
}
static int camera_threads_stopped(void);
#include "real_front.inc"
#include "front_control.inc"
#include "collector_ready.inc"
#include "front_handoff_host.inc"
#include "front_workspace.inc"
static int unmap_aliases(void){int ok=1;
 if(slot_page){if(munmap((void*)slot_page,4096))ok=0;slot_page=NULL;}
 if(writefd>=0){if(close(writefd))ok=0;writefd=-1;}
 if(console_map){if(munmap((void*)console_map,P33_CONSOLE_BYTES))ok=0;console_map=NULL;}
 if(cap_alias){if(munmap((void*)cap_alias,P39_CAP_BYTES))ok=0;cap_alias=NULL;}
 if(sma_fd>=0){if(close(sma_fd))ok=0;sma_fd=-1;}return ok;
}
static int p48_refresh_template(void){
 if(!cap_alias||!still_owned()||!slot_original()||memcmp((const void*)cap_alias,expected,P39_SOURCE_OFFSET))return 0;
 for(unsigned i=0;i<p48_case_index;i++){unsigned off=P48_BATCH_LISTENER_OFFSET+i*P48_BATCH_LISTENER_STRIDE;if(memcmp((const void*)(cap_alias+off),expected+off,P48_BATCH_LISTENER_STRIDE))return 0;}
 p48_images(expected,response[1],nonce,P39_IRQ_OFFSET,p48_case_index);p41_info[2]=nonce;p41_info[63]=p41_checksum(p41_info,63);memcpy(expected+P41_OFFSET,p41_info,P41_BYTES);
 int32_t cc[18];for(unsigned k=0;k<18;k++)cc[k]=p48_source->c0_cc_packed[k/9][k%9];memset(expected+P48_CC_OFFSET,0,P48_CC_BYTES);
 if(!p48_cc_make_request((uint32_t*)(expected+P48_CC_OFFSET),nonce,p48_case_index+1,p48_case->source,p48_source->iso,p48_case->cc,cc))return 0;
 const unsigned ranges[][2]={{P39_CONTEXT_OFFSET,sizeof(struct p39_context)},{P39_RESULT_OFFSET,256},{P39_JOB_OFFSET,128},{P39_INPUT_IMAGE_OFFSET,176},{P39_OUTPUT_IMAGE_OFFSET,176},{P39_DIAG_OFFSET,P39_DIAG_BYTES},{SLA_OFFSET,SLA_BYTES},{P39_STOP_OFFSET,P39_STOP_BYTES},{P39_STOP_COMMAND_OFFSET,32},{P41_OFFSET,P41_BYTES},{P48_CC_OFFSET,P48_CC_BYTES}};
 for(unsigned k=0;k<sizeof ranges/sizeof ranges[0];k++)for(unsigned i=0;i<ranges[k][1];i++)cap_alias[ranges[k][0]+i]=expected[ranges[k][0]+i];
 unsigned listener=P48_BATCH_LISTENER_OFFSET+p48_case_index*P48_BATCH_LISTENER_STRIDE;for(unsigned i=0;i<P48_BATCH_LISTENER_STRIDE;i++)cap_alias[listener+i]=expected[listener+i];
 /* Keep previous DONE, previous SAVED and immutable first-template untouched. */
 volatile uint32_t *abort_area=(volatile uint32_t*)(cap_alias+P48_BATCH_CONTROL_OFFSET+0x200);for(unsigned i=0;i<16;i++)abort_area[i]=0;
 __sync_synchronize();return still_owned();
}
static void p48_reset_frame(void){
 source_iq_evidence_saved=0;
 front_preflight_uninitialized=front_guard_rejected=0;front_capacity_rejected=front_allocation_null=0;pipeline_jpeg_missing=pipeline_jpeg_checked=0;
 p49_go_sent=p49_native_done=0;
 if(p48_case_index&&cap_alias)memcpy(p48_expected_previous_idle,(const void*)(cap_alias+P48_IDLE_HANDOFF_OFFSET),256);else memset(p48_expected_previous_idle,0,256);
 hook_installed=hook_ever_installed=hook_restored=hook_result_verified=copy_verified=0;
 publish_cache_frames=publish_ordering_observed=hook_ram_restored=hook_native_restore_verified=slot_pointer_writes=0;
 packet_count=send_calls=native_packet_reads=cache_sends=console_outputs_confirmed=console_bytes=0;
 native_calls=entry_intercepted=code_restored=detached_ok=parked_registers_verified=idle_registers_verified=call_boundary_active=0;
 thread_candidates=proc_syscall_matched=0;camera_resumes=camera_resumed_verified=ready_verified=dma_write_verified=input_unchanged=output_changed_bytes=output_rows_written=0;
 queued_console_before=queued_status_sent=liveness_attempted=0;real_opened=real_ready=real_measured=task_quiescent_verified=0;
 source_staged_fnv=frame_measured_ms=frame_prepare_ms=source_fnv=target_fnv=0;proof_snapshot_saved=measured_nonzero_bytes=guard_changed_bytes=0;first_guard_change=UINT32_MAX;
 source_before_sha[0]=source_after_sha[0]=plane_sha[0][0]=plane_sha[1][0]=cap_expected_hash[0]=cap_observed_hash[0]=stop_hash[0]=diagnostics_hash[0]=0;
 memset(&observed_result,0,sizeof observed_result);memset(observed_stop,0,sizeof observed_stop);memset(observed_diagnostics,0,sizeof observed_diagnostics);memset(&real_front,0,sizeof real_front);
 memset(p48_wb_copy,0,sizeof p48_wb_copy);memset(p48_cc_copy,0,sizeof p48_cc_copy);memset(p48_control_copy,0,sizeof p48_control_copy);memset(p48_frozen_copy,0,sizeof p48_frozen_copy);p48_canonical_copied=0;memset(p48_initial_stop_copy,0,sizeof p48_initial_stop_copy);memset(p48_idle_copy,0,sizeof p48_idle_copy);memset(p48_previous_idle_copy,0,sizeof p48_previous_idle_copy);
 failure_stage="";trace_error=NULL;trace_phase="frame_preflight";reason="NOT_STARTED";
}
static int p49_hold_uncertain_frame(void){
 p48_parked_camera=1;camera_paused=1;int ok=pid>1&&!kill(pid,SIGSTOP);
 if(pause_watch_pipe>=0){char c='P';if(write(pause_watch_pipe,&c,1)!=1)ok=0;close(pause_watch_pipe);pause_watch_pipe=-1;}
 if(pause_watch_pid>0){int status;pid_t got;do{got=waitpid(pause_watch_pid,&status,0);}while(got<0&&errno==EINTR);
  if(got!=pause_watch_pid||!WIFEXITED(status)||WEXITSTATUS(status))ok=0;pause_watch_pid=-1;}
 if(!p48_atomic_text(batch_directory,"P49_RESTART_REQUIRED","NXP49RESTART1\nreason=NATIVE_FRAME_NOT_VERIFIED\nno_auto_resume=1\ncold_restart_required=1\n"))ok=0;
 event(ok?"UNCERTAIN_FRAME_HELD_COLD_RESTART_REQUIRED":"UNCERTAIN_FRAME_HOLD_UNCONFIRMED");return ok;
}
static int p48_run_frame(const char *dir){
 front_mark("FRAME_BEGIN");
 uint64_t frame_start=ns();int rc=1,queue=-1,op_ok=0,cleanup_ok=1,liveness=0;uint32_t ptr=0,mapped_pa=0;struct alloc_regs regs;struct alloc_message message;
#define REQUIRE(c,m) do{if(!(c)){reason=(m);event(reason);goto finish;}}while(0)
#define REQUIRE_TRACE(c,m) do{if(!(c)){failure_stage=(m);reason=trace_error?trace_error:(m);event(reason);goto finish;}}while(0)
 REQUIRE(!attached&&!camera_paused&&!api_inflight&&!native_signal,"PREVIOUS_FRAME_CONTEXT_NOT_RELEASED");
 REQUIRE(p48_progress("PREPARE"),"PROGRESS_SAVE_FAILED");
 REQUIRE(real_open(dir),real_error);if(p48_selected_ready)memcpy(p41_info,selected_iq,sizeof selected_iq);else REQUIRE(p41_srw_info(real_front.source,p41_info),"SRW_METADATA_INVALID");
 REQUIRE(p41_info[4]==p48_source->iso&&!memcmp(p41_info+8,p48_source->wb_q8,16)&&!memcmp(p41_info+16,p48_source->black_rggb,16),"SRW_METADATA_DESCRIPTOR_MISMATCH");
 p41_info[48]=p48_case_index;p41_info[49]=p48_case->wb;p41_info[50]=p48_case->cc;p41_info[63]=p41_checksum(p41_info,63);
 REQUIRE(load_maps(dir,"begin")&&app_guards()&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof *alloc_cap_guards)&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof *alloc_shell_guards)&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof *alloc_libc_guards),"RUNTIME_CODE_OR_MAP_GUARD_FAILED");
 REQUIRE(read_word(shell_base+0x1e33c+15*8+4,&ptr)&&ptr==shell_base+ALLOC_ENTRY,"NATIVE_APP_DISPATCH_CHANGED");
 if(!ro_native_guards(dir)){front_guard_rejected=1;REQUIRE(0,bridge_error);}
 if(!front_initial_ready(dir)){
  front_preflight_uninitialized=fp_uninitialized(&front_readiness_last);
  REQUIRE(0,front_preflight_uninitialized?"FRONT_NEEDS_STILL_INIT":"NATIVE_STILL_NOT_IDLE_OR_UNINITIALIZED");
 }
 REQUIRE(pipeline_jpeg_preflight(dir),pipeline_jpeg_missing?"PIPELINE_NEEDS_BOOT_JPEG_INIT":"PIPELINE_JPEG_CONTEXT_NOT_READY");
 (void)cq_capture(dir,"BEFORE_SRW");
 REQUIRE(slot_original(),"NATIVE_CACHE_DISPATCH_SLOT_NOT_ORIGINAL");REQUIRE(native_bridge_context(dir),"MCB_RECEIVER_CONTEXT_NOT_VERIFIED");
 queue=queue_id();REQUIRE(queue>=0,"NATIVE_COMMAND_QUEUE_BUSY_OR_UNAVAILABLE");tid=find_worker(queue,dir);REQUIRE(tid>0,"NATIVE_SHELL_THREAD_NOT_UNIQUE");
 front_mark("READINESS_VERIFIED");nxl_live_progress("PIPE_WORKSPACE","RUNNING","Checking and reserving the shared workspace");
 if(!ownership_verified){
  if(!snapshot(dir)){front_capacity_rejected=!strcmp(allocator_snapshot_reason,"ALLOCATOR_BANK2_NO_CONTIGUOUS_EXTENT");REQUIRE(0,allocator_snapshot_reason);}
 }else REQUIRE(still_owned(),"RETAINED_CAP_OWNERSHIP_CHANGED");
 patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;
 REQUIRE(read_word(patches[0].address,&patches[0].original)&&read_word(patches[1].address,&patches[1].original)&&patches[0].original==0xe92d4008&&patches[1].original==0xe8bd85f8,"CALL_BOUNDARY_INSTRUCTION_MISMATCH");
 REQUIRE(watchdog_start(),"ROLLBACK_WATCHDOG_UNAVAILABLE");
 REQUIRE(!stop_requested&&ptrace(PTRACE_ATTACH,tid,0,0)==0,"NATIVE_WORKER_ATTACH_REFUSED");attached=1;trace_phase="initial_attach";REQUIRE_TRACE(wait_stop(3000,SIGSTOP),"NATIVE_WORKER_ATTACH_STOP_FAILED");
 REQUIRE(other_threads_running()&&verify_parked(dir,"parked",queue,0),"NATIVE_MSGRCV_REGISTERS_NOT_CONFIRMED");parked_registers_verified=1;
 REQUIRE(arm_patch(0),"ENTRY_BREAKPOINT_NOT_INSTALLED");alloc_make_message(&message,(uint32_t)getpid());
 REQUIRE(queue_id()==queue,"CONTROLLED_COMMAND_QUEUE_CHANGED");REQUIRE(watchdog_mode('H'),"WATCHDOG_HOLD_ARM_FAILED");p48_unverified_shell_message=1;
 if(msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT)){p48_unverified_shell_message=0;watchdog_mode('V');REQUIRE(0,"CONTROLLED_COMMAND_NOT_QUEUED");}trace_phase="command_entry";
 REQUIRE_TRACE(resume()&&wait_stop(5000,SIGTRAP)&&verify_breakpoint(0),"CONTROLLED_COMMAND_ENTRY_STOP_FAILED");REQUIRE(get_regs(&regs)&&own_command(&message,&regs),"CONTROLLED_COMMAND_CONTEXT_MISMATCH");
 entry_intercepted=call_boundary_active=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;
 /* Make the original app command non-executable BEFORE releasing watchdog H.
  * Native invoke3 later supplies its own PC using this verified saved frame. */
 struct alloc_regs consumed=call_original;consumed.r[0]=0;consumed.r[15]=shell_base+ALLOC_RETURN;consumed.r[17]=UINT32_MAX;
 REQUIRE(set_regs(&consumed),"VERIFIED_COMMAND_SKIP_RETURN_FAILED");p48_unverified_shell_message=0;REQUIRE(watchdog_mode('V'),"WATCHDOG_VERIFIED_MODE_FAILED");
 REQUIRE(save_regs(dir,"entry",&regs)&&restore_patch(0)&&arm_patch(1),"CALL_BOUNDARY_PREPARATION_FAILED");
 if(!ownership_verified){
  REQUIRE(p33_guard_claim(batch_directory),p33_guard_reason);trace_phase="request_return";allocation_requests++;front_resource.request_started=1;REQUIRE(front_resource_emit(NR_ALLOCATING),"RESOURCE_REQUEST_INTENT_SAVE_FAILED");
  REQUIRE_TRACE(invoke(cap_base+ALLOC_REQUEST,receive_buffer+48,8000),"REQUEST_RESPONSE_STOP_FAILED");REQUIRE(get_regs(&regs)&&read_process(receive_buffer+48,response,sizeof response),"REQUEST_RESULT_READ_FAILED");request_done=1;front_resource.response_valid=1;front_resource.response_status=regs.r[0];REQUIRE(front_resource_emit(NR_REQUEST_RETURNED),"RESOURCE_RESPONSE_SAVE_FAILED");
  {char out[1200];snprintf(out,sizeof out,"%s/allocator-request-result.bin",dir);FILE*f=fopen(out,"wx");REQUIRE(f,"REQUEST_RESULT_AUDIT_OPEN_FAILED");int saved=fwrite(response,1,sizeof response,f)==sizeof response&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))saved=0;REQUIRE(saved,"REQUEST_RESULT_AUDIT_SAVE_FAILED");}
  fprintf(log_file,"CAP_RESPONSE status=%u va=0x%08x pa=0x%08x bytes=%u bank=%u flags=%u\n",regs.r[0],response[0],response[1],response[2],response[3],response[4]);fflush(log_file);
  if(!regs.r[0]&&!response[0]&&!response[1]&&response[2]==ALLOC_BYTES&&response[3]==2){front_allocation_null=1;REQUIRE(0,"PIPELINE_BANK2_REQUEST_RETURNED_NULL");}
  uint32_t meta[4];REQUIRE(regs.r[0]==0&&translate(response[0],&mapped_pa,&index_owned)&&entry(index_owned,meta)&&alloc_response(response,mapped_pa,index_owned,baseline,meta),"REQUEST_OWNERSHIP_NOT_CONFIRMED");memcpy(owned_meta,meta,sizeof meta);ownership_verified=1;REQUIRE(pool_head(&free_owned),"POOL_CHANGED_AFTER_REQUEST");
  REQUIRE(alias_setup(dir),"OWNED_ALIAS_MMU_OR_SLOT_PREFLIGHT_FAILED_CAP_RETAINED");REQUIRE(front_resource_emit(NR_OWNED),"RESOURCE_OWNERSHIP_SAVE_FAILED");
 }else REQUIRE(p48_refresh_template(),"RETAINED_CAP_TEMPLATE_REFRESH_REFUSED");
 front_mark("WORKSPACE_READY");REQUIRE(real_prepare(),real_error);front_mark("DECODE_END");REQUIRE(p48_prepare_query(),"BATCH_QUERY_PREPARATION_FAILED");
 REQUIRE(console_command(P33_MON_STATUS,dir,"p39_liveness_before.txt"),"ORIGINAL_CONSOLE_LIVENESS_FAILED");
 REQUIRE(p48_progress("CONFIGURE"),"PROGRESS_SAVE_FAILED");
 REQUIRE(send_cache_frame(0),"SLOT_PRECLEAN_SEND_FAILED");REQUIRE(console_command(P33_MON_STATUS,dir,"p39_publish_ordering.txt"),"SLOT_PRECLEAN_ORDERING_NOT_OBSERVED");publish_ordering_observed=1;
 REQUIRE(hook_publish(),"DISPATCH_POINTER_PUBLISH_FAILED");REQUIRE(send_cache_frame(1),"SLOT_POSTPUBLISH_SEND_FAILED");REQUIRE(send_cache_frame(2),"PROBE_CACHE_PACKET_SEND_FAILED");
 REQUIRE(wait_ready()&&p48_read_reply(P48_PHASE_READY),"RTOS_TASK_PREPARE_FAILED");
 queued_console_before=volatile_word(console_map+4);REQUIRE(send_packet(p33_command_text(P33_MON_STATUS)),"POST_TASK_CONSOLE_QUEUE_FAILED");queued_status_sent=1;
 REQUIRE(leave_native_worker(dir,queue),"NATIVE_WORKER_RESTORE_BEFORE_TASK_FAILED");
 REQUIRE(pause_camera(),"CAMERA_PAUSE_FAILED_NO_GO_SENT");REQUIRE(front_idle(),"CS1_NOT_IDLE_AFTER_CAMERA_PAUSE");pause_ms(150);REQUIRE(front_idle(),"CS1_CHANGED_DURING_HANDOFF");
 REQUIRE(p48_publish_query(),"PUBLISHED_QUERY_VALIDATION_FAILED");
 {char flight='I';REQUIRE(pause_watch_pipe>=0&&write(pause_watch_pipe,&flight,1)==1,"INFLIGHT_GUARDIAN_ARM_FAILED");}
 p49_go_sent=1;REQUIRE(front_resource_emit(NR_FRONT_GO),"RESOURCE_GO_INTENT_SAVE_FAILED");
 ((volatile uint32_t*)(cap_alias+P39_RESULT_OFFSET))[16]=nonce^P39_GO_XOR;__sync_synchronize();event("NATIVE_IPC_GO_AFTER_CAMERA_STOP");
 REQUIRE(p48_progress("RENDER"),"PROGRESS_SAVE_FAILED");REQUIRE(wait_done()&&p48_read_reply(P48_PHASE_DONE)&&real_native_completion(),"NATIVE_IPC_FRAME_OR_BATCH_REPLY_NOT_VERIFIED");
 p49_native_done=1;REQUIRE(front_resource_emit(NR_FRONT_DONE),"RESOURCE_FRONT_COMPLETION_SAVE_FAILED");
 REQUIRE(isp_compare_export(dir),"ISP_COMPARE_AUDIT_EXPORT_FAILED");
 REQUIRE(resume_camera(),"TASK_VERIFIED_CAMERA_RESUME_FAILED");liveness_attempted=1;REQUIRE(queued_console(dir),"TASK_VERIFIED_ORIGINAL_CONSOLE_LIVENESS_MISSING");liveness=1;
 nxl_live_progress("PIPE_STILL_SETTLE","RUNNING","Still completed; verifying stable ownership and idle state");
 REQUIRE(p48_progress("HANDOFF_SETTLE"),"PROGRESS_SAVE_FAILED");
 REQUIRE(front_handoff_released(dir,"postframe"),front_handoff_error);
 (void)cq_capture(dir,"AFTER_STILL");
 REQUIRE(p48_progress("READBACK"),"PROGRESS_SAVE_FAILED");REQUIRE(verify_result(),"REAL_SRW_FULL_FRAME_COVERAGE_OR_GUARD_CHECK_FAILED");op_ok=1;
 p48_batch_warn|=(p48_reply[P48_R_FLAGS]&P48_REPLY_WB_VISIBILITY_WARNING)!=0;reason="P48_NATIVE_FRAME_COMPLETE";rc=0;
finish:
 if(p49_go_sent&&!p49_native_done){if(!p49_hold_uncertain_frame())cleanup_ok=0;reason="NATIVE_FRAME_UNVERIFIED_COLD_RESTART_REQUIRED";}
 if(p48_unverified_shell_message){if(!p48_park_uncertain_command())cleanup_ok=0;reason="UNVERIFIED_SHELL_MESSAGE_COLD_RESTART_REQUIRED";}
 if(cap_alias&&!op_ok)p48_abort(1);
 if(!resume_camera())cleanup_ok=0;
 if(!p48_parked_camera&&queued_status_sent&&!liveness_attempted){liveness_attempted=1;liveness=queued_console(dir);}
 if(cap_alias&&real_ready)measure_buffers();if(!hook_restore())cleanup_ok=0;if(!save_proof(dir))cleanup_ok=0;
 if(call_boundary_active&&!api_inflight&&trace_stopped&&!native_signal){regs=call_original;regs.r[0]=0;regs.r[15]=shell_base+ALLOC_RETURN;regs.r[17]=0xffffffffu;if(!set_regs(&regs))cleanup_ok=0;}
 if(!p48_parked_camera&&!cleanup_trace())cleanup_ok=0;
 if(real_front.source||real_front.reference||real_front.rows)if(!p32_front_close(&real_front))cleanup_ok=0;
 if(!p48_parked_camera&&!idle_registers_verified&&detached_ok&&queue>=0&&(ownership_verified||front_allocation_null)){if(stabilize_worker(dir,queue))idle_registers_verified=1;else cleanup_ok=0;}
 if(!cleanup_ok){rc=1;reason="FRAME_CLEANUP_INCOMPLETE";}
 /* This boundary precedes queue submission, ptrace and every native call.
  * Recheck all activity counters after cleanup; an interrupted/partial run
  * never receives the dedicated no-submit exit status. */
 struct fp_no_submit preflight={front_preflight_uninitialized,(unsigned)cleanup_ok,
  {allocation_requests,request_done,ownership_verified,p33_guard_acquired,
   native_calls,send_calls,cache_sends,slot_pointer_writes,hook_ever_installed,
   attached,camera_paused,api_inflight,entry_intercepted,call_boundary_active,
   p49_go_sent,p48_unverified_shell_message,(unsigned)p48_parked_camera}};
 if(rc&&fp_safe_no_submit(&preflight))rc=42;
 /* A read-only firmware check cannot have started a native job. Dedicated
  * status46 requires every activity counter and cleanup to prove that fact. */
 if(rc&&fp_safe_guard_rejection(&preflight,front_guard_rejected))rc=46;
 /* Both read-only capacity rejection and a fully returned null request
  * produce no owned CAP, no MCB or ISP work. Never classify partial calls. */
 unsigned no_work=!ownership_verified&&!api_inflight&&!attached&&!camera_paused&&!native_signal&&!p49_go_sent&&!send_calls&&!cache_sends&&!hook_ever_installed&&!slot_pointer_writes&&!p48_unverified_shell_message&&!p48_parked_camera;
 if(rc&&cleanup_ok&&no_work){
  if(pipeline_jpeg_checked&&pipeline_jpeg_missing&&!native_calls&&!allocation_requests)rc=42;
  if(front_capacity_rejected&&!native_calls&&!allocation_requests)rc=43;
  if(front_allocation_null&&native_calls==1&&allocation_requests==1&&code_restored&&detached_ok&&idle_registers_verified&&front_return_unallocated_grant())rc=43;
 }
 if(!write_result(dir,rc,liveness))rc=1;
#ifdef NXL_FAST_PIPELINE
 if(!rc){frames_complete++;if(!p48_progress("FRONT_COMPLETE_IN_MEMORY"))rc=1;}
#else
 if(!rc){
  char path[1200];snprintf(path,sizeof path,"%s/result.json",dir);if(!p48_file_sha(path,frame_result_sha[p48_case_index])||!p48_publish_saved(dir))rc=1;
  if(!rc){strcpy(frame_luma_sha[p48_case_index],plane_sha[0]);strcpy(frame_chroma_sha[p48_case_index],plane_sha[1]);frames_complete++;if(!p48_progress("CASE_SAVED"))rc=1;}
 }
#endif
 if(slot_page){if(munmap((void*)slot_page,4096))rc=1;slot_page=NULL;}if(writefd>=0){if(close(writefd))rc=1;writefd=-1;}
 fprintf(log_file,"FRAME_END index=%u elapsed_ms=%llu result=%d phase=%u reason=%s\n",p48_case_index,(unsigned long long)((ns()-frame_start)/1000000u),rc,observed_result.w[3],reason);fflush(log_file);event(rc?"P48_FRAME_STOPPED_NO_RETRY":"P48_FRAME_SAVED_AND_REENTRY_RELEASED");return rc;
#undef REQUIRE
#undef REQUIRE_TRACE
}
static int p48_write_batch_result(const char *dir,int rc){
#ifdef NXL_FAST_PIPELINE
 char path[1200],text[1000];snprintf(path,sizeof path,"%s/result.json",dir);
 int n=snprintf(text,sizeof text,"{\"format\":\"NX_FAST_FRONT_BATCH1\",\"exit\":%d,\"reason\":\"%s\",\"frames_complete\":%u,\"allocation_requests\":%u,\"cap_allocation_bytes\":%u,\"pa\":%u,\"va\":%u,\"nonce\":%u,\"middle_pixel_files\":0,\"full_frame_audit\":false}\n",rc,reason,frames_complete,allocation_requests,ownership_verified?P39_CAP_BYTES:0,response[1],response[0],nonce);
 return n>0&&(unsigned)n<sizeof text&&sw_file_new(path,text,(size_t)n);
#else
 char path[1200],text[2048];snprintf(path,sizeof path,"%s/result.json",dir);FILE *f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\n\"format\":\"NXIPC_DEMO49\",\"version\":\"0.3.6-demo50\",\"exit\":%d,\"complete\":%u,\"plan_sha256\":\"%s\",\"frames_expected\":1,\"frames_complete\":%u,\"allocation_requests\":%u,\"cap_allocation_bytes\":%u,\"allocation_retained_for_handoff\":%s,\"allocation_retained_until_restart\":false,\"cold_restart_required\":%s,\"wb_visibility_warning\":%s,\"source_cc_reloaded\":false,\"source_cc_requested\":true,\"extra_parameter_reload_calls\":0,\"wb_packet_schema\":3,\"boot_counter_used\":false,\"reason\":\"%s\",\"frames\":[",rc,!rc&&frames_complete==P49_FRAME_COUNT,P48_PLAN_SHA256,frames_complete,allocation_requests,ownership_verified?P39_CAP_BYTES:0,ownership_verified?"true":"false",ownership_verified?"true":"false",p48_batch_warn?"true":"false",reason);
 for(unsigned i=0;i<frames_complete;i++)fprintf(f,"%s{\"index\":%u,\"dir\":\"case_%02u\",\"case_id\":\"%s\",\"case_sha256\":\"%s\",\"result_sha256\":\"%s\",\"luma_sha256\":\"%s\",\"chroma_sha256\":\"%s\"}",i?",":"",i,i,p48_cases[i].id,p48_case_hashes[i],frame_result_sha[i],frame_luma_sha[i],frame_chroma_sha[i]);fputs("]}\n",f);
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;if(!ok)return 0;
 if(!rc&&frames_complete==P49_FRAME_COUNT&&allocation_requests==1){char hash[65],pdhash[65],posthash[65],frame[1200];if(!p48_file_sha(path,hash))return 0;
  if(snprintf(frame,sizeof frame,"%s/case_00",dir)>=(int)sizeof frame)return 0;
  if(snprintf(path,sizeof path,"%s/p49_param_dma_audit.bin",frame)>=(int)sizeof path||!p48_file_sha(path,pdhash))return 0;
  snprintf(text,sizeof text,"NXP49POSTINPUT1\nversion=0.3.6-demo50\nsource_srw_sha256=%s\ny_sha256=%s\nuv_sha256=%s\nwidth=6480\nheight=4320\nstride=6496\nnonce=%u\nsource_wb_complete=1\ncc_source_reloaded=0\nnative_frame_complete=1\nsource_origin_x=0\nsource_origin_y=0\nfront_result_sha256=%s\nparam_dma_audit_sha256=%s\n",p48_source->source_sha256,frame_luma_sha[0],frame_chroma_sha[0],nonce,frame_result_sha[0],pdhash);
  if(!p48_atomic_text(frame,"post-input.txt",text)||snprintf(path,sizeof path,"%s/post-input.txt",frame)>=(int)sizeof path||!p48_file_sha(path,posthash))return 0;
  snprintf(text,sizeof text,"NXP49FRONTCOMPLETE1\nplan_sha256=%s\nresult_sha256=%s\npost_input_sha256=%s\nframes_complete=1\nstatus=%s\ncold_restart_required=1\n",P48_PLAN_SHA256,hash,posthash,p48_batch_warn?"WARN_WB_RESTORE":"PASS");return p48_atomic_text(dir,"P49_FRONT_COMPLETE",text);
 }return 1;
#endif
}
int main(int argc,char **argv){
 if(argc==4&&!strcmp(argv[1],"--source-ticket")){
  uint64_t ticket_begin=selected_mono();selected_query_mode=1;if(!selected_prepare(argv[2],NULL))return 2;
  int saved=stkt_save(argv[3],getenv("NX51_SESSION_TOKEN"),argv[2],selected_sha,&selected_stat);
  fprintf(stderr,"SOURCE_TICKET saved=%u full_source_hash=1 source_hash_ms=%llu total_ms=%llu\n",saved?1u:0u,(unsigned long long)(selected_hash_ns/1000000u),(unsigned long long)((selected_mono()-ticket_begin)/1000000u));puts(selected_sha);selected_close();return 0;
 }
 if(argc==3&&!strcmp(argv[1],"--source-sha")){
  selected_query_mode=1;
  if(!selected_prepare(argv[2],NULL))return 2;puts(selected_sha);selected_close();return 0;
 }
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_DEMO_FRONT=0.3.6-demo50; ONE_SRW_NATIVE_WB_FACTORY_CC; READINESS_R1; FNV_WORDLOAD_R6; OBSERVED_PHASE_TIMING_R6; ALLOC_CAPACITY_R7C; READINESS_CLOCK_R7C; READONLY_INIT_RETRY_R7E; SHARED192_OWNER_R7M");return 0;}
 if(argc==2&&!strcmp(argv[1],"--clock")){struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);clock_gettime(CLOCK_REALTIME,&b);printf("mono=%lld.%09ld realtime=%lld.%09ld\n",(long long)a.tv_sec,a.tv_nsec,(long long)b.tv_sec,b.tv_nsec);return 0;}
#if !defined(__arm__)
 (void)argv;return 77;
#else
 shared_owner_mode=argc==3&&!strcmp(argv[2],"--shared-owner");
#ifdef NXL_PIPELINE_EMBEDDED
 if(argc==3&&!strcmp(argv[2],"--pipeline-return"))shared_owner_mode=2;
#endif
 if((argc!=2&&!shared_owner_mode)||argv[1][0]!='/'||strlen(argv[1])>1000)return 1;batch_directory=argv[1];char path[1200],case_dir[1200];int rc=1;started=ns();
 snprintf(path,sizeof path,"%s/probe.log",batch_directory);log_file=fopen(path,"wx");if(!log_file)return 1;snprintf(path,sizeof path,"%s/trace_events.txt",batch_directory);trace_file=fopen(path,"wx");if(!trace_file){fclose(log_file);return 1;}snprintf(path,sizeof path,"%s/p39_commands.log",batch_directory);command_log=fopen(path,"wx");if(!command_log){fclose(log_file);fclose(trace_file);return 1;}
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGALRM,stop);signal(SIGPIPE,SIG_IGN);alarm(300);
#define BATCH_REQUIRE(c,m) do{if(!(c)){reason=m;event(reason);goto done;}}while(0)
 BATCH_REQUIRE(front_resource_emit(NR_PREPARED),"RESOURCE_JOURNAL_OPEN_FAILED");BATCH_REQUIRE(firmware(),"NX500_1_12_REQUIRED");BATCH_REQUIRE(p33_guard_prepare(batch_directory),p33_guard_reason);BATCH_REQUIRE(collector_ready(batch_directory),"COLLECTOR_READY_SAVE_FAILED");front_mark("SOURCE_VERIFY_BEGIN");
 if(!p48_preflight_assets()){
  reason=selected_settings_error;event(reason);(void)selected_rejection_report(batch_directory);
  if(!native_calls&&!allocation_requests&&!request_done&&!ownership_verified&&!api_inflight&&!attached&&!camera_paused&&!p49_go_sent&&!hook_ever_installed&&!send_calls){rc=45;batch_no_submit=1;}
  goto done;
 }front_mark("SOURCE_VERIFY_END");BATCH_REQUIRE(p48_space_preflight(batch_directory),"CONTROL_REPORT_SPACE_UNAVAILABLE");
 BATCH_REQUIRE(fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)&&fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV),"ORIGINAL_BUILD_FINGERPRINT_MISMATCH");
 pid=find_camera();BATCH_REQUIRE(pid,"CAMERA_PROCESS_NOT_UNIQUE");snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,O_RDWR|O_CLOEXEC);BATCH_REQUIRE(procfd>=0,"PROCESS_MEMORY_PERMISSION_REQUIRED");BATCH_REQUIRE(load_maps(batch_directory,"batch")&&read_word(PB_POINTER_VA,&initial_ui)&&map_metadata(),"BATCH_MEMORY_PREFLIGHT_FAILED");
 for(unsigned i=0;i<P49_FRAME_COUNT;i++){
  if(i)BATCH_REQUIRE(still_owned()&&slot_original()&&!attached&&!api_inflight&&!camera_paused&&detached_ok&&idle_registers_verified,"PREVIOUS_FRAME_REUSE_GATE_FAILED");
  char selector[2]={(char)('0'+i),0};BATCH_REQUIRE(p48_select_case(selector),"COMPILED_CASE_INVALID");snprintf(case_dir,sizeof case_dir,"%s/case_%02u",batch_directory,i);BATCH_REQUIRE(!mkdir(case_dir,0700),"CASE_DIRECTORY_CREATE_FAILED");p48_reset_frame();
  int frame_rc=p48_run_frame(case_dir);
  if(frame_rc){if(frame_rc==46){rc=46;batch_no_submit=1;}else if(frame_rc==42||frame_rc==43){rc=frame_rc;batch_no_submit=1;reason=frame_rc==43?"PIPELINE_WORKSPACE_CAPACITY_UNAVAILABLE":pipeline_jpeg_missing?"PIPELINE_NEEDS_BOOT_JPEG_INIT":"FRONT_NEEDS_STILL_INIT";}else reason="P48_BATCH_STOPPED_NO_RETRY";goto done;}
 }
 BATCH_REQUIRE(frames_complete==P49_FRAME_COUNT&&allocation_requests==1,"BATCH_COUNTS_INVALID");reason="P49_FRONT_FRAME_SAVED_COLD_RESTART_REQUIRED";rc=0;
done:
 front_mark("FRONT_TERMINAL");(void)front_timing_save(batch_directory);selected_close();
 alarm(0);if(!resume_camera())rc=1;if(!p48_parked_camera&&!cleanup_trace())rc=1;
 if(!rc&&!front_workspace_export(batch_directory)){rc=1;reason="SHARED_WORKSPACE_EXPORT_FAILED";}
 if(!unmap_aliases())rc=1;if(!front_resource_emit(NR_STOPPED))rc=1;
 if(p33_guard_acquired){p33_guard_note(reason);p33_guard_close();}if(!p48_write_batch_result(batch_directory,rc))rc=1;
 event(reason);if(fclose(command_log))rc=1;if(fclose(trace_file))rc=1;if(fclose(log_file))rc=1;if(manager_map)munmap((void*)manager_map,0xb2000);if(root_map)munmap((void*)root_map,4096);if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);free(baseline);free(baseline_again);
 if(!rc&&shared_owner_mode==1)rc=front_workspace_wait(batch_directory);
 if(!rc&&shared_owner_mode==2){char ready[1200];snprintf(ready,sizeof ready,"%s/workspace-owner.ready",batch_directory);if(!sw_file_new(ready,&front_workspace,sizeof front_workspace))rc=1;}
 return rc;
#endif
}
