/* SPDX-License-Identifier: AGPL-3.0-only. Actual production read-only gate,
 * modeled /proc, queue, memory and time. No camera or native calls. */
#include <assert.h>
#include <stdio.h>
#include "shell_idle.h"
#include "alloc_core.h"

#define LIBC 0xb31e1000u
#define SHELL 0xb2b20000u
#define QUEUE 0x48006
static const char field[]="0 0x48006 0xafff69b0 0xd0 0x0 0x0 0xafff68b0 0xafff6890 0xb32a6c10";
struct model {
 char syscall[512],wchan[128];uint64_t now;
 unsigned named,queue_reads,line_reads,word_reads,notes,pauses;
 unsigned queue_busy,queue_change,line_fail,word_fail,worker_fail,wrong_lr,change_frame;
 unsigned sleep_fail,clock_fail,backwards,stalled,huge_step,busy_polls;
};
static void init(struct model*m){memset(m,0,sizeof *m);strcpy(m->syscall,field);strcpy(m->wchan,"do_msgrcv");m->named=1;m->now=100;}
static int q(void*x,int*out){struct model*m=x;m->queue_reads++;if(m->queue_busy)return 0;if(m->busy_polls){m->busy_polls--;return 0;}*out=QUEUE+(m->queue_change&&m->queue_reads%2==0);return 1;}
static int worker(void*x,unsigned*n,int*t){struct model*m=x;*n=m->named;*t=259;return !m->worker_fail;}
static int ln(void*x,int tid,const char*leaf,char*out,size_t cap){
 struct model*m=x;assert(tid==259);m->line_reads++;if(m->line_fail)return 0;
 const char*s=!strcmp(leaf,"syscall")?m->syscall:m->wchan;
 if(m->change_frame&&m->line_reads==3)s="0 0x48006 0xafff69c0 0xd0 0 0 0 0xafff68a0 0xb32a6c10";
 assert(strlen(s)<cap);strcpy(out,s);return 1;
}
static int word(void*x,uint32_t address,uint32_t*out){struct model*m=x;m->word_reads++;assert(address==0xafff6898u||m->change_frame);*out=SHELL+0x7344u+m->wrong_lr;return !m->word_fail;}
static void note(void*x,int tid,const char*leaf,const char*text){struct model*m=x;assert(tid==259&&leaf&&text);m->notes++;}
static uint64_t clk(void*x){struct model*m=x;return m->clock_fail?UINT64_MAX:m->now;}
static int sleep_ms(void*x,unsigned ms){struct model*m=x;m->pauses++;if(m->sleep_fail)return 0;if(m->backwards)m->now--;else if(m->huge_step)m->now+=(1ULL<<32);else if(!m->stalled)m->now+=ms;return 1;}
static const struct shell_idle_ops ops={q,worker,ln,word,note,clk,sleep_ms};
static int once(struct model*m,struct shell_idle_report*r){return shell_idle_once(m,&ops,LIBC,SHELL,r);}
static int wait_idle(struct model*m,struct shell_idle_report*r){return shell_idle_wait(m,&ops,LIBC,SHELL,r);}
static unsigned checks;
#define CHECK(c) do{assert(c);checks++;}while(0)

int main(void){
 struct model m;struct shell_idle_report r;struct shell_frame frame;
 CHECK(shell_parse(field,&frame));CHECK(frame.number==0);
 /* Reproduces the old field regression: old no!=302 gate rejects this exact
  * record, despite a supported PC/SP and the original verified r7=302 frame. */
 CHECK(frame.number!=302);struct alloc_regs native={{0}};
 native.r[1]=0xafff69b0u;native.r[2]=208;native.r[7]=302;
 native.r[13]=0xafff6890u;native.r[15]=0xb32a6c10u;native.r[17]=QUEUE;
 CHECK(alloc_parked_regs(&native,LIBC,SHELL,QUEUE,SHELL+0x7344u));
 init(&m);CHECK(once(&m,&r));CHECK(r.parked&&r.queue_empty&&r.named==1&&m.word_reads==2&&m.queue_reads==2&&m.notes==4);
 init(&m);m.syscall[0]='3';strcpy(m.syscall,"302 0x48006 0xafff69b0 0xd0 0 0 0 0xafff6890 0xb32a6c10");CHECK(once(&m,&r));
 const char*bad[]={
  "running","-1 0xafff6890 0xb32a6c10","",field,
  "303 0x48006 0xafff69b0 0xd0 0 0 0 0xafff6890 0xb32a6c10",
  "0 0x48007 0xafff69b0 0xd0 0 0 0 0xafff6890 0xb32a6c10",
  "0 0x48006 0xafff69b0 0xcf 0 0 0 0xafff6890 0xb32a6c10",
  "0 0x48006 0xafff69b0 0xd0 1 0 0 0xafff6890 0xb32a6c10",
  "0 0x48006 0xafff69b0 0xd0 0 1 0 0xafff6890 0xb32a6c10",
  "0 0x48006 0xafff69ac 0xd0 0 0 0 0xafff6890 0xb32a6c10",
  "0 0x48006 0xafff69b1 0xd0 0 0 0 0xafff6891 0xb32a6c10",
  "0 0x48006 0xafff69b0 0xd0 0 0 0 0xafff6890 0xb32a6c14",
  "0 0x48006 0xafff69b0 0xd0 0 0 0 0xafff6890 0x1b32a6c10",
  "0 0x48006 0x100000120 0xd0 0 0 0 0xfffffff8 0xb32a6c10",
  "0 0x48006 0xafff69b0 0xd0 0 0 0 0xafff6890 0xb32a6c10 trailing",
  "0 0x48006 0xafff69a4 0xd0 0 0 0 0xafff6890 0xb32a6be4"
 };
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){
  init(&m);strcpy(m.syscall,bad[i]);if(i==3)strcpy(m.wchan,"do_futex");
  CHECK(!once(&m,&r));CHECK(!r.parked);
 }
 for(unsigned i=0;i<10;i++){
  init(&m);
  switch(i){case 0:m.named=0;break;case 1:m.named=2;break;case 2:m.queue_busy=1;break;case 3:m.queue_change=1;break;case 4:m.line_fail=1;break;case 5:m.word_fail=1;break;case 6:m.wrong_lr=4;break;case 7:m.worker_fail=1;break;case 8:m.change_frame=1;break;case 9:strcpy(m.wchan,"0");break;}
  CHECK(!once(&m,&r));CHECK(!r.parked);
 }
 init(&m);CHECK(wait_idle(&m,&r));CHECK(r.samples==3&&r.consecutive==3&&r.elapsed_ms==40&&m.pauses==2);
 init(&m);m.busy_polls=2;CHECK(wait_idle(&m,&r));CHECK(r.samples==5&&r.elapsed_ms==80);
 init(&m);m.queue_busy=1;CHECK(!wait_idle(&m,&r));CHECK(r.samples==101&&r.elapsed_ms==2000&&!r.parked);
 init(&m);m.stalled=1;CHECK(!wait_idle(&m,&r));CHECK(r.samples==101&&!strcmp(r.reason,"SHELL_CLOCK_NOT_ADVANCING"));
 init(&m);m.backwards=1;CHECK(!wait_idle(&m,&r));CHECK(!strcmp(r.reason,"SHELL_CLOCK_FAILED")&&!r.parked);
 init(&m);m.clock_fail=1;CHECK(!wait_idle(&m,&r));CHECK(!m.queue_reads);
 init(&m);m.huge_step=1;CHECK(!wait_idle(&m,&r));CHECK(r.elapsed_ms==UINT32_MAX&&!r.parked);
 init(&m);m.sleep_fail=1;CHECK(!wait_idle(&m,&r));CHECK(!strcmp(r.reason,"SHELL_SAMPLE_SLEEP_FAILED"));
 /* Failed observations reset fields instead of retaining prior success. */
 init(&m);CHECK(once(&m,&r));m.queue_busy=1;CHECK(!once(&m,&r)&&!r.queue_empty&&!r.parked);
 printf("PASS shell_idle_r7c checks=%u; exact field record, guarded kernel quirk, frame races, queue and LR refusal, bounded waits\n",checks);
 return 0;
}
