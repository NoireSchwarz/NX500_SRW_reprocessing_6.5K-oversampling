/* SPDX-License-Identifier: AGPL-3.0-only.
 * Read-only host preflight. No native command, pointer write or state repair.
 * The original front_idle predicates are retained; child pointers additionally
 * require word alignment before a read can be attempted.
 */
#ifndef NXL_FRONT_READINESS_H
#define NXL_FRONT_READINESS_H
#include <stdint.h>
#include <string.h>

#define FR_TASK 0x8153e6d4u
#define FR_FIELDS 16u
#define FR_STABILITY_BIT (1u << FR_FIELDS)
#define FR_WAIT_MS 5000u
#define FR_SAMPLE_MS 20u
#define FR_REQUIRED_STABLE 3u
enum fr_rule { FR_EQUAL, FR_NOT_EQUAL, FR_POINTER };
struct fr_descriptor { const char *name; uint32_t address,expected,mask; enum fr_rule rule; };
static const struct fr_descriptor fr_descriptors[FR_FIELDS]={
 {"still_vtable",FR_TASK,0x80592640u,0xffffffffu,FR_EQUAL},
 {"still_state",FR_TASK+3364,7,255,FR_EQUAL},
 {"still_pending_0",FR_TASK+3204,0,0xffffffffu,FR_EQUAL},
 {"still_pending_1",FR_TASK+3208,0,0xffffffffu,FR_EQUAL},
 {"still_active_initial_0",FR_TASK+3284,0,0xffffffffu,FR_EQUAL},
 {"still_active_initial_1",FR_TASK+3288,0,0xffffffffu,FR_EQUAL},
 {"controller_pointer",FR_TASK+3428,0,0xffffffffu,FR_POINTER},
 {"controller_vtable",0,0x805926c8u,0xffffffffu,FR_EQUAL},
 {"owner_pointer",0x81543d40u,0,0xffffffffu,FR_POINTER},
 {"owner_state",0,1,255,FR_EQUAL},
 {"ep_vtable",0x8142d154u,0x80582820u,0xffffffffu,FR_EQUAL},
 {"ep_active_owner",0x8142d154u+3444,0x8142df3cu,0xffffffffu,FR_NOT_EQUAL},
 {"still_active_repeat_a_0",FR_TASK+3284,0,0xffffffffu,FR_EQUAL},
 {"still_active_repeat_a_1",FR_TASK+3288,0,0xffffffffu,FR_EQUAL},
 {"still_active_repeat_b_0",FR_TASK+3284,0,0xffffffffu,FR_EQUAL},
 {"still_active_repeat_b_1",FR_TASK+3288,0,0xffffffffu,FR_EQUAL}
};
struct fr_field { uint32_t address,value,readable,passed; };
struct fr_snapshot { struct fr_field field[FR_FIELDS]; uint32_t read_mask,failure_mask,stable_pair; };
struct fr_wait_result {
 struct fr_snapshot first,last;
 uint32_t samples,consecutive,elapsed_ms,cancelled,timed_out,report_ok,ready,clock_failed;
};
typedef int (*fr_reader)(void *,uint32_t,uint32_t *,unsigned);
typedef uint64_t (*fr_clock)(void *);
typedef void (*fr_pause)(void *,unsigned);
typedef int (*fr_cancel)(void *);
typedef int (*fr_observer)(void *,const struct fr_wait_result *,unsigned);
static int fr_pointer_valid(uint32_t p){return p>=0x81000000u&&p<=0x93fff000u&&!(p&3u);}
static void fr_read(struct fr_snapshot *s,fr_reader read,void *context,unsigned first,unsigned count,uint32_t address){
 uint32_t values[2]={0,0};int ok=read(context,address,values,count);
 for(unsigned n=0;n<count;n++){
  struct fr_field *f=s->field+first+n;f->address=address+n*4;f->readable=(unsigned)ok;
  if(ok)f->value=values[n];
 }
}
static void fr_take(struct fr_snapshot *s,fr_reader read,void *context){
 memset(s,0,sizeof *s);
 for(unsigned i=0;i<FR_FIELDS;i++)s->field[i].address=fr_descriptors[i].address;
 fr_read(s,read,context,0,1,FR_TASK);fr_read(s,read,context,1,1,FR_TASK+3364);
 fr_read(s,read,context,2,2,FR_TASK+3204);fr_read(s,read,context,4,2,FR_TASK+3284);
 fr_read(s,read,context,6,1,FR_TASK+3428);
 s->field[7].address=s->field[6].value;
 if(s->field[6].readable&&fr_pointer_valid(s->field[6].value))fr_read(s,read,context,7,1,s->field[6].value);
 fr_read(s,read,context,8,1,0x81543d40u);
 s->field[9].address=s->field[8].value;
 if(s->field[8].readable&&fr_pointer_valid(s->field[8].value))fr_read(s,read,context,9,1,s->field[8].value);
 fr_read(s,read,context,10,1,0x8142d154u);fr_read(s,read,context,11,1,0x8142d154u+3444);
 fr_read(s,read,context,12,2,FR_TASK+3284);fr_read(s,read,context,14,2,FR_TASK+3284);
 for(unsigned i=0;i<FR_FIELDS;i++){
  const struct fr_descriptor *d=fr_descriptors+i;struct fr_field *f=s->field+i;
  if(f->readable){
   s->read_mask|=1u<<i;
   f->passed=d->rule==FR_POINTER?fr_pointer_valid(f->value):
    (d->rule==FR_NOT_EQUAL?(f->value&d->mask)!=d->expected:(f->value&d->mask)==d->expected);
  }
  if(!f->passed)s->failure_mask|=1u<<i;
 }
 s->stable_pair=s->field[12].readable&&s->field[13].readable&&s->field[14].readable&&s->field[15].readable&&
  s->field[12].value==s->field[14].value&&s->field[13].value==s->field[15].value;
 if(!s->stable_pair)s->failure_mask|=FR_STABILITY_BIT;
}
/* Admission to the original SetConfig is distinct from permission to Start.
 * Only the separately built experiment accepts the observed, fully readable
 * retired owner byte0. The original failure bits remain in every report. */
static int fr_config_admitted(const struct fr_snapshot *s){
 if(!s->failure_mask)return 1;
#ifdef NXL_COLD_INIT_TEST
 if(s->failure_mask!=(1u<<9)||s->read_mask!=0xffffu||!s->stable_pair||
    (s->field[9].value&255u)||s->field[9].passed)return 0;
 for(unsigned i=0;i<FR_FIELDS;i++)if(!s->field[i].readable||(i!=9&&!s->field[i].passed))return 0;
 return 1;
#else
 return 0;
#endif
}
static int fr_wait_mode(void *context,fr_reader read,fr_clock clock,fr_pause pause,fr_cancel cancelled,fr_observer observe,struct fr_wait_result *r,unsigned config_only){
 memset(r,0,sizeof *r);r->report_ok=1;uint64_t start=clock(context),previous=start;
 for(;;){
  fr_take(&r->last,read,context);r->samples++;
  r->cancelled=cancelled?(unsigned)cancelled(context):0;
  if(!r->last.failure_mask||(config_only&&fr_config_admitted(&r->last)))r->consecutive++;else r->consecutive=0;
  if(r->samples==1){r->first=r->last;if(observe&&!observe(context,r,0)){r->report_ok=0;break;}}
  if(r->cancelled)break;
  /* Include snapshot/report I/O, retain 64-bit elapsed time, and refuse a
   * backwards clock. A sample cap also bounds a failed/stalled clock. */
  uint64_t now=clock(context);
  if(now<previous){r->clock_failed=r->timed_out=1;break;}
  previous=now;uint64_t elapsed=now-start;
  r->elapsed_ms=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
  if(r->consecutive>=FR_REQUIRED_STABLE&&elapsed<=FR_WAIT_MS&&
     elapsed>=(FR_REQUIRED_STABLE-1u)*FR_SAMPLE_MS){r->ready=1;break;}
  if(r->elapsed_ms>=FR_WAIT_MS){r->timed_out=1;break;}
  if(r->samples>=FR_WAIT_MS/FR_SAMPLE_MS+1u){r->clock_failed=r->timed_out=1;break;}
  unsigned remaining=FR_WAIT_MS-r->elapsed_ms;pause(context,remaining<FR_SAMPLE_MS?remaining:FR_SAMPLE_MS);
 }
 uint64_t finish=clock(context);
 if(finish<previous){r->clock_failed=r->timed_out=1;r->ready=0;}
 else{uint64_t elapsed=finish-start;r->elapsed_ms=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;if(r->ready&&elapsed>FR_WAIT_MS){r->ready=0;r->timed_out=1;}}
 if(observe&&!observe(context,r,1))r->report_ok=0;
 return r->ready&&r->report_ok&&!r->cancelled&&!r->clock_failed;
}
/* All ordinary callers, including post-frame handoff, remain strict. */
static int fr_wait(void *context,fr_reader read,fr_clock clock,fr_pause pause,fr_cancel cancelled,fr_observer observe,struct fr_wait_result *r){
 return fr_wait_mode(context,read,clock,pause,cancelled,observe,r,0);
}
#endif
