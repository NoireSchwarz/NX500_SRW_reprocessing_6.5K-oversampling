/* Bounded original CORE wait calls. No queue reset/re-enable between slices.
 * Original kernel c02cce80 calls wait_for_completion_timeout without init.
 * A completion arriving between slices remains queued for the next wait. */
#ifndef NXL_LDC_TIMED_WAIT_H
#define NXL_LDC_TIMED_WAIT_H
#include <errno.h>
#include <stdint.h>
struct ldc_wait_ops {void*ctx;uint32_t(*ms)(void*);int(*call)(void*,uint32_t*);int(*error)(void*);void(*observe)(void*);};
static int ldc_timed_wait(const struct ldc_wait_ops*o,uint32_t started,uint32_t budget){
 for(;;){
  uint32_t elapsed=o->ms(o->ctx)-started;
  if(elapsed>=budget){errno=ETIMEDOUT;return -1;}
  uint32_t left=budget-elapsed,request[2]={left>250u?250u:left,5};
  int rc=o->call(o->ctx,request),error=o->error(o->ctx);
  if(rc>0)return rc;
  if(rc<0&&error!=ETIMEDOUT){errno=error;return rc;}
  if(o->observe)o->observe(o->ctx);
  /* An immediate zero/error without elapsed time cannot spin indefinitely. */
  if(o->ms(o->ctx)==started+elapsed){errno=EIO;return -1;}
 }
}
#endif
