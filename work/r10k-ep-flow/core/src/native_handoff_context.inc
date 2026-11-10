#include "native_allocator.inc"
#include "p48_case.h"
#include "p48_plan_hashes.h"
#include "batch_contract.h"
#include <sys/stat.h>
#include <sys/resource.h>
#include "sha256.h"
#include "bridge_guards.h"
#include "bridge_core.h"
#include "handoff_core.h"
#ifdef NXL_COLD_INIT_TEST
#include "rtos_front_trial_blob.h"
#else
#include "rtos_front_blob.h"
#endif
#include "install_guard.inc"
static uint32_t mcb_base,packet_count,send_calls;
static uint32_t rtos_readonly_guards,cap_canaries_verified;
static uint32_t console_bytes;

static char cap_expected_hash[65],cap_observed_hash[65];
static unsigned char expected[P39_PROOF_BYTES],console_raw[P33_CAPTURE_BYTES],console_text[P33_CAPTURE_BYTES+1];
static volatile unsigned char *cap_alias,*console_map;static int sma_fd=-1;
static FILE *command_log;static const char *bridge_error="NOT_STARTED";
static int safe_call(void){return !api_inflight&&!native_signal&&trace_stopped&&still_owned()&&!stop_requested;}
static int invoke4(uint32_t fn,uint32_t a,uint32_t b,uint32_t c,uint32_t d,unsigned timeout){
 struct alloc_regs r=alloc_call3(&call_original,fn,a,b,c);r.r[3]=d;trace_error=NULL;
 if(!set_regs(&r)){trace_error="NATIVE_CALL_SETREGS_FAILED";return 0;}native_calls++;api_inflight=1;
 if(!resume()||!wait_stop(timeout,SIGTRAP)||!verify_breakpoint(1)||!at_return(&r))return 0;
 api_inflight=0;return 1;
}
#include "rtos_guard_reader.inc"
static int native_bridge_context(const char *dir){
 for(unsigned i=0;i<region_count;i++)if(!regions[i].offset&&!strcmp(regions[i].perms,"r-xp")&&!strcmp(regions[i].path,"/usr/lib/libmulticore-bridge.so")){
  if(mcb_base&&mcb_base!=regions[i].lo)return 0;mcb_base=regions[i].lo;
 }
 if(!mcb_base||!fingerprint("/usr/lib/libmulticore-bridge.so",P33_MCB_BYTES,P33_MCB_FNV)||!guards(mcb_base,p33_mcb_guards,sizeof p33_mcb_guards/sizeof *p33_mcb_guards))return 0;
 /* Pinned CReceiver singleton and its CID182 bucket. Empty means no native
  * console client is displaced and the stock receiver safely drops ACKs.
  * The original callback list and semaphore are never written. */
 uint32_t a[6]={0},b[6]={0},guard=0;unsigned char alive=0;
 const uint32_t holder=mcb_base+0x12730u+68u,bucket=holder+29u*20u;
 char path[1200];snprintf(path,sizeof path,"%s/p33_bridge_context.txt",dir);FILE *audit=fopen(path,"wx");if(!audit)return 0;
 uint32_t singleton_guard=0,vptr=0;int g=read_word(mcb_base+0x1272c,&singleton_guard),v=read_word(holder,&vptr),x=read_process(bucket+4,a,20),y=read_process(bucket+4,b,20),z=read_process(mcb_base+0x12ad8,&alive,1);
 fprintf(audit,"mcb_base=0x%08x holder=0x%08x bucket=0x%08x singleton_ok=%d singleton_guard=0x%08x vptr_ok=%d vptr=0x%08x expected_vptr=0x%08x reads=%d,%d alive_ok=%d alive=%u\n",mcb_base,holder,bucket,g,singleton_guard,v,vptr,mcb_base+0x12050u,x,y,z,alive);
 fprintf(audit,"cid182_words=%08x,%08x,%08x,%08x,%u\n",a[0],a[1],a[2],a[3],a[4]);if(fclose(audit))return 0;
 if(!read_word(mcb_base+0x1272c,&guard)||!(guard&1)||!read_word(holder,&guard)||guard!=mcb_base+0x12050u||
    !read_process(bucket+4,a,20)||!read_process(bucket+4,b,20)||memcmp(a,b,20))return 0;
 for(unsigned i=0;i<5;i++)if(a[i])return 0;
 return read_process(mcb_base+0x12ad8,&alive,1)&&alive==1;
}
