/* SPDX-License-Identifier: AGPL-3.0-only. Durable controller lifecycle. */
static int controller_dir_sync(const char *dir){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 0;int ok=!fsync(fd);if(close(fd))ok=0;return ok;}
static const char *controller_classification(void){
 if(shared_borrowed&&!attach_attempted&&!shell_message_queued&&!native_calls&&!request_done&&!api_inflight&&!attached&&!p7_go_sent)return "SAFE_RETAINED";
 if(!attach_attempted&&!shell_message_queued&&!native_calls&&!request_done&&!api_inflight&&!attached&&!p7_go_sent&&(!p7_shared||!p7_shared->camera_paused))return "SAFE_NO_SUBMIT";
 if(shared_borrowed&&!release_done&&!return_verified&&ownership_verified&&code_restored&&detached_ok&&idle_registers_verified&&!api_inflight&&!attached&&!p7_quarantined&&(!p7_shared||!p7_shared->camera_paused)&&(!p7_pauses||(p7_resumes==p7_pauses&&p7_resumed_verified==p7_pauses)))return "SAFE_RETAINED";
 if(return_verified&&release_done&&ownership_verified&&code_restored&&detached_ok&&idle_registers_verified&&!api_inflight&&!attached&&!p7_quarantined&&(!p7_shared||!p7_shared->camera_paused)&&(!p7_pauses||(p7_resumes==p7_pauses&&p7_resumed_verified==p7_pauses)))return "SAFE_RELEASED";
 return "UNKNOWN_REQUIRE_RESTART";
}
static unsigned controller_restart_needed(const char *classification){return shared_borrowed||!strcmp(classification,"UNKNOWN_REQUIRE_RESTART")||p7_result.cold_restart_required||p7_quarantined;}
static int controller_outcome_commit(const char *dir,const char *classification){
 char path[1200],temp[1200],hex[65];unsigned char b[8192],digest[32];struct sha256 hash;sha256_init(&hash);
 snprintf(path,sizeof path,"%s/result.json",dir);FILE *f=fopen(path,"rb");if(!f)return 0;size_t n;int ok=1;
 while((n=fread(b,1,sizeof b,f)))sha256_update(&hash,b,n);if(ferror(f))ok=0;if(fclose(f))ok=0;if(!ok)return 0;sha256_final(&hash,digest);digest_hex(digest,hex);
 snprintf(temp,sizeof temp,"%s/controller-outcome.txt.tmp",dir);f=fopen(temp,"w");if(!f)return 0;
 fprintf(f,"NX51CONTROLLER1\nclassification=%s\ncontroller_pid=%ld\nreason=%s\nresult_sha256=%s\ncold_restart_required=%u\nresult_complete=1\n",classification,(long)getpid(),reason,hex,controller_restart_needed(classification));
 ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;snprintf(path,sizeof path,"%s/controller-outcome.txt",dir);
 return ok&&!rename(temp,path)&&controller_dir_sync(dir);
}
static int controller_report(const char *dir,int rc,const char *classification){
 char path[1200];
 snprintf(path,sizeof path,"%s/result.json.tmp",dir);FILE *result=fopen(path,"w");
 if(result){fprintf(result,
 "{\n\"resource_disposition\":\"%s\",\n\"format\":\"NXPIPE1\",\"version\":\"0.4.0-demo51-422\",\"exit\":%d,\"reason\":\"%s\",\n"
 "\"failure_stage\":\"%s\",\"controller_pid\":%d,\"pid\":%d,\"native_worker_tid\":%d,\"requested_bytes\":%u,\"native_calls\":%u,\n"
 "\"request_returned\":%u,\"ownership_verified\":%u,\"release_returned\":%u,\"allocator_return_verified\":%u,\"original_code_restored\":%u,\"detached\":%u,\"api_inflight_at_exit\":%u,\n"
 "\"va\":%u,\"pa\":%u,\"allocator_index\":%u,\"free_before\":%u,\"free_owned\":%u,\"free_after\":%u,\"pattern_nonce\":%u,\"idle_registers_verified\":%u,\n"
 "\"ep_contract_verified\":%u,\"ep_error\":\"%s\",\"sync_preflight_verified\":%u,\"sync_kernel_guards_verified\":%u,\"sync_error\":\"%s\",\n"
 "\"copy_worker_ready\":%u,\"copy_go_sent\":%u,\"copy_worker_exited\":%u,\"copy_worker_exit\":%u,\"copy_result_valid\":%u,\"copy_safe_to_release\":%u,\"copy_memory_quarantined\":%u,\"lut_jpeg_pipeline_verified\":%u,\"copy_error\":\"%s\",\n"
 "\"camera_pauses\":%u,\"camera_resumes\":%u,\"camera_resumed_verified\":%u,\"camera_stopped_at_exit\":%u,\"copy_rehearsal_verified\":%u,\"copy_peer_scans\":%u,\n"
 "\"backend\":\"%s\",\"release_only\":%u,\"engine_jobs\":%u,\"jpeg_total_output_bytes\":%u,\"pipeline_fresh_completions\":%u,\"pipeline_wait_errno\":%d,\"pipeline_worker_error\":%u,\"pipeline_failed_stage\":%u,\"local_reset_calls\":%u,\n"
 "\"shared_workspace_borrowed\":%u,\"shared_workspace_completed\":%u,\"backend_CAP_requests\":0,\"backend_CAP_releases\":%u,\"allocation_retained\":%u,\"shared_workspace_released\":%u,\"front_retirement_samples\":%u,\"cold_restart_required\":%u,\"photo_pixel_reads\":1,\"photo_pixel_writes\":0,\"ldc_hardware_jobs\":%u,\"lut_hardware_jobs\":%u,\"srw_decode_jobs\":0,\"elapsed_ms\":%u\n}\n",
 classification,rc,reason,failure_stage,getpid(),pid,tid,ALLOC_BYTES,native_calls,
 request_done,ownership_verified,release_done,return_verified,code_restored,detached_ok,api_inflight,
 response[0],response[1],index_owned,free_before,free_owned,free_after,pattern_nonce,idle_registers_verified,
 ep_contract_verified,ep_error,sync_preflight_verified,sync_kernel_guards_verified,sync_error,
 p7_worker_ready,p7_go_sent,p7_worker_exited,p7_worker_exit,p7_result_valid,p7_safe_to_release,p7_quarantined,p7_verified,p7_error,
 p7_pauses,p7_resumes,p7_resumed_verified,p7_shared?p7_shared->camera_paused:0,p7_rehearsal_verified,p7_peer_scans,
 shared_release_only?"SHARED_WORKSPACE_RELEASE_ONLY":"NATIVE_LDC_LUT_JPEG_422_SHARED192",shared_release_only,p7_result.engine_jobs,p7_result.pixel_bytes,p7_result.fresh_completion,p7_result.wait_errno,p7_result.error,p7_result.failed_stage,p7_result.local_reset_calls,
 shared_borrowed,shared_completed,release_done,shared_borrowed&&!return_verified,shared_released,shared_release_samples,controller_restart_needed(classification),p7_result.ldc_hardware_jobs,p7_result.lut_hardware_jobs,ms());
  int ok=!ferror(result)&&!fflush(result)&&!fsync(fileno(result));if(fclose(result))ok=0;
  char final[1200];snprintf(final,sizeof final,"%s/result.json",dir);if(!ok||rename(path,final)||!controller_dir_sync(dir))return 0;
 }else return 0;
 return controller_outcome_commit(dir,classification);

}
