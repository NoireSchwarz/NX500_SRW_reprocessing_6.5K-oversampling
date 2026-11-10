#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include "../src/front_handoff.h"
static unsigned mode,sample,pauses,reports;static uint64_t clock_ms=10000;
static unsigned owned=1,slot=1,attached,detached_ok=1,api_inflight,native_signal;
static unsigned code_restored=1,idle_registers_verified=1,call_boundary_active;
static unsigned camera_paused,camera_resumed_verified=1,p49_native_done=1,task_quiescent_verified=1;
static FILE*log_file;
static int still_owned(void){return owned;}
static int slot_original(void){return slot;}
static int front_readiness_read(void*c,uint32_t address,uint32_t*out,unsigned words){
 (void)c;
 if(address==FR_TASK)sample++;
 for(unsigned n=0;n<words;n++){
  uint32_t at=address+4*n,v=0;unsigned known=1;
  if(at==FR_TASK)v=0x80592640;
  else if(at==FR_TASK+3364)v=((mode==1&&sample<=3)||(mode==2&&(sample==1||sample==3))||mode==3)?6:7;
  else if(at==FR_TASK+3204||at==FR_TASK+3208||at==FR_TASK+3284||at==FR_TASK+3288)v=0;
  else if(at==FR_TASK+3428)v=mode==4?0x81000003:0x81001000;
  else if(at==0x81001000)v=0x805926c8;
  else if(at==0x81543d40)v=0x81002000;
  else if(at==0x81002000)v=1;
  else if(at==0x8142d154)v=0x80582820;
  else if(at==0x8142d154+3444)v=0;
  else known=0;
  assert(known);out[n]=v;
 }return mode!=5;
}
static uint64_t front_readiness_clock(void*c){(void)c;return clock_ms;}
static void front_readiness_pause(void*c,unsigned ms){(void)c;pauses++;if(mode==8)clock_ms-=1;else if(mode!=9)clock_ms+=ms;}
static int front_readiness_cancel(void*c){(void)c;return mode==7;}
#include "../src/front_handoff_host.inc"
static uint32_t proof(void*c){if(mode==6&&sample>=1)owned=0;return front_handoff_context_read(c);}
static int report(void*c,const struct fh_result*r,unsigned final){(void)c;(void)r;reports++;if(mode==10)return 0;if(mode==11&&!final)clock_ms+=FR_WAIT_MS+1;return 1;}
int main(int argc,char**argv){
 assert(argc==2);mode=(unsigned)atoi(argv[1]);struct fh_result r;
 if(mode>=20&&mode<27){unsigned bit=1u<<(mode-20);if(bit==FH_OWNED)owned=0;if(bit==FH_SLOT_ORIGINAL)slot=0;if(bit==FH_DETACHED)attached=1;if(bit==FH_API_QUIET)api_inflight=1;if(bit==FH_REGISTERS_RESTORED)idle_registers_verified=0;if(bit==FH_CAMERA_RESUMED)camera_paused=1;if(bit==FH_NATIVE_DONE)p49_native_done=0;}
 if(mode>=30){log_file=fopen("/mnt/mmc/postframe.log","w");assert(log_file);int ok=front_handoff_released("/mnt/mmc",mode==30?"postframe":"export");assert(ok);assert(front_handoff_last.readiness.samples==3);assert(!fclose(log_file));puts("PASS production handoff adapter and bounded JSON evidence");return 0;}
 int ok=fh_wait(NULL,front_readiness_read,front_readiness_clock,front_readiness_pause,front_readiness_cancel,proof,report,&r);
 if(mode<=2){assert(ok&&r.readiness.consecutive==3&&r.readiness.elapsed_ms>=40&&r.last_context==FH_REQUIRED);assert(r.readiness.samples==(mode==0?3:mode==1?6:6));}
 else{assert(!ok);if(mode>=20){assert(r.readiness.samples==1&&pauses==0&&r.last_context!=FH_REQUIRED);}if(mode==6)assert(r.readiness.samples==2&&pauses==1);if(mode==3||mode==4||mode==5)assert(r.readiness.timed_out&&r.readiness.samples<=FR_WAIT_MS/FR_SAMPLE_MS+1);if(mode==8||mode==9)assert(r.readiness.clock_failed);if(mode==10)assert(!r.readiness.report_ok);}
 assert(reports>=1&&reports<=2);printf("PASS handoff mode=%u result=%d samples=%u context=%u failure=%u elapsed=%u\n",mode,ok,r.readiness.samples,r.last_context,r.readiness.last.failure_mask,r.readiness.elapsed_ms);return 0;
}
