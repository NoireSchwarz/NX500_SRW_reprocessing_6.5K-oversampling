/* The release API queues an RTOS attribute. Publish the allocator's normal
 * RAM through the OEM RTOS cache operation after that message, exactly once.
 * A queued cache message is not proof of return: full table checks follow.
 * Do not pass a freed CAP alias through the public VA-conversion wrapper. */
#include "../../common/allocator_cache_contract.h"
static unsigned cache_fence_attempted,cache_fence_guards,cache_fence_send_returned,cache_fence_queued,cache_fence_scratch_restored;
static uint32_t cache_fence_result=UINT32_MAX;
static int allocator_cache_fence_once(const char*dir){
 if(cache_fence_attempted){reason="ALLOCATOR_CACHE_FENCE_ALREADY_ATTEMPTED";return 0;}
 cache_fence_attempted=1;int ok=0,saved=0,wrote=0;uint32_t scratch=receive_buffer+48u,backup[3],check[3],adapter=0,vtable=0;
 const uint32_t payload[3]={0,AC_START,AC_BYTES};uint64_t begin=ns();
#define AC_REQUIRE(test,error) do{if(!(test)){reason=(error);goto done;}}while(0)
 AC_REQUIRE(release_done&&shared_borrowed&&sw_valid(&shared_lease)&&attached&&trace_stopped&&call_boundary_active&&!api_inflight&&!native_signal&&!stop_requested&&(!p7_shared||!p7_shared->camera_paused),"ALLOCATOR_CACHE_FENCE_CONTEXT_UNPROVEN");
 AC_REQUIRE(ac_payload_valid(payload)&&receive_buffer<=UINT32_MAX-60u&&region_ok(scratch,12,1,0),"ALLOCATOR_CACHE_FENCE_SCRATCH_INVALID");
 AC_REQUIRE(read_word(cap_base+0xb33dcu,&adapter)&&adapter&&region_ok(adapter,4,0,0)&&read_word(adapter,&vtable)&&vtable==cap_base+0xb0240u,"ALLOCATOR_CACHE_FENCE_ADAPTER_UNPROVEN");
 uint32_t root_free=0;AC_REQUIRE(af_pool_head(devfd,&root_free),"ALLOCATOR_CACHE_FENCE_POOL_ROOT_UNPROVEN");
 uint32_t code[32];AC_REQUIRE(read_process(cap_base+AC_NATIVE_SEND,code,32)&&!memcmp(code,ac_send_words,32),"ALLOCATOR_CACHE_FENCE_NATIVE_CODE_MISMATCH");
 for(unsigned i=0;i<sizeof ac_guards/sizeof *ac_guards;i++){
  AC_REQUIRE(ac_guards[i].bytes<=sizeof code&&shared_phys(ac_guards[i].address,code,ac_guards[i].bytes)&&sw_checksum(code,ac_guards[i].bytes)==ac_guards[i].hash,"ALLOCATOR_CACHE_FENCE_RTOS_CODE_MISMATCH");
 }
 pr_close(&shared_reader);cache_fence_guards=1;
 AC_REQUIRE(read_process(scratch,backup,12),"ALLOCATOR_CACHE_FENCE_SCRATCH_SAVE_FAILED");saved=1;wrote=1;
 AC_REQUIRE(pwrite(procfd,payload,12,(off_t)scratch)==12&&read_process(scratch,check,12)&&!memcmp(payload,check,12),"ALLOCATOR_CACHE_FENCE_SCRATCH_WRITE_FAILED");
 trace_phase="allocator_metadata_cache_fence";event("ALLOCATOR_METADATA_CACHE_FENCE_BEGIN");
 AC_REQUIRE(invoke4(cap_base+AC_NATIVE_SEND,adapter,AC_ATTRIBUTE,12,scratch,AC_SEND_WAIT_MS),"ALLOCATOR_CACHE_FENCE_SEND_UNCONFIRMED");cache_fence_send_returned=1;
 struct alloc_regs regs;AC_REQUIRE(get_regs(&regs),"ALLOCATOR_CACHE_FENCE_RETURN_REGISTERS_UNAVAILABLE");cache_fence_result=regs.r[0];
 AC_REQUIRE(!cache_fence_result,"ALLOCATOR_CACHE_FENCE_SEND_FAILED");cache_fence_queued=1;ok=1;
done:
 pr_close(&shared_reader);
 /* A native sender may still be using this ordinary shell-stack payload.
  * Leave it intact on an unknown in-flight call; normal quarantine follows. */
 if(saved&&wrote&&!api_inflight&&trace_stopped){
  cache_fence_scratch_restored=(unsigned)(pwrite(procfd,backup,12,(off_t)scratch)==12&&read_process(scratch,check,12)&&!memcmp(backup,check,12));
  if(!cache_fence_scratch_restored){reason="ALLOCATOR_CACHE_FENCE_SCRATCH_RESTORE_FAILED";ok=0;}
 }
 if(ok&&!cache_fence_scratch_restored){reason="ALLOCATOR_CACHE_FENCE_SCRATCH_RESTORE_UNPROVEN";ok=0;}
 char path[1200];snprintf(path,sizeof path,"%s/allocator-cache-fence.json",dir);FILE*f=fopen(path,"wx");
 if(f){fprintf(f,"{\"format\":\"NXALLOCATORCACHE1\",\"native_release_retried\":false,\"metadata_only\":true,\"attribute\":%u,\"payload\":[0,%u,%u],\"attempts\":%u,\"code_guards_verified\":%u,\"send_returned\":%u,\"send_result\":%u,\"queued\":%u,\"scratch_restored\":%u,\"api_inflight\":%u,\"elapsed_ms\":%llu,\"allocator_return_proven_by_this_record\":false}\n",AC_ATTRIBUTE,AC_START,AC_BYTES,cache_fence_attempted,cache_fence_guards,cache_fence_send_returned,cache_fence_result,cache_fence_queued,cache_fence_scratch_restored,api_inflight,(unsigned long long)((ns()-begin)/1000000ULL));(void)fclose(f);}
 fprintf(log_file,"ALLOCATOR_METADATA_CACHE_FENCE guards=%u queued=%u send_result=0x%08x scratch_restored=%u api_inflight=%u elapsed_ms=%llu\n",cache_fence_guards,cache_fence_queued,cache_fence_result,cache_fence_scratch_restored,api_inflight,(unsigned long long)((ns()-begin)/1000000ULL));fflush(log_file);
 return ok;
#undef AC_REQUIRE
}
