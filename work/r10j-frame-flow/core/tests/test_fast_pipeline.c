/* Actual coordinator and return verifier. Native submissions are modeled. */
#define _GNU_SOURCE
#define NXL_PIPELINE_TEST
#define main nxl_post_entry
#include "../../post/src/hw-post.c"
#undef main
#define main pipeline_entry
#include "../src/nxl-pipeline.c"
#undef main
#include <assert.h>
static int mode;
#ifdef NXL_COLD_INIT_TEST
static unsigned ep_acquired,ep_returned;
int pipeline_test_init_helper(const char*operation){
 if(!strcmp(operation,"--acquire")){assert(!ep_acquired);ep_acquired++;return 0;}
 assert(!strcmp(operation,"--release")&&ep_acquired&&!ep_returned);ep_returned++;return mode==24?1:0;
}
#endif
static struct sw_lease fixture_lease(void){
 struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;
 s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;s.pa=0xa1c00000;s.va=0x87000000;s.nonce=777;s.owner_pid=getpid();s.owner_start_ticks=12345;s.index=(s.pa-0x94000000u)/16384;
 s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;
 memset(s.prefix_sha256,'a',64);memset(s.lut_sha256,'b',64);memset(s.source_sha256,'c',64);sw_seal(&s);return s;
}
int nxl_front_entry(int argc,char**argv){
 assert(argc==3&&!strcmp(argv[2],"--pipeline-return"));FILE*f=fopen("/test-mode","r");assert(f&&fscanf(f,"%d",&mode)==1&&!fclose(f));
 char p[1200];snprintf(p,sizeof p,"%s/result.json",argv[1]);const char*json="{\"format\":\"NX_FAST_FRONT_BATCH1\"}\n";assert(sw_file_new(p,json,strlen(json)));
 if(mode==1)return 42;if(mode==2)return 43;if(mode==12)return 45;
 if(mode==23||mode==24){snprintf(p,sizeof p,"%s/case_00",argv[1]);assert(!mkdir(p,0700));snprintf(p,sizeof p,"%s/case_00/p39_rtos_code_guards.txt",argv[1]);assert(sw_file_new(p,"GUARD_REJECTION_MODEL\n",22));return 46;}
 if(mode==14||mode==15||mode==22){
  assert(!setenv("NX51_SOURCE_SRW","/mnt/mmc/DCIM/305PHOTO/SELECTED.SRW",1));
  snprintf(p,sizeof p,"%s/case_00",argv[1]);assert(!mkdir(p,0700));
  const char*names[]={"selected-source.json","selected-c0.bin","case_00/isp-config-schema.json","native-recorder-link.json","source-tone-curves.json"};
  for(unsigned i=0;i<5;i++){if(mode==15&&i==4)continue;snprintf(p,sizeof p,"%s/%s",argv[1],names[i]);assert(sw_file_new(p,"MODEL_ENDPOINT",14));}
  static struct ic_sample captures[IC_STAGES];
  for(unsigned i=0;i<IC_STAGES;i++){struct ic_sample*r=captures+i;r->magic=IC_MAGIC;r->schema=1;r->nonce=777;r->stage=i+1;r->mask=31;r->stable=1;r->checksum=ic_hash(r,sizeof *r-8);r->commit=IC_MAGIC^r->nonce^r->stage^r->checksum;}
  snprintf(p,sizeof p,"%s/case_00/isp-config-stages.bin",argv[1]);assert(sw_file_new(p,captures,sizeof captures));
  if(mode!=22){const char*iq[]={"source-cc-audit.bin","source-wb-audit.bin","gamma-test-audit.bin"};unsigned char control[4096]={0};const unsigned bytes[]={4096,1024,2048};for(unsigned i=0;i<3;i++){snprintf(p,sizeof p,"%s/case_00/%s",argv[1],iq[i]);assert(sw_file_new(p,control,bytes[i]));}}
  snprintf(p,sizeof p,"%s/case_00/p39_rtos_code_guards.txt",argv[1]);assert(sw_file_new(p,"GUARD_SUCCESS_MODEL\n",20));
 }
 if(mode==0){
  snprintf(p,sizeof p,"%s/case_00",argv[1]);assert(!mkdir(p,0700));
  const char*phases[]={"BEFORE_SRW","AFTER_STILL"};unsigned char evidence[8564]={0};
  for(unsigned i=0;i<2;i++){snprintf(p,sizeof p,"%s/case_00/diag-cmdq-%s.bin",argv[1],phases[i]);evidence[0]=(unsigned char)(i+1);assert(sw_file_new(p,evidence,sizeof evidence));}
 }
 if(mode==3){
  assert(!setenv("NX51_PROGRESS_TOKEN","0123456789abcdef0123456789abcdef",1));
  snprintf(p,sizeof p,"%s/case_00",argv[1]);assert(!mkdir(p,0700));
  snprintf(p,sizeof p,"%s/case_00/result.json",argv[1]);const char*failure="{\"reason\":\"POST_FRAME_STILL_NOT_STABLE\"}\n";assert(sw_file_new(p,failure,strlen(failure)));
  snprintf(p,sizeof p,"%s/case_00/front_handoff_postframe_final.json",argv[1]);assert(sw_file_new(p,failure,strlen(failure)));
  snprintf(p,sizeof p,"%s/case_00/failed-native-result.bin",argv[1]);unsigned char control[256]={0};assert(sw_file_new(p,control,sizeof control));
  return 1;
 }
 struct sw_lease s=fixture_lease();assert(sw_owner_proof_publish(&s)&&sw_owner_alive(&s));
 snprintf(p,sizeof p,"%s/shared-workspace.bin",argv[1]);assert(sw_file_new(p,&s,sizeof s));
 unsigned char table[19712]={0};snprintf(p,sizeof p,"%s/srw-lut.bin",argv[1]);assert(sw_file_new(p,table,sizeof table));
 snprintf(p,sizeof p,"%s/native-jpeg-config.bin",argv[1]);assert(sw_file_new(p,table,256));snprintf(p,sizeof p,"%s/native-ldc-config.bin",argv[1]);assert(sw_file_new(p,table,1024));snprintf(p,sizeof p,"%s/source-settings.json",argv[1]);assert(sw_file_new(p,"{}",2));snprintf(p,sizeof p,"%s/front-timing.json",argv[1]);assert(sw_file_new(p,"{}",2));return mode==4?1:0;
}
int pipeline_test_controller(const char*dir,const char*out,unsigned abort_only){
 assert(mode!=1&&mode!=2&&mode!=3&&mode!=23&&mode!=24);if(mode==6)return 1;
 if(mode==0){const char*phases[]={"BEFORE_EP","AFTER_EP","AFTER_RELEASE"};char path[1400];unsigned char evidence[8564]={0};
  for(unsigned i=0;i<3;i++){evidence[0]=(unsigned char)(i+3);snprintf(path,sizeof path,"%s/diag-cmdq-%s.bin",dir,phases[i]);assert(sw_file_new(path,evidence,sizeof evidence));}}
 struct sw_lease s=fixture_lease();char p[1400],text[7000],hash[65];unsigned bytes=0;
 if(!abort_only){const char*names[]={"pipeline-isp-settings.json","bayer-out-samples.bin","ldc-out-samples.bin","lut-out-samples.bin","native-ldc-state.bin","j422-proof.bin","ldc-config-audit.json","ldc-hardware-audit.json","ldc-progress-audit.json","p11_kernel_before.bin","p11_kernel_after.bin","p11_rtos_irq_before.bin","p11_rtos_irq_after.bin"};for(unsigned i=0;i<sizeof names/sizeof *names;i++){if((mode==13&&i==2)||(mode==17&&i==6)||(mode==20&&i==7))continue;snprintf(p,sizeof p,"%s/%s",dir,names[i]);assert(sw_file_new(p,"MODEL_ENDPOINT",14));}}

 if(!abort_only&&mode!=16&&mode!=18&&mode!=21){const char*target=getenv("NX51_FINAL_PENDING");if(target)snprintf(p,sizeof p,"%s",target);else snprintf(p,sizeof p,"%s/native.jpg.pending",out);assert(pipeline_copy_small("/mnt/mmc/model.jpg",p));assert(pipeline_hash(p,hash,&bytes));char hpath[1400];snprintf(hpath,sizeof hpath,"%s/final-jpeg.sha256",dir);assert(sw_file_new(hpath,hash,64));
#ifdef NXL_PIPELINE_TICKET_TEST
  struct stat identity;assert(!lstat(p,&identity));snprintf(hpath,sizeof hpath,"%s/final-jpeg-ticket.bin",dir);assert(jt_write(hpath,&identity,s.nonce,s.pa,s.owner_pid,hash,s.source_sha256));
#endif
 if(mode==8)assert(!unlink(p));}
 if(mode==21){
  int n=snprintf(text,sizeof text,"{\"exit\":1,\"copy_go_sent\":1,\"copy_worker_exited\":1,\"copy_result_valid\":1,\"copy_safe_to_release\":0,\"pipeline_failed_stage\":1,\"pipeline_worker_error\":23,\"pipeline_fresh_completions\":1,\"copy_error\":\"LDC_DMA_STOP_ACK_UNVERIFIED\",\"requested_bytes\":%u}\n",SW_BYTES);
  snprintf(p,sizeof p,"%s/result.json",dir);assert(sw_file_new(p,text,(size_t)n));return 1;
 }
 if(mode==18){
  int n=snprintf(text,sizeof text,"{\"exit\":1,\"copy_go_sent\":1,\"copy_worker_exited\":1,\"copy_worker_exit\":1,\"copy_result_valid\":1,\"copy_safe_to_release\":0,\"copy_memory_quarantined\":1,\"pipeline_failed_stage\":1,\"pipeline_worker_error\":13,\"copy_error\":\"LDC_COMPLETION_TIMEOUT\",\"requested_bytes\":%u,\"backend\":\"NATIVE_LDC_LUT_JPEG_422_SHARED192\"}\n",SW_BYTES);
  snprintf(p,sizeof p,"%s/result.json",dir);assert(sw_file_new(p,text,(size_t)n));return 1;
 }
 if(mode>=9&&mode<=11){
  if(mode==10){FILE*f=fopen(p,"r+b");assert(f&&!fseek(f,20,SEEK_SET));assert(fputc(0,f)!=EOF&&!fclose(f));}
  int n=snprintf(text,sizeof text,"{\"exit\":1,\"copy_go_sent\":1,\"copy_worker_exited\":1,\"copy_worker_exit\":0,\"copy_result_valid\":1,\"copy_safe_to_release\":1,\"copy_memory_quarantined\":%u,\"lut_jpeg_pipeline_verified\":1,\"shared_workspace_completed\":1,\"ldc_hardware_jobs\":1,\"lut_hardware_jobs\":1,\"engine_jobs\":3,\"requested_bytes\":%u,\"backend\":\"NATIVE_LDC_LUT_JPEG_422_SHARED192\"}\n",mode==11?1u:0u,SW_BYTES);
  snprintf(p,sizeof p,"%s/result.json",dir);assert(sw_file_new(p,text,(size_t)n));return 1;
 }
 assert(sw_transition(&s,SW_FRONT_READY,SW_BACKEND_ACTIVE)&&sw_transition(&s,SW_BACKEND_ACTIVE,SW_BACKEND_DONE)&&sw_transition(&s,SW_BACKEND_DONE,SW_RELEASED));snprintf(p,sizeof p,"%s/workspace-released.bin",dir);assert(sw_file_new(p,&s,sizeof s));
 int n=snprintf(text,sizeof text,"{\"exit\":%u,\"ownership_verified\":1,\"shared_workspace_borrowed\":1,\"shared_workspace_completed\":%u,\"shared_workspace_released\":1,\"release_returned\":1,\"allocator_return_verified\":1,\"original_code_restored\":1,\"detached\":1,\"idle_registers_verified\":1,\"backend_CAP_releases\":1,\"backend_CAP_requests\":0,\"request_returned\":0,\"allocation_retained\":0,\"api_inflight_at_exit\":0,\"camera_stopped_at_exit\":0,\"copy_memory_quarantined\":0,\"front_retirement_samples\":16,\"pa\":%u,\"va\":%u,\"requested_bytes\":%u,\"allocator_index\":%u,\"copy_go_sent\":%u,\"copy_worker_exited\":1,\"copy_worker_exit\":%u,\"copy_result_valid\":1,\"copy_safe_to_release\":1,\"lut_jpeg_pipeline_verified\":%u,\"ldc_hardware_jobs\":%u,\"lut_hardware_jobs\":%u,\"engine_jobs\":%u,\"release_only\":%u,\"backend\":\"%s\"%s}\n",(mode==5||mode==16)?1u:0u,mode==16?0u:1u,s.pa,s.va,SW_BYTES,s.index,abort_only?0u:1u,mode==16?1u:0u,mode==16?0u:1u,mode==16?0u:1u,mode==16?0u:1u,mode==16?0u:3u,abort_only,abort_only?"SHARED_WORKSPACE_RELEASE_ONLY":"NATIVE_LDC_LUT_JPEG_422_SHARED192",mode==16?",\"copy_error\":\"LDC_CONFIGURATION_READBACK_FAILED\",\"pipeline_failed_stage\":1,\"pipeline_worker_error\":14":"");
 snprintf(p,sizeof p,"%s/result.json",dir);assert(sw_file_new(p,text,(size_t)n));struct sha256 sh;unsigned char digest[32];sha256_init(&sh);sha256_update(&sh,text,(size_t)n);sha256_final(&sh,digest);digest_hex(digest,hash);
 n=snprintf(text,sizeof text,"NX51CONTROLLER1\nclassification=SAFE_RELEASED\ncontroller_pid=456\nreason=MODEL_RELEASE\nresult_sha256=%s\ncold_restart_required=1\nresult_complete=1\n",hash);snprintf(p,sizeof p,"%s/controller-outcome.txt",dir);assert(sw_file_new(p,text,(size_t)n));return mode==5||mode==16?1:0;
}
int main(int argc,char**argv){
#ifdef NXL_PIPELINE_ALBUM_TEST
 assert(!setenv("NX51_SOURCE_SRW","/mnt/mmc/DCIM/305PHOTO/SELECTED.SRW",1));
#endif
 int rc=pipeline_entry(argc,argv);
#ifdef NXL_COLD_INIT_TEST
 assert(ep_acquired==1&&ep_returned==((mode==3||mode==18||mode==21)?0u:1u));
#endif
 return rc;
}
