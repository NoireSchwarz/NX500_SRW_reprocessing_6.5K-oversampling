#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include "../../common/progress_emit.h"
#include "../../base/nx-lut-lite/src/progress_view.h"
static void record(struct np_record*r,const char*phase,const char*state){strcpy(r->phase,phase);strcpy(r->state,state);strcpy(r->detail,"Actual stage boundary");np_digest(r,r->digest);assert(np_valid(r));}
int main(void){
 const char*token="0123456789abcdef0123456789abcdef";struct nxl_progress_view v;struct np_record r,legacy;
 assert(np_last("/mnt/mmc/legacy.events",&legacy)&&legacy.version==2);nxl_progress_view_begin(&v,token,0);assert(!nxl_progress_view_record(&v,&legacy)&&v.percent==0);
 assert(!setenv("NX51_PROGRESS_TOKEN",token,1));nxl_live_progress("PIPE_SRW","RUNNING","Loading the actual SRW");assert(np_last("/tmp/nx-lut-lite/PROGRESS.live",&r)&&r.version==3&&!strcmp(r.operation,token));
 assert(nxl_progress_view_record(&v,&r)&&v.percent==8);
 record(&r,"PIPE_STILL_SETTLE","RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==35);
 record(&r,"PIPE_ABORT_CHECK","RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==35&&strstr(v.title,"failed task"));
 nxl_progress_view_begin(&v,token,0);
 for(unsigned i=0;i<sizeof nxl_progress_stages/sizeof *nxl_progress_stages;i++){const struct nxl_progress_stage*s=nxl_progress_stages+i;if(strncmp(s->phase,"PIPE_",5))continue;record(&r,s->phase,"RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==s->percent&&v.percent<100);}
 record(&r,"PIPE_SRW","RUNNING");assert(!nxl_progress_view_record(&v,&r)&&v.percent==97);
 record(&r,"PIPE_JPEG","RUNNING");r.digest[0]^=1;assert(!nxl_progress_view_record(&v,&r));r.digest[0]^=1;
 strcpy(r.operation,"fedcba9876543210fedcba9876543210");np_digest(&r,r.digest);assert(!nxl_progress_view_record(&v,&r));strcpy(r.operation,token);np_digest(&r,r.digest);
 record(&r,"PIPE_HELD","HELD");strcpy(r.detail,"JPEG encoded; callback retirement was not verified");np_digest(&r,r.digest);assert(nxl_progress_view_record(&v,&r)&&v.issue&&v.percent==97);
 nxl_progress_view_finish(&v,0,0);assert(v.issue&&v.percent<100&&strstr(v.title,"Stopped"));
 nxl_progress_view_begin(&v,token,0);record(&r,"PIPE_RETIRE","RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==85);nxl_progress_view_finish(&v,1,0);assert(v.percent==100&&!v.issue);
 nxl_progress_view_begin(&v,token,1);for(unsigned i=0;i<sizeof nxl_progress_stages/sizeof *nxl_progress_stages;i++){const struct nxl_progress_stage*s=nxl_progress_stages+i;if(strncmp(s->phase,"CHECK_",6))continue;record(&r,s->phase,"RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==s->percent&&v.percent<100);}
 assert(v.percent==99);nxl_progress_view_finish(&v,1,1);assert(v.percent==100&&!v.issue&&strstr(v.title,"passed"));

 nxl_progress_view_begin(&v,token,0);v.auto_init=1;
 const char*field_phases[]={"AUTO_RECORD","AUTO_BASELINE","RAW_RESOURCE_CHECK","CHECK_AUTH","CHECK_METADATA","CHECK_CODE","CHECK_POOL1","CHECK_POOL2","CHECK_POOL3","CHECK_BACKEND1","CHECK_BACKEND2","CHECK_BACKEND3","CHECK_BACKEND4","CHECK_FINAL","CHECK_LOCKS","CHECK_GRANT","CHECK_COMMIT","RAW_RESOURCE_CHECK","AUTO_EP_CYCLE","SETUP","NATIVE_SRW_LUT_JPEG","PIPE_PREPARE","AUTO_EP_ACQUIRE","PIPE_SRW","PIPE_ISP","PIPE_STILL_SETTLE","PIPE_FRONT_DONE","NATIVE_SRW_LUT_JPEG","AUTO_INIT_STOP"};unsigned previous=0,seen_front=0,seen_commit=0;
 for(unsigned i=0;i<sizeof field_phases/sizeof *field_phases;i++){
  const char*p=field_phases[i];record(&r,p,(!strcmp(p,"AUTO_INIT_STOP")||(!strcmp(p,"NATIVE_SRW_LUT_JPEG")&&seen_front))?"HELD":"RUNNING");
  (void)nxl_progress_view_record(&v,&r);assert(v.percent>=previous);previous=v.percent;
  if(!strcmp(p,"CHECK_COMMIT")){assert(v.percent==20);seen_commit=1;}
  if(!strcmp(p,"PIPE_FRONT_DONE")){assert(v.percent==48);seen_front=1;}
 }
 assert(seen_commit&&seen_front&&v.percent==48&&v.issue);nxl_progress_view_finish(&v,0,0);assert(v.percent==48&&v.issue);
 nxl_progress_view_begin(&v,token,0);v.auto_init=1;
 for(unsigned i=0;i<sizeof nxl_auto_progress_stages/sizeof *nxl_auto_progress_stages;i++){
  const struct nxl_progress_stage*s=nxl_auto_progress_stages+i;record(&r,s->phase,"RUNNING");
  (void)nxl_progress_view_record(&v,&r);assert(v.percent==s->percent);if(strncmp(s->phase,"AUTO_READY",10)&&strncmp(s->phase,"AUTO_STILL_COLD",15))assert(v.percent<100);
 }
 nxl_progress_view_begin(&v,token,0);v.auto_init=1;record(&r,"CHECK_COMMIT","RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==20);
 strcpy(r.operation,"fedcba9876543210fedcba9876543210");np_digest(&r,r.digest);assert(!nxl_progress_view_record(&v,&r)&&v.percent==20);
 strcpy(r.operation,token);record(&r,"PIPE_ISP","RUNNING");r.digest[0]^=1;assert(!nxl_progress_view_record(&v,&r)&&v.percent==20);

 nxl_progress_view_begin(&v,token,0);v.auto_init=1;
 record(&r,"AUTO_EP_RELEASE","RUNNING");assert(nxl_progress_view_record(&v,&r)&&v.percent==90);
 record(&r,"AUTO_INIT_STOP","HELD");strcpy(r.detail,"LDC_CONFIGURATION_READBACK_FAILED_WORKSPACE_RETURNED");np_digest(&r,r.digest);
 assert(nxl_progress_view_record(&v,&r)&&v.issue&&v.percent==90&&strstr(v.title,"lens configuration readback"));
 nxl_progress_view_finish(&v,0,0);assert(strstr(v.title,"lens configuration readback")&&strstr(v.detail,"LDC_CONFIGURATION"));
 nxl_progress_view_begin(&v,token,0);v.auto_init=1;record(&r,"AUTO_INIT_STOP","HELD");strcpy(r.detail,"LDC_COMPLETION_TIMEOUT_WORKSPACE_AND_EP_HELD");np_digest(&r,r.digest);
 assert(nxl_progress_view_record(&v,&r)&&strstr(v.title,"lens correction did not finish"));nxl_progress_view_finish(&v,0,0);assert(strstr(v.title,"did not finish")&&strstr(v.detail,"WORKSPACE_AND_EP_HELD"));
 nxl_progress_view_begin(&v,token,0);v.auto_init=1;record(&r,"AUTO_INIT_STOP","HELD");strcpy(r.detail,"LDC_DMA_STOP_ACK_UNVERIFIED_WORKSPACE_AND_EP_HELD");np_digest(&r,r.digest);
 assert(nxl_progress_view_record(&v,&r)&&v.issue&&strstr(v.title,"LDC DMA drain unverified")&&strstr(v.detail,"DMA retirement is unverified"));
 nxl_progress_view_finish(&v,0,0);assert(strstr(v.title,"LDC DMA drain unverified")&&strstr(v.detail,"power off/on"));
 puts("PASS real legacy records, operation-bound RAM producer and progress reducer; first-tile fence is recorded but never full-frame success");return 0;
}
