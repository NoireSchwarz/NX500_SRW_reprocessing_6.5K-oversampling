/* SPDX-License-Identifier: AGPL-3.0-only. No device access. */
#include <assert.h>
#include <stdio.h>
#include "front_readiness.h"
struct model {
 uint32_t value[FR_FIELDS],read_fail_mask,overrides;
 unsigned active_reads,samples,inject_field,inject_sample,invalid_reads;
 unsigned mode,observer_calls,fail_observer,cancel,clock_step,sleep_mode,observer_delay;
 uint64_t now;
};
static void initialize(struct model *m){
 memset(m,0,sizeof *m);m->inject_field=FR_FIELDS;
 for(unsigned i=0;i<FR_FIELDS;i++)m->value[i]=fr_descriptors[i].expected;
 m->value[6]=0x819f1000u;m->value[8]=0x819f0000u;m->value[11]=0x8142dee0u;
}
static int model_read(void *context,uint32_t address,uint32_t *out,unsigned words){
 struct model *m=context;unsigned at=FR_FIELDS;
 if(address==FR_TASK){m->active_reads=0;m->samples++;at=0;}
 else if(address==FR_TASK+3364)at=1;
 else if(address==FR_TASK+3204)at=2;
 else if(address==FR_TASK+3284){at=m->active_reads==0?4:(m->active_reads==1?12:14);m->active_reads++;}
 else if(address==FR_TASK+3428)at=6;
 else if(address==0x819f1000u||address==0x81000000u||address==0x93fff000u)at=7;
 else if(address==0x81543d40u)at=8;
 else if(address==0x819f0000u)at=9;
 else if(address==0x8142d154u)at=10;
 else if(address==0x8142d154u+3444)at=11;
 else{m->invalid_reads++;return 0;}
 assert(at+words<=FR_FIELDS);
 for(unsigned n=0;n<words;n++){
  unsigned i=at+n;if(m->read_fail_mask&(1u<<i))return 0;
  out[n]=m->value[i];
  if(m->inject_field==i&&m->inject_sample==m->samples)out[n]^=1;
  if(i==1){
   if(m->mode==1&&m->samples<4)out[n]=2;
   if(m->mode==2)out[n]=(m->samples%3==0)?2:7;
  }
 }
 return 1;
}
static uint64_t model_clock(void *context){struct model *m=context;return m->now;}
static void model_pause(void *context,unsigned ms){struct model *m=context;if(m->sleep_mode==1)return;if(m->sleep_mode==2){m->now--;return;}m->now+=(uint64_t)ms+m->clock_step;}
static int model_cancel(void *context){return ((struct model*)context)->cancel;}
static int model_observe(void *context,const struct fr_wait_result *r,unsigned final){
 struct model *m=context;m->observer_calls++;if(m->observer_calls==1)m->now+=m->observer_delay;
 assert(final==0||final==1);assert(r->samples>=1);
 if(final)assert(r->first.field[0].value==0x80592640u);
 return !(m->fail_observer&&m->observer_calls==m->fail_observer);
}
static int run_wait(struct model *m,struct fr_wait_result *r){return fr_wait(m,model_read,model_clock,model_pause,model_cancel,model_observe,r);}
int main(void){
 unsigned checks=0;struct model m;struct fr_snapshot s;struct fr_wait_result r;
 initialize(&m);fr_take(&s,model_read,&m);assert(!s.failure_mask&&s.read_mask==0xffffu&&s.stable_pair);checks++;
 /* Every field's predicate rejects a wrong value, without short circuiting
  * the other root observations. Invalid child pointers are never read. */
 for(unsigned i=0;i<FR_FIELDS;i++){
  initialize(&m);if(i==6||i==8)m.value[i]=0x80000000u;else if(i==11)m.value[i]=0x8142df3cu;else m.value[i]^=1;
  fr_take(&s,model_read,&m);assert(s.failure_mask&(1u<<i));assert(s.field[10].readable&&s.field[15].readable);assert(!m.invalid_reads);checks++;
 }
 /* Each read position can independently fail; pair read failures mark both
  * unreadable as the original ro_words(words=2) contract does. */
 for(unsigned i=0;i<FR_FIELDS;i++){
  initialize(&m);m.read_fail_mask=1u<<i;fr_take(&s,model_read,&m);
  assert(s.failure_mask&(1u<<i));assert(!(s.read_mask&(1u<<i)));assert(!m.invalid_reads);checks++;
 }
 /* State predicates inspect only the low byte, vtables and zero counters
  * require all 32 bits. EP owner is NOT_EQUAL, preserving the original gate. */
 initialize(&m);m.value[1]=0x12340007u;m.value[9]=0x89ab0001u;fr_take(&s,model_read,&m);assert(!s.failure_mask);checks++;
 initialize(&m);m.value[2]=0x100u;fr_take(&s,model_read,&m);assert(s.failure_mask&(1u<<2));checks++;
 initialize(&m);m.value[0]^=0x100u;fr_take(&s,model_read,&m);assert(s.failure_mask&1u);checks++;
 const uint32_t invalid[]={0,0x80fffffcu,0x93fff004u,0xffffffffu,0x819f1001u,0x819f1002u,0x819f1003u};
 for(unsigned pointer=6;pointer<=8;pointer+=2)for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){
  initialize(&m);m.value[pointer]=invalid[i];fr_take(&s,model_read,&m);
  assert(s.failure_mask&(1u<<pointer));assert(!s.field[pointer+1].readable);assert(s.field[pointer+1].address==invalid[i]);assert(!m.invalid_reads);checks++;
 }
 initialize(&m);m.value[6]=0x81000000u;fr_take(&s,model_read,&m);assert(!s.failure_mask);checks++;
 initialize(&m);m.value[6]=0x93fff000u;fr_take(&s,model_read,&m);assert(!s.failure_mask);checks++;
 initialize(&m);m.value[14]=1;fr_take(&s,model_read,&m);assert(s.failure_mask&FR_STABILITY_BIT);checks++;
 initialize(&m);assert(run_wait(&m,&r));assert(r.samples==3&&r.elapsed_ms==40&&r.consecutive==3&&m.observer_calls==2);checks++;
 initialize(&m);m.mode=1;assert(run_wait(&m,&r));assert(r.first.failure_mask&&(r.last.failure_mask==0)&&r.samples==6&&r.elapsed_ms==100);checks++;
 initialize(&m);m.value[1]=2;assert(!run_wait(&m,&r));assert(r.timed_out&&r.elapsed_ms==FR_WAIT_MS&&!r.ready&&r.samples==251&&m.observer_calls==2);checks++;
 initialize(&m);m.mode=2;assert(!run_wait(&m,&r));assert(r.timed_out&&!r.ready&&r.consecutive<3);checks++;
 initialize(&m);m.cancel=1;assert(!run_wait(&m,&r));assert(r.cancelled&&r.samples==1&&!r.ready&&m.observer_calls==2);checks++;
 initialize(&m);m.fail_observer=1;assert(!run_wait(&m,&r));assert(!r.report_ok&&r.samples==1);checks++;
 initialize(&m);m.fail_observer=2;assert(!run_wait(&m,&r));assert(!r.report_ok&&r.ready);checks++;
 initialize(&m);m.clock_step=3000;assert(!run_wait(&m,&r));assert(r.timed_out&&!r.ready&&r.samples==3);checks++;
 initialize(&m);m.sleep_mode=1;assert(!run_wait(&m,&r));assert(r.clock_failed&&r.timed_out&&!r.ready&&r.samples==251);checks++;
 initialize(&m);m.now=100;m.sleep_mode=2;assert(!run_wait(&m,&r));assert(r.clock_failed&&!r.ready&&r.samples==2);checks++;
 initialize(&m);m.clock_step=UINT32_MAX;assert(!run_wait(&m,&r));assert(r.timed_out&&r.elapsed_ms==UINT32_MAX&&r.samples==2);checks++;
 initialize(&m);m.observer_delay=6000;assert(!run_wait(&m,&r));assert(r.timed_out&&!r.ready&&r.samples==1);checks++;
 printf("PASS front_readiness checks=%u; every field reject/read-fail, pointer boundaries/alignment, low-byte masks, repeat stability, bounded 3-pass wait, transient, persistent, unstable, cancellation/report failure\n",checks);
 return 0;
}
