/* Compact pipeline receipts identify a retired allocation without YUV files.
 * Current allocator, roots, callbacks and engine checks still follow. */
static int fast_rearm_metadata(void){
 char dir[1300],p[1400],b[16384],outcome[2048],hash[65];struct sw_lease issued,returned,front_lease;
 snprintf(dir,sizeof dir,"%s/jpeg/attempt_%u/hardware",previous,backend_attempt);if(!no_links(dir))return 0;
 snprintf(p,sizeof p,"%s/shared-workspace.bin",dir);if(!sw_file_read(p,&issued,sizeof issued)||!sw_receipt_valid(&issued)||issued.phase!=SW_FRONT_READY)return 0;
 snprintf(p,sizeof p,"%s/shared-workspace.bin",front);if(!sw_file_read(p,&front_lease,sizeof front_lease)||memcmp(&issued,&front_lease,sizeof issued))return 0;
 snprintf(p,sizeof p,"%s/workspace-released.bin",dir);if(!sw_file_read(p,&returned,sizeof returned)||!sw_receipt_same(&issued,&returned)||returned.phase!=SW_RELEASED)return 0;
 snprintf(p,sizeof p,"%s/controller-outcome.txt",dir);
 if(!readtext(p,outcome,sizeof outcome)||!key(outcome,"result_sha256",hash,sizeof hash)||!boundfile(dir,"result.json",hash))return 0;
 char classification[64],complete[16];if(!key(outcome,"classification",classification,sizeof classification)||strcmp(classification,"SAFE_RELEASED")||!key(outcome,"result_complete",complete,sizeof complete)||strcmp(complete,"1"))return 0;
 snprintf(p,sizeof p,"%s/result.json",dir);if(!readtext(p,b,sizeof b))return 0;size_t n=strlen(b);
 const char*one[]={"ownership_verified","shared_workspace_borrowed","shared_workspace_completed","shared_workspace_released","release_returned","allocator_return_verified","original_code_restored","detached","idle_registers_verified","backend_CAP_releases","copy_worker_exited","copy_result_valid","copy_safe_to_release","lut_jpeg_pipeline_verified","lut_hardware_jobs"};
 const char*zero[]={"exit","backend_CAP_requests","request_returned","allocation_retained","api_inflight_at_exit","camera_stopped_at_exit","copy_memory_quarantined","copy_worker_exit"};
 for(unsigned i=0;i<sizeof one/sizeof *one;i++)if(!p49_field_uint(b,n,one[i],1))return 0;
 for(unsigned i=0;i<sizeof zero/sizeof *zero;i++)if(!p49_field_uint(b,n,zero[i],0))return 0;
 if(!p49_field_uint(b,n,"pa",issued.pa)||!p49_field_uint(b,n,"va",issued.va)||!p49_field_uint(b,n,"requested_bytes",issued.capacity)||!p49_field_uint(b,n,"engine_jobs",issued.capacity==167772160u?2u:3u)||!p49_field_uint(b,n,"front_retirement_samples",16))return 0;
 snprintf(p,sizeof p,"%s/jpeg/attempt_%u/result.json",previous,backend_attempt);if(!readtext(p,b,sizeof b))return 0;n=strlen(b);
 if(!p49_field_text(b,n,"format","NX_SINGLE_PIPELINE_FAST1")||!p49_field_uint(b,n,"exit",0)||!p49_field_uint(b,n,"pa",issued.pa)||!p49_field_uint(b,n,"va",issued.va)||!p49_field_uint(b,n,"nonce",issued.nonce)||!p49_field_uint(b,n,"native_jobs",issued.capacity==167772160u?3u:4u)||!p49_field_boolean(b,n,"workspace_return_verified",1)||!p49_field_uint(b,n,"workspace_bytes",issued.capacity)||!p49_field_text(b,n,"lut_sha256",issued.lut_sha256)||!p49_field_text(b,n,"source_srw_sha256",issued.source_sha256))return 0;
 old_pa=issued.pa;old_va=issued.va;old_nonce=issued.nonce;old_bytes=issued.capacity;
 return r7_set_front_capacity(old_bytes)&&r7_extent(old_pa,old_va);
}
