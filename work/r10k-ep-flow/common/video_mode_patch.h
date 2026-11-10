/* SPDX-License-Identifier: AGPL-3.0-only. Only NX500 UHD constructor words.
 * Same 2160/3840 <-> 1440/2560 mapping as NX-KS/metol resup; no new sensor
 * mode, clock or ISP register write. UI selection is an explicit operation.
 */
#ifndef NX_VIDEO_MODE_PATCH_H
#define NX_VIDEO_MODE_PATCH_H
#include <stdint.h>
#include <string.h>
#define VM_HEIGHT 0x000e4428u
#define VM_WIDTH 0x000e442cu
enum {VM_UNKNOWN=0,VM_UHD=1,VM_25K=2};
struct vm_ops {void*ctx;int(*freeze)(void*);int(*thaw)(void*);int(*ready)(void*);int(*read)(void*,uint32_t,uint32_t*);int(*write)(void*,uint32_t,uint32_t);};
struct vm_result {uint32_t before[2],after[2];unsigned writes,rollback_attempted,rollback_verified,thawed;const char*reason;};
static int vm_pair(const uint32_t*p){return p[0]==0xe3a08e87u&&p[1]==0xe3a0ac0fu?VM_UHD:p[0]==0xe3a08e5au&&p[1]==0xe3a0ac0au?VM_25K:VM_UNKNOWN;}
static int vm_read(const struct vm_ops*o,uint32_t*p){return o->read(o->ctx,VM_HEIGHT,p)&&o->read(o->ctx,VM_WIDTH,p+1);}
static int vm_apply(const struct vm_ops*o,unsigned target,struct vm_result*r){
 memset(r,0,sizeof *r);r->reason="VIDEO_MODE_ARGUMENT_INVALID";if(target!=VM_UHD&&target!=VM_25K)return 0;
 r->reason="VIDEO_MODE_READ_UNAVAILABLE";if(!vm_read(o,r->before))return 0;memcpy(r->after,r->before,8);
 r->reason="VIDEO_MODE_UNKNOWN_INSTRUCTIONS";if(!vm_pair(r->before))return 0;
 if(vm_pair(r->before)==(int)target){r->reason="ALREADY_SELECTED";return 1;}
 r->reason="STOP_RECORDING_OR_TASK_BEFORE_CHANGE";if(!o->ready(o->ctx))return 0;
 int ok=0;r->reason="CAMERA_THREADS_NOT_QUIESCENT";if(!o->freeze(o->ctx))goto out;
 r->reason="CAMERA_BUSY_OR_CODE_CHANGED";uint32_t check[2];if(!o->ready(o->ctx)||!vm_read(o,check)||memcmp(check,r->before,8))goto out;
 uint32_t want[2]={target==VM_25K?0xe3a08e5au:0xe3a08e87u,target==VM_25K?0xe3a0ac0au:0xe3a0ac0fu};
 r->reason="PATCH_WRITE_OR_READBACK_FAILED";
 r->writes++;int a=o->write(o->ctx,VM_HEIGHT,want[0]);int b=0;if(a){r->writes++;b=o->write(o->ctx,VM_WIDTH,want[1]);}
 if(!a||!b||!vm_read(o,r->after)||memcmp(r->after,want,8)){
  r->rollback_attempted=1;int x=o->write(o->ctx,VM_HEIGHT,r->before[0]);int y=o->write(o->ctx,VM_WIDTH,r->before[1]);
  r->rollback_verified=x&&y&&vm_read(o,r->after)&&!memcmp(r->after,r->before,8);r->reason=r->rollback_verified?"PATCH_FAILED_ORIGINAL_PAIR_RESTORED":"PATCH_ROLLBACK_UNCONFIRMED";goto out;
 }
 ok=1;r->reason="SELECT_UHD_AGAIN_TO_USE_THIS_MODE";
out:
 r->thawed=o->thaw(o->ctx);if(!r->thawed){r->reason="CAMERA_THREAD_DETACH_UNCONFIRMED";ok=0;}return ok;
}
#endif
