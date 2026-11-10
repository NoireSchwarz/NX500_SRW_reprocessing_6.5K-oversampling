/* SPDX-License-Identifier: AGPL-3.0-only. Confirm a returned original ioctl.
 * Read-only, bounded visibility settling. Never resubmit the ioctl, write
 * driver state, accept route0/3 as encoder-ready, or drop idle/ref checks. */
#ifndef NXL_SELECTOR_CONFIRM_H
#define NXL_SELECTOR_CONFIRM_H
#include <stdint.h>
#include <string.h>
#define ESC_POLLS 16u
#define ESC_DELAY_MS 10u
enum esc_status {ESC_UNCHECKED=0,ESC_CONFIRMED=1,ESC_READ_FAILED=2,ESC_REFERENCE_CHANGED=3,ESC_DRIVER_ERROR=4,ESC_FOREIGN_ROUTE=5,ESC_IDLE_UNCONFIRMED=6,ESC_ROUTE_CHANGED=7,ESC_DEADLINE=8};
struct esc_observation {uint32_t selector,error,references,context_matches;};
struct esc_result {uint32_t status,samples,pauses,idle_checks;struct esc_observation first,last;};
/* idle:1 stable proof,0 actual busy/unavailable,2 two individually idle
 * snapshots changed. Only2 can be observed again within the same budget. */
struct esc_ops {void*ctx;int(*read)(void*,struct esc_observation*);int(*idle)(void*);void(*pause)(void*,unsigned);void(*note)(void*,unsigned,const struct esc_observation*,int);};
static unsigned esc_check(const struct esc_observation*s,unsigned expected){
 if(s->references!=expected||s->context_matches!=1)return ESC_REFERENCE_CHANGED;
 if(s->error)return ESC_DRIVER_ERROR;
 if(s->selector!=0&&s->selector!=5)return ESC_FOREIGN_ROUTE;
 return 0;
}
static unsigned esc_read(const struct esc_ops*o,struct esc_result*r,unsigned expected){
 struct esc_observation s={0};int ok=o->read(o->ctx,&s);if(o->note)o->note(o->ctx,r->samples,&s,ok);
 if(!r->samples)r->first=s;r->last=s;r->samples++;return ok?esc_check(&s,expected):ESC_READ_FAILED;
}
static int esc_confirm(const struct esc_ops*o,unsigned expected,struct esc_result*r){
 memset(r,0,sizeof *r);
 for(unsigned poll=0;poll<ESC_POLLS;poll++){
  r->status=esc_read(o,r,expected);if(r->status)return 0;
  if(r->last.selector==5){
   r->idle_checks++;int idle=o->idle(o->ctx);
   if(idle!=1&&idle!=2){r->status=ESC_IDLE_UNCONFIRMED;return 0;}
   if(idle==1){
    r->status=esc_read(o,r,expected);if(r->status)return 0;
    if(r->last.selector!=5){r->status=ESC_ROUTE_CHANGED;return 0;}
    r->status=ESC_CONFIRMED;return 1;
   }
  }
  if(poll+1<ESC_POLLS){o->pause(o->ctx,ESC_DELAY_MS);r->pauses++;}
 }r->status=ESC_DEADLINE;return 0;
}
static const char*esc_reason(unsigned status){
 switch(status){
 case ESC_CONFIRMED:return "EP_SELECTOR_STABLE_AFTER_ORIGINAL_IOCTL";
 case ESC_READ_FAILED:return "EP_SELECTOR_READBACK_UNAVAILABLE";
 case ESC_REFERENCE_CHANGED:return "EP_SELECTOR_OWNED_CONTEXT_CHANGED";
 case ESC_DRIVER_ERROR:return "EP_SELECTOR_DRIVER_ERROR";
 case ESC_FOREIGN_ROUTE:return "EP_SELECTOR_FOREIGN_ROUTE";
 case ESC_IDLE_UNCONFIRMED:return "EP_SELECTOR_IDLE_UNCONFIRMED";
 case ESC_ROUTE_CHANGED:return "EP_SELECTOR_CHANGED_AFTER_IDLE_CHECK";
 case ESC_DEADLINE:return "EP_SELECTOR_READBACK_DEADLINE";
 default:return "EP_SELECTOR_NOT_CHECKED";
 }
}
#endif
