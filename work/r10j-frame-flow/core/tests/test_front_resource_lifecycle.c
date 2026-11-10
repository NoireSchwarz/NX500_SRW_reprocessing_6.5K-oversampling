#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define P39_CAP_BYTES 167772160u
static uint32_t response[5],ownership_verified,index_owned,owned_meta[4],nonce,native_calls,send_calls,slot_pointer_writes;
static uint32_t p49_go_sent,p49_native_done,code_restored,detached_ok,api_inflight,camera_paused;
#include "../src/front_resource.inc"
int main(void){
 const char*dir="/mnt/mmc/NXDEMO51/JOB_MODEL/front_1/front",*file="/mnt/mmc/NXDEMO51/JOB_MODEL/front_1/front/RESOURCE.journal";struct nr_resource last;
 assert(nxl_front_resource_target(dir)&&nxl_front_resource_prepared());
 assert(nr_read(file,&last)&&last.sequence==1&&last.phase==NR_PREPARED&&!last.request_started&&!last.native_calls);
 /* A setup failure already has durable no-call facts; entering the frontend
  * can repeat PREPARED without resetting that sequence or changing owner. */
 assert(nxl_front_resource_prepared()&&nr_read(file,&last)&&last.sequence==2);
 front_resource.request_started=1;assert(front_resource_emit(NR_ALLOCATING));
 response[0]=0x87000000;response[1]=0xa1c00000;response[2]=P39_CAP_BYTES;response[3]=2;front_resource.response_valid=1;
 native_calls=1;assert(front_resource_emit(NR_REQUEST_RETURNED));
 ownership_verified=1;index_owned=(response[1]-0x94000000u)/16384u;owned_meta[0]=35;owned_meta[1]=P39_CAP_BYTES/16384u;nonce=999;
 assert(front_resource_emit(NR_OWNED));p49_go_sent=1;assert(front_resource_emit(NR_FRONT_GO));
 p49_native_done=code_restored=detached_ok=1;assert(front_resource_emit(NR_FRONT_DONE)&&front_resource_emit(NR_STOPPED));
 assert(nxl_front_resource_released()&&nr_read(file,&last)&&last.sequence==9&&last.phase==NR_RELEASED&&last.ownership_verified&&last.done);
 assert(nxl_front_resource_target(dir)&&!nxl_front_resource_prepared());
 assert(nr_read(file,&last)&&last.sequence==9&&last.phase==NR_RELEASED);
 puts("PASS actual owner fact emitter: pre-setup durable no-call record, repeated prepare, allocate/GO/stop/verified release; same path cannot reset or overwrite an old ledger");return 0;
}
