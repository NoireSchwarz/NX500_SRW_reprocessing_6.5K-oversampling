#define main rearm_unused_main
#include "../src/nxl-session-rearm.c"
#undef main
#include <assert.h>
int main(void){
 job=previous="/mnt/mmc/NXDEMO51/JOB_MODEL";const char*path="/mnt/mmc/NXDEMO51/JOB_MODEL/front_1/front/RESOURCE.journal";
 struct nr_resource r={.phase=NR_PREPARED,.capacity=167772160,.bank=2,.kind=35,.owner_pid=123,.ticks_lo=12345};
 assert(nr_append(path,&r)&&resource_metadata()&&inventory.operations==1&&inventory.resolved==1&&!inventory.count);
 r.phase=NR_ALLOCATING;r.request_started=1;assert(nr_append(path,&r)&&resource_metadata()&&inventory.count==1&&inventory.item[0].mode==RI_UNLOCATED);
 r.phase=NR_REQUEST_RETURNED;r.response_valid=1;r.response[0]=0x87000000;r.response[1]=0xa1c00000;r.response[2]=r.capacity;r.response[3]=2;
 assert(nr_append(path,&r)&&resource_metadata()&&inventory.count==1&&inventory.item[0].mode==RI_EXTENT&&inventory.item[0].pa==0xa1c00000);
 r.phase=NR_OWNED;r.ownership_verified=1;r.index=(r.response[1]-0x94000000u)/16384u;r.metadata[0]=35;r.metadata[1]=r.capacity/16384;r.nonce=999;assert(nr_append(path,&r)&&resource_metadata());
 r.phase=NR_FRONT_GO;r.go=1;assert(nr_append(path,&r)&&resource_metadata());
 r.phase=NR_FRONT_DONE;r.done=1;r.code_restored=r.detached=1;assert(nr_append(path,&r)&&resource_metadata()&&inventory.count==1);
 r.phase=NR_RELEASED;assert(nr_append(path,&r)&&resource_metadata()&&!inventory.count&&inventory.resolved==1);
 puts("PASS v1 shared owner facts cover a missing/interrupted backend: pre-call, unlocated request, known extent, submitted frame, completed frame, verified return; no image files needed");return 0;
}
