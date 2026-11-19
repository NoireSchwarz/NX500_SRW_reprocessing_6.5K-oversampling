/* SPDX-License-Identifier: AGPL-3.0-only.
 * OEM DCI24 retains its native frame rate. Only its output width is mapped
 * to the existing 3840x2160 full-width scaler route; no timer/PTS patch.
 */
#ifndef NX_VIDEO_NATIVE_24P_H
#define NX_VIDEO_NATIVE_24P_H
#define V24_WIDTH 0x000e43f0u
#define V24_NATIVE 0xe3a0aa01u
#define V24_UHD 0xe3a0ac0fu
static int v24_known(uint32_t w){return w==V24_NATIVE||w==V24_UHD;}
static int v24_apply(const struct vm_ops*o,int mapped,struct vm_result*r){
 memset(r,0,sizeof *r);r->reason="NATIVE24_READ_FAILED";
 if(!o->read(o->ctx,V24_WIDTH,r->before))return 0;r->after[0]=r->before[0];
 r->reason="NATIVE24_UNKNOWN_CONSTRUCTOR";if(!v24_known(r->before[0]))return 0;
 uint32_t want=mapped?V24_UHD:V24_NATIVE;
 if(r->before[0]==want){r->reason="NATIVE24_MAPPING_ALREADY_SELECTED";return 1;}
 r->reason="STOP_RECORDING_OR_TASK_BEFORE_CHANGE";if(!o->ready(o->ctx))return 0;
 int ok=0;r->reason="CAMERA_THREADS_NOT_QUIESCENT";if(!o->freeze(o->ctx))goto out;
 uint32_t current;r->reason="NATIVE24_CONTEXT_CHANGED";
 if(!o->ready(o->ctx)||!o->read(o->ctx,V24_WIDTH,&current)||current!=r->before[0])goto out;
 r->reason="NATIVE24_WRITE_FAILED";r->writes++;
 if(!o->write(o->ctx,V24_WIDTH,want)||!o->read(o->ctx,V24_WIDTH,r->after)||r->after[0]!=want){
  r->rollback_attempted=1;int restored=o->write(o->ctx,V24_WIDTH,r->before[0]);
  r->rollback_verified=restored&&o->read(o->ctx,V24_WIDTH,r->after)&&r->after[0]==r->before[0];
  r->reason=r->rollback_verified?"NATIVE24_FAILED_ORIGINAL_RESTORED":"NATIVE24_ROLLBACK_UNCONFIRMED_RESTART_REQUIRED";goto out;
 }
 ok=1;r->reason=mapped?"NATIVE_DCI24_UHD_MAPPING_CONFIGURED":"NATIVE_DCI24_4096_RESTORED";
out:
 r->thawed=o->thaw(o->ctx);if(!r->thawed){r->reason="CAMERA_THREAD_DETACH_UNCONFIRMED";ok=0;}return ok;
}
#endif
