#include <assert.h>
#include <stdio.h>
#include "../../experiments/selector-confirm.h"
struct fixture {unsigned at,idle_calls,pauses,idle_ok,idle_transient;struct esc_observation value[18];};
static int read_sample(void*v,struct esc_observation*s){struct fixture*f=v;*s=f->value[f->at<17?f->at:17];f->at++;return s->context_matches!=99;}
static int idle(void*v){struct fixture*f=v;f->idle_calls++;if(f->idle_transient){f->idle_transient--;return 2;}return f->idle_ok;}
static void delay(void*v,unsigned ms){struct fixture*f=v;assert(ms==10);f->pauses++;}
static void setup(struct fixture*f){*f=(struct fixture){0};f->idle_ok=1;for(unsigned i=0;i<18;i++)f->value[i]=(struct esc_observation){5,0,2,1};}
static void run(unsigned number,struct fixture*f,unsigned expect){
 const struct esc_ops o={f,read_sample,idle,delay,NULL};struct esc_result r;int ok=esc_confirm(&o,2,&r);assert(r.status==expect&&ok==(expect==ESC_CONFIRMED));
 assert(r.samples==f->at&&r.samples<=17&&r.pauses==f->pauses&&r.pauses<=15&&r.idle_checks==f->idle_calls&&r.idle_checks<=16);
 if(ok)assert(r.last.selector==5&&r.last.references==2&&!r.last.error&&r.last.context_matches==1&&r.idle_checks>=1);
 printf("case=%u status=%u samples=%u pauses=%u idle=%u first=%u last=%u\n",number,r.status,r.samples,r.pauses,r.idle_checks,r.first.selector,r.last.selector);
}
int main(void){
 struct fixture f;unsigned n=0;setup(&f);run(n++,&f,ESC_CONFIRMED);
 for(unsigned lag=1;lag<=15;lag++){setup(&f);for(unsigned i=0;i<lag;i++)f.value[i].selector=0;run(n++,&f,ESC_CONFIRMED);}
 setup(&f);for(unsigned i=0;i<18;i++)f.value[i].selector=0;run(n++,&f,ESC_DEADLINE);assert(!f.idle_calls);
 setup(&f);f.value[0].selector=3;run(n++,&f,ESC_FOREIGN_ROUTE);
 setup(&f);f.value[0].selector=99;run(n++,&f,ESC_FOREIGN_ROUTE);
 setup(&f);f.value[0].references=3;run(n++,&f,ESC_REFERENCE_CHANGED);
 setup(&f);f.value[0].context_matches=0;run(n++,&f,ESC_REFERENCE_CHANGED);
 setup(&f);f.value[0].error=7;run(n++,&f,ESC_DRIVER_ERROR);
 setup(&f);f.value[0].context_matches=99;run(n++,&f,ESC_READ_FAILED);
 setup(&f);f.idle_ok=0;run(n++,&f,ESC_IDLE_UNCONFIRMED);
 setup(&f);f.value[1].selector=0;run(n++,&f,ESC_ROUTE_CHANGED);
 setup(&f);f.value[1].selector=3;run(n++,&f,ESC_FOREIGN_ROUTE);
 setup(&f);f.value[1].references=1;run(n++,&f,ESC_REFERENCE_CHANGED);
 setup(&f);f.value[1].context_matches=99;run(n++,&f,ESC_READ_FAILED);
 setup(&f);f.idle_transient=1;run(n++,&f,ESC_CONFIRMED);assert(f.idle_calls==2);
 setup(&f);f.idle_ok=2;run(n++,&f,ESC_DEADLINE);assert(f.idle_calls==16);
 assert(n==30);puts("PASS30 selector readback cases; zero native calls, CAP or device writes");return 0;
}
