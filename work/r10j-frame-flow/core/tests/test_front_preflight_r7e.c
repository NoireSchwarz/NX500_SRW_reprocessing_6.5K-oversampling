/* SPDX-License-Identifier: AGPL-3.0-only. Replay recorded rejection and mutations. */
#include <assert.h>
#include <stdio.h>
#include "front_preflight.h"
#include "fixtures/field_preflight_r7d.h"
int main(void){
 unsigned checks=0;struct fr_wait_result r=field_preflight_r7d;
 assert(fp_uninitialized(&r));checks++;
 struct fp_no_submit s={1,1,{0}};
 assert(fp_safe_no_submit(&s));checks++;
 for(unsigned i=0;i<FP_ACTIVITY_FIELDS;i++){s.activity[i]=1;assert(!fp_safe_no_submit(&s));checks++;s.activity[i]=0;}
 s.cleanup_ok=0;assert(!fp_safe_no_submit(&s));checks++;s.cleanup_ok=1;
 s.uninitialized=0;assert(!fp_safe_no_submit(&s));checks++;
 assert(fp_safe_guard_rejection(&s,1));checks++;assert(!fp_safe_guard_rejection(&s,0));checks++;assert(!fp_safe_guard_rejection(&s,2));checks++;
 for(unsigned i=0;i<FP_ACTIVITY_FIELDS;i++){s.activity[i]=1;assert(!fp_safe_guard_rejection(&s,1));checks++;s.activity[i]=0;}
 s.cleanup_ok=0;assert(!fp_safe_guard_rejection(&s,1));checks++;s.cleanup_ok=1;
 for(unsigned i=0;i<FR_FIELDS;i++){
  r=field_preflight_r7d;r.last.read_mask^=1u<<i;assert(!fp_uninitialized(&r));checks++;
  if(i==9)continue;r=field_preflight_r7d;r.last.failure_mask|=1u<<i;assert(!fp_uninitialized(&r));checks++;
 }
 for(unsigned i=0;i<8;i++){
  r=field_preflight_r7d;
  if(i==0)r.ready=1;if(i==1)r.cancelled=1;if(i==2)r.clock_failed=1;
  if(i==3)r.report_ok=0;if(i==4)r.timed_out=0;if(i==5)r.last.stable_pair=0;
  if(i==6)r.last.field[9].value=2;if(i==7)r.last.failure_mask=0;
  assert(!fp_uninitialized(&r));checks++;
 }
 printf("PASS %u frontend preflight cases: recorded owner0 rejection and every native-activity, cleanup and readiness mutation\n",checks);
 return 0;
}
