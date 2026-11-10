/* Durable control-plane facts survive a lost worker or camera standby cycle. */
#include "../../common/resource_ledger.h"
#include "../../common/shared_workspace_io.h"
static const char*front_resource_dir;
static struct nr_resource front_resource;
int nxl_front_resource_target(const char*dir){
 const char*p="/mnt/mmc/NXDEMO51/JOB_";if(!dir||strncmp(dir,p,strlen(p))||strstr(dir,"..")||strlen(dir)>1000)return 0;
 front_resource_dir=dir;memset(&front_resource,0,sizeof front_resource);return 1;
}
static int front_resource_emit(unsigned phase){
 if(!front_resource_dir)return 1;struct nr_resource*r=&front_resource;uint64_t ticks;
 if(!r->sequence){if(!sw_owner_ticks((uint32_t)getpid(),&ticks))return 0;r->ticks_lo=(uint32_t)ticks;r->ticks_hi=(uint32_t)(ticks>>32);}
 r->phase=phase;r->capacity=P39_CAP_BYTES;r->bank=2;r->kind=35;r->owner_pid=(uint32_t)getpid();
 memcpy(r->response,response,sizeof r->response);r->ownership_verified=ownership_verified;r->index=index_owned;memcpy(r->metadata,owned_meta,sizeof r->metadata);
 r->nonce=nonce;r->native_calls=native_calls;r->mcb_sends=send_calls;r->slot_writes=slot_pointer_writes;r->go=p49_go_sent;r->done=p49_native_done;
 r->code_restored=code_restored;r->detached=detached_ok;r->api_inflight=api_inflight;r->paused=camera_paused;
 char path[1200];snprintf(path,sizeof path,"%s/RESOURCE.journal",front_resource_dir);return nr_append(path,r);
}
int nxl_front_resource_prepared(void){return front_resource_emit(NR_PREPARED);}
int nxl_front_resource_released(void){return front_resource.sequence&&front_resource.ownership_verified&&front_resource_emit(NR_RELEASED);}
