#include <assert.h>
#include <stdio.h>
#include "../src/ldc_timed_wait.h"
struct fixture {uint32_t now,start,calls,observations,mode,error,queued;};
static uint32_t ms(void*v){return ((struct fixture*)v)->now;}
static int error(void*v){return ((struct fixture*)v)->error;}
static int call(void*v,uint32_t*p){
 struct fixture*f=v;assert(p[1]==5&&p[0]>0&&p[0]<=250);f->calls++;
 if(f->queued){f->queued=0;f->error=0;return 10;}
 if(f->mode==1&&f->calls==3){f->now+=17;f->error=0;return 40;}
 if(f->mode==2){f->error=EINTR;return -1;}
 if(f->mode==3){f->error=EIO;return -1;}
 if(f->mode==4){f->error=0;return 0;}
 f->now+=p[0];f->error=ETIMEDOUT;return -1;
}
static void observe(void*v){struct fixture*f=v;f->observations++;if(f->mode==5)f->queued=1;if(f->mode==6)f->now+=13;}
int main(void){
 for(unsigned mode=0;mode<9;mode++){
  struct fixture f={.now=1000,.start=1000,.mode=mode};if(mode==7)f.now=f.start=0xffffff00u;if(mode==8)f.now+=5000;
  struct ldc_wait_ops o={&f,ms,call,error,observe};errno=0;int rc=ldc_timed_wait(&o,f.start,5000);
  if(mode==1){assert(rc==40&&f.calls==3&&f.observations==2);}
  else if(mode==5){assert(rc==10&&f.calls==2&&!f.queued);}
  else{assert(rc<0);if(mode==2)assert(errno==EINTR&&f.calls==1);else if(mode==3||mode==4)assert(errno==EIO&&f.calls==1);else assert(errno==ETIMEDOUT&&(uint32_t)(f.now-f.start)>=5000);}
  if(mode==0||mode==7)assert(f.calls==20&&(uint32_t)(f.now-f.start)==5000);
  if(mode==6)assert(f.calls<21&&(uint32_t)(f.now-f.start)<5014);
  if(mode==8)assert(!f.calls);
  printf("PASS bounded CORE wait mode=%u calls=%u rc=%d errno=%d\n",mode,f.calls,rc,errno);
 }
 return 0;
}
