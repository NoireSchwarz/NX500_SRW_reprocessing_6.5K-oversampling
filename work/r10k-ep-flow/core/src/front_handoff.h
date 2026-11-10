/* Read-only Still handoff: the original readiness predicates must pass in
 * three spaced observations. Ownership, code and API facts are hard gates;
 * their loss aborts immediately, never starts another native operation. */
#ifndef NXL_FRONT_HANDOFF_H
#define NXL_FRONT_HANDOFF_H
#include "front_readiness.h"
#define FH_OWNED 1u
#define FH_SLOT_ORIGINAL 2u
#define FH_DETACHED 4u
#define FH_API_QUIET 8u
#define FH_REGISTERS_RESTORED 16u
#define FH_CAMERA_RESUMED 32u
#define FH_NATIVE_DONE 64u
#define FH_REQUIRED 127u
typedef uint32_t (*fh_context_reader)(void *);
struct fh_result {struct fr_wait_result readiness;uint32_t first_context,last_context;};
typedef int (*fh_observer)(void *,const struct fh_result *,unsigned);
struct fh_adapter {
 void *context;fr_reader read;fr_clock clock;fr_pause pause;fr_cancel cancelled;
 fh_context_reader proof;fh_observer observe;struct fh_result *result;
};
static int fh_read(void *opaque,uint32_t address,uint32_t *out,unsigned words){
 struct fh_adapter *a=opaque;
 if(address==FR_TASK){a->result->last_context=a->proof(a->context);if(!a->result->readiness.samples)a->result->first_context=a->result->last_context;}
 return a->result->last_context==FH_REQUIRED&&a->read(a->context,address,out,words);
}
static uint64_t fh_clock(void *opaque){struct fh_adapter*a=opaque;return a->clock(a->context);}
static void fh_pause(void *opaque,unsigned ms){struct fh_adapter*a=opaque;a->pause(a->context,ms);}
static int fh_cancel(void *opaque){struct fh_adapter*a=opaque;return a->result->last_context!=FH_REQUIRED||(a->cancelled&&a->cancelled(a->context));}
static int fh_report(void *opaque,const struct fr_wait_result *r,unsigned final){
 struct fh_adapter*a=opaque;(void)r;return !a->observe||a->observe(a->context,a->result,final);
}
static int fh_wait(void *context,fr_reader read,fr_clock clock,fr_pause pause,fr_cancel cancel,fh_context_reader proof,fh_observer observe,struct fh_result *r){
 memset(r,0,sizeof *r);struct fh_adapter a={context,read,clock,pause,cancel,proof,observe,r};
 return fr_wait(&a,fh_read,fh_clock,fh_pause,fh_cancel,fh_report,&r->readiness);
}
#endif
