
#ifndef NXL_REAL_EP_PREFLIGHT_FIXTURE
#define NXL_REAL_EP_PREFLIGHT_FIXTURE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../common/shared_workspace_io.h"
#ifndef NXL_TEST_EP_ALLOC_HEADER
#define NXL_TEST_EP_ALLOC_HEADER "../../hardware/src/alloc_core.h"
#endif
#include NXL_TEST_EP_ALLOC_HEADER
static uint32_t rpe_response[5],rpe_last,rpe_native_calls,rpe_owned_calls,rpe_reads,rpe_fault,rpe_count;
static uint32_t rpe_udd_base=0x50000000u,rpe_tint_base=0x51000000u,rpe_type_base=0x52000000u;
static unsigned rpe_ep_queries,rpe_ep_matches,rpe_ep_open_count,rpe_ep_context_observed,rpe_ep_fd_verified,rpe_ep_contract_verified,rpe_stop_requested;
static int rpe_ep_sma_fd;
static const char*rpe_ep_error="NONE",*rpe_trace_error,*rpe_trace_phase;
static FILE*rpe_log_file;
static int rpe_still_owned(void){rpe_owned_calls++;return rpe_fault!=5||rpe_owned_calls<3;}
static void rpe_event(const char*s){fprintf(rpe_log_file,"%s\n",s);}
static int rpe_read_word(uint32_t a,uint32_t*v){
 if(rpe_fault==1)return 0;
 if(a==rpe_udd_base+EP_OPEN_COUNT_DATA){rpe_reads++;*v=rpe_count+(rpe_fault==7&&rpe_reads>=3);return 1;}
 if(a==rpe_udd_base+EP_SMA_FD_DATA){*v=3;return 1;}assert(0);return 0;
}
static int rpe_invoke(uint32_t fn,uint32_t arg,unsigned timeout){
 assert(timeout==3000);rpe_native_calls++;
 if(fn==rpe_udd_base+EP_OPEN_COUNT_FN){assert(!arg);rpe_last=rpe_count+(rpe_fault==4);return 1;}
 assert(fn==rpe_udd_base+EP_SMA_VTOP_FN);assert(arg>=rpe_response[0]&&(uint64_t)arg<rpe_response[0]+(uint64_t)rpe_response[2]);
 if(rpe_fault==6)return 0;rpe_last=rpe_response[1]+arg-rpe_response[0]+(rpe_fault==3?4096u:0);return 1;
}
static int rpe_get_regs(struct alloc_regs*r){memset(r,0,sizeof *r);r->r[0]=rpe_last;return 1;}
static int rpe_sma_target(char b[256]){strcpy(b,rpe_fault==2?"/dev/null":"/dev/d5_sma");return rpe_fault!=2;}
#define ep_preflight rpe_ep_preflight
#define response rpe_response
#define still_owned rpe_still_owned
#define ep_error rpe_ep_error
#define trace_error rpe_trace_error
#define trace_phase rpe_trace_phase
#define ep_sma_fd rpe_ep_sma_fd
#define udd_base rpe_udd_base
#define tint_base rpe_tint_base
#define type_base rpe_type_base
#define log_file rpe_log_file
#define ep_open_count rpe_ep_open_count
#define ep_context_observed rpe_ep_context_observed
#define ep_fd_verified rpe_ep_fd_verified
#define ep_contract_verified rpe_ep_contract_verified
#define ep_queries rpe_ep_queries
#define ep_matches rpe_ep_matches
#define stop_requested rpe_stop_requested
#define read_word rpe_read_word
#define invoke rpe_invoke
#define get_regs rpe_get_regs
#define sma_target rpe_sma_target
#define event rpe_event
static int ep_preflight(const char *dir){
 static const uint32_t offsets[4]={0,EP_TEST_Y_BYTES,8192,8192+EP_TEST_Y_BYTES};
 static const char *phases[4]={"ep_source_y","ep_source_c","ep_target_y","ep_target_c"};
 uint32_t count_before=0,count_after=0,fd=0;struct alloc_regs r;struct ep_candidate candidates[2];char path[1200],target[256]={0};FILE *context=NULL,*table=NULL;
 int ok=0;trace_error=NULL;
#define EP_REQUIRE(condition,message) do{if(!(condition)){ep_error=trace_error?trace_error:(message);event(ep_error);goto ep_finish;}}while(0)
 EP_REQUIRE(still_owned()&&ep_make_candidates(response[0],response[1],candidates),"EP_CANDIDATE_RANGE_UNAVAILABLE");
 snprintf(path,sizeof path,"%s/ep_candidates.bin",dir);FILE *saved=fopen(path,"wb");EP_REQUIRE(saved,"EP_CANDIDATE_SAVE_FAILED");
 int saved_ok=fwrite(candidates,1,sizeof candidates,saved)==sizeof candidates;if(fclose(saved))saved_ok=0;EP_REQUIRE(saved_ok,"EP_CANDIDATE_SAVE_FAILED");
 snprintf(path,sizeof path,"%s/ep_context.txt",dir);context=fopen(path,"w");EP_REQUIRE(context,"EP_CONTEXT_SAVE_FAILED");
 EP_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count_before)&&read_word(udd_base+EP_SMA_FD_DATA,&fd),"EP_CONTEXT_MEMORY_UNAVAILABLE");ep_sma_fd=(int32_t)fd;
 trace_phase="ep_open_count";EP_REQUIRE(invoke(udd_base+EP_OPEN_COUNT_FN,0,3000)&&get_regs(&r),"EP_OPEN_COUNT_CALL_FAILED");ep_open_count=r.r[0];
 EP_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count_after),"EP_OPEN_COUNT_READ_FAILED");
 fprintf(context,"udd_base=0x%08x tint_base=0x%08x type_base=0x%08x\nopen_count_before=%u native_return=%u after=%u\nsma_fd=%d\ncandidate_bytes=%u hardware_jobs=0 module_open_calls=0\n",udd_base,tint_base,type_base,count_before,ep_open_count,count_after,ep_sma_fd,(unsigned)sizeof candidates);fflush(context);
 EP_REQUIRE(count_before==ep_open_count&&ep_open_count==count_after,"EP_OPEN_COUNT_UNSTABLE");ep_context_observed=1;
 EP_REQUIRE(ep_sma_fd>=0,"EP_SMA_CONTEXT_NOT_OPEN");ep_fd_verified=sma_target(target);
 fprintf(context,"sma_target=%s verified=%u\n",target,ep_fd_verified);fflush(context);EP_REQUIRE(ep_fd_verified,"EP_SMA_FD_TARGET_UNCONFIRMED");
 snprintf(path,sizeof path,"%s/ep_preflight.csv",dir);table=fopen(path,"w");EP_REQUIRE(table,"EP_ADDRESS_TABLE_CREATE_FAILED");
 fputs("plane,offset,va,cap_expected_pa,sma_return_pa,match\n",table);
 for(unsigned i=0;i<4;i++){
  EP_REQUIRE(!stop_requested&&still_owned()&&read_word(udd_base+EP_SMA_FD_DATA,&fd)&&fd==(uint32_t)ep_sma_fd&&sma_target(target),"EP_CONTEXT_CHANGED_DURING_QUERY");
  trace_phase=phases[i];EP_REQUIRE(invoke(udd_base+EP_SMA_VTOP_FN,response[0]+offsets[i],3000)&&get_regs(&r),"EP_SMA_QUERY_DID_NOT_RETURN");ep_queries++;
  unsigned match=r.r[0]==response[1]+offsets[i];ep_matches+=match;
  fprintf(table,"%s,%u,0x%08x,0x%08x,0x%08x,%u\n",phases[i],offsets[i],response[0]+offsets[i],response[1]+offsets[i],r.r[0],match);fflush(table);
  fprintf(log_file,"EP_ADDRESS plane=%s va=0x%08x cap_pa=0x%08x sma_pa=0x%08x match=%u\n",phases[i],response[0]+offsets[i],response[1]+offsets[i],r.r[0],match);fflush(log_file);
 }
 EP_REQUIRE(read_word(udd_base+EP_OPEN_COUNT_DATA,&count_after)&&count_after==ep_open_count&&still_owned(),"EP_CONTEXT_CHANGED_AFTER_QUERY");
 EP_REQUIRE(ep_queries==4&&ep_matches==4,"EP_SMA_TRANSLATION_MISMATCH");ep_contract_verified=1;ok=1;
ep_finish:
 if(table&&fclose(table)){ok=0;ep_error="EP_ADDRESS_TABLE_SAVE_FAILED";}
 if(!ok)ep_contract_verified=0;
 if(context){fprintf(context,"contract_verified=%u error=%s\n",ep_contract_verified,ep_error);if(fclose(context)){ok=0;ep_contract_verified=0;ep_error="EP_CONTEXT_SAVE_FAILED";}}
 return ok;
#undef EP_REQUIRE
}

#undef ep_preflight
#undef response
#undef still_owned
#undef ep_error
#undef trace_error
#undef trace_phase
#undef ep_sma_fd
#undef udd_base
#undef tint_base
#undef type_base
#undef log_file
#undef ep_open_count
#undef ep_context_observed
#undef ep_fd_verified
#undef ep_contract_verified
#undef ep_queries
#undef ep_matches
#undef stop_requested
#undef read_word
#undef invoke
#undef get_regs
#undef sma_target
#undef event

static int rpe_run(const char*dir,const struct sw_lease*s,unsigned fault,unsigned count){
 assert(sw_valid(s));rpe_response[0]=s->va;rpe_response[1]=s->pa;rpe_response[2]=s->capacity;rpe_response[3]=2;rpe_response[4]=0;
 rpe_fault=fault;rpe_count=count;rpe_last=rpe_native_calls=rpe_owned_calls=rpe_reads=rpe_ep_queries=rpe_ep_matches=rpe_ep_contract_verified=rpe_ep_context_observed=rpe_ep_fd_verified=rpe_ep_open_count=0;rpe_stop_requested=fault==8;rpe_trace_error=NULL;rpe_ep_error="NONE";
 char p[1400];snprintf(p,sizeof p,"%s/ep-replay.log",dir);rpe_log_file=fopen(p,"w");assert(rpe_log_file);int ok=rpe_ep_preflight(dir);assert(!fclose(rpe_log_file));
 if(ok)assert(rpe_ep_queries==4&&rpe_ep_matches==4&&rpe_native_calls==5&&rpe_ep_contract_verified);return ok;
}
#endif
