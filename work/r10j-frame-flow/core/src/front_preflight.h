/* SPDX-License-Identifier: AGPL-3.0-only. A read-only rejection is retryable. */
#ifndef NXL_FRONT_PREFLIGHT_H
#define NXL_FRONT_PREFLIGHT_H
#include "front_readiness.h"
#define FP_ACTIVITY_FIELDS 17
struct fp_no_submit { unsigned uninitialized,cleanup_ok,activity[FP_ACTIVITY_FIELDS]; };
static int fp_uninitialized(const struct fr_wait_result *r){
 return r->report_ok&&!r->ready&&!r->cancelled&&!r->clock_failed&&r->timed_out&&
  r->last.read_mask==((1u<<FR_FIELDS)-1u)&&r->last.stable_pair&&
  r->last.failure_mask==(1u<<9)&&r->last.field[9].value==0;
}
static int fp_clean_no_submit(const struct fp_no_submit *s){
 if(!s->cleanup_ok)return 0;
 for(unsigned i=0;i<FP_ACTIVITY_FIELDS;i++)if(s->activity[i])return 0;
 return 1;
}
static int fp_safe_no_submit(const struct fp_no_submit *s){return s->uninitialized&&fp_clean_no_submit(s);}
static int fp_safe_guard_rejection(const struct fp_no_submit*s,unsigned rejected){return rejected==1u&&fp_clean_no_submit(s);}
#endif
