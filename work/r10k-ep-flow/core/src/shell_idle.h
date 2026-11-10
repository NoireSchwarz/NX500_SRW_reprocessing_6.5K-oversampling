/* SPDX-License-Identifier: AGPL-3.0-only. Read-only NX500 1.12 receive proof. */
#ifndef NX51_SHELL_IDLE_H
#define NX51_SHELL_IDLE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SHELL_IDLE_WAIT_MS 2000u
#define SHELL_IDLE_SAMPLE_MS 20u
#define SHELL_IDLE_REQUIRED 3u
#define SHELL_IDLE_MAX_SAMPLES (SHELL_IDLE_WAIT_MS / SHELL_IDLE_SAMPLE_MS + 1u)

struct shell_frame {
 uint64_t number, arg[6], sp, pc;
};
struct shell_idle_report {
 struct shell_frame frame;
 const char *reason;
 unsigned named, queue_empty, parked, samples, consecutive, elapsed_ms;
 int tid, queue;
};
struct shell_idle_ops {
 int (*queue)(void *, int *);
 int (*worker)(void *, unsigned *, int *);
 int (*line)(void *, int, const char *, char *, size_t);
 int (*word)(void *, uint32_t, uint32_t *);
 void (*note)(void *, int, const char *, const char *);
 uint64_t (*clock_ms)(void *);
 int (*pause_ms)(void *, unsigned);
 int (*receive_context)(void *,const struct shell_frame *,uint32_t,uint32_t);
};

static int shell_parse(const char *text, struct shell_frame *out){
 unsigned long long n,a[6],sp,pc;int end=0;
 if(sscanf(text,"%llu %llx %llx %llx %llx %llx %llx %llx %llx %n",
   &n,a,a+1,a+2,a+3,a+4,a+5,&sp,&pc,&end)!=9||!end||text[end])return 0;
 memset(out,0,sizeof *out);out->number=n;out->sp=sp;out->pc=pc;
 for(unsigned i=0;i<6;i++)out->arg[i]=a[i];
 return 1;
}

static const char *shell_frame_error(const struct shell_frame *f,const char *wchan,
                                    int queue,uint32_t libc_base){
 /* This camera reports 0 in /proc/.../syscall even while ptrace confirms
  * r7=302. Only the exact, fingerprinted cancellation frame + do_msgrcv
  * may use that observed kernel quirk. Never accept arbitrary syscall 0. */
 if(f->number!=302&&(f->number!=0||strcmp(wchan,"do_msgrcv")))return "SHELL_SYSCALL_OR_WCHAN_MISMATCH";
 if(queue<0||f->arg[0]!=(unsigned)queue||f->arg[2]!=208||f->arg[3]||f->arg[4])return "SHELL_RECEIVE_ARGUMENTS_MISMATCH";
 if(f->sp>UINT32_MAX-12u||f->arg[1]>UINT32_MAX||f->pc>UINT32_MAX||
    (f->sp&3u)||f->arg[1]<f->sp||f->arg[1]-f->sp!=0x120u)return "SHELL_RECEIVE_STACK_MISMATCH";
 if(f->pc<libc_base||(f->pc-libc_base!=0xc5c0cu&&f->pc-libc_base!=0xc5c10u))return "SHELL_RECEIVE_PC_UNPROVEN";
 /* The fast libc path keeps LR in a register, absent from /proc/syscall.
  * It still requires the original ptrace verifier; do not infer its LR. */
 return NULL;
}

static int shell_idle_once(void *ctx,const struct shell_idle_ops *ops,
                           uint32_t libc_base,uint32_t shell_base,
                           struct shell_idle_report *r){
 memset(r,0,sizeof *r);r->reason="SHELL_QUEUE_BUSY_OR_UNREADABLE";
 if(!ops->queue(ctx,&r->queue))return 0;r->queue_empty=1;
 r->reason="SHELL_THREAD_NOT_UNIQUE_OR_UNREADABLE";
 if(!ops->worker(ctx,&r->named,&r->tid)||r->named!=1||r->tid<=1)return 0;
 struct shell_frame first={0},second={0};uint32_t lr[2]={0,0};
 for(unsigned pass=0;pass<2;pass++){
  char text[512]={0},wchan[128]={0};
  r->reason="SHELL_SYSCALL_UNREADABLE";
  if(!ops->line(ctx,r->tid,"syscall",text,sizeof text))return 0;
  if(ops->note)ops->note(ctx,r->tid,"syscall",text);
  r->reason="SHELL_SYSCALL_RECORD_INVALID";
  struct shell_frame *f=pass?&second:&first;
  if(!shell_parse(text,f))return 0;r->frame=*f;
  r->reason="SHELL_WCHAN_UNREADABLE";
  if(!ops->line(ctx,r->tid,"wchan",wchan,sizeof wchan))return 0;
  if(ops->note)ops->note(ctx,r->tid,"wchan",wchan);
  const char *error;
  if(ops->receive_context){
   error=(strcmp(wchan,"do_msgrcv")||(f->number!=302&&f->number!=0))?"SHELL_SYSCALL_OR_WCHAN_MISMATCH":
    r->queue<0||f->arg[0]!=(unsigned)r->queue||f->arg[2]!=208||f->arg[3]||f->arg[4]?"SHELL_RECEIVE_ARGUMENTS_MISMATCH":
    f->sp>UINT32_MAX-4u||f->arg[1]>UINT32_MAX-212u||f->pc>UINT32_MAX||(f->sp&3u)||(f->arg[1]&3u)?"SHELL_RECEIVE_RANGE_INVALID":NULL;
  }else error=shell_frame_error(f,wchan,r->queue,libc_base);
  if(error){r->reason=error;return 0;}
  if(ops->receive_context){
   r->reason="SHELL_RECEIVE_MAPPING_OR_INSTRUCTION_UNPROVEN";
   if(!ops->receive_context(ctx,f,libc_base,shell_base))return 0;
   continue;
  }
  r->reason="SHELL_SAVED_LR_UNREADABLE";
  if(!ops->word(ctx,(uint32_t)f->sp+8,lr+pass))return 0;
  r->reason="SHELL_SAVED_LR_MISMATCH";
  if((uint64_t)shell_base+0x7344u>UINT32_MAX||lr[pass]!=shell_base+0x7344u)return 0;
 }
 r->reason="SHELL_FRAME_CHANGED";
 if(memcmp(&first,&second,sizeof first)||lr[0]!=lr[1])return 0;
 int q=-1;r->reason="SHELL_QUEUE_CHANGED_OR_BUSY";
 if(!ops->queue(ctx,&q)||q!=r->queue){r->queue_empty=0;return 0;}
 r->parked=1;r->reason="SHELL_RECEIVE_VERIFIED";return 1;
}

static int shell_idle_wait(void *ctx,const struct shell_idle_ops *ops,
                           uint32_t libc_base,uint32_t shell_base,
                           struct shell_idle_report *r){
 struct shell_idle_report previous={0};unsigned consecutive=0;
 uint64_t start=ops->clock_ms(ctx),last=start;
 memset(r,0,sizeof *r);r->reason="SHELL_CLOCK_FAILED";
 if(start==UINT64_MAX)return 0;
 for(unsigned sample=1;sample<=SHELL_IDLE_MAX_SAMPLES;sample++){
  int ok=shell_idle_once(ctx,ops,libc_base,shell_base,r);
  uint64_t now=ops->clock_ms(ctx);r->samples=sample;
  if(now==UINT64_MAX||now<last){r->reason="SHELL_CLOCK_FAILED";r->parked=0;return 0;}
  uint64_t elapsed=now-start;last=now;
  r->elapsed_ms=elapsed>UINT32_MAX?UINT32_MAX:(unsigned)elapsed;
  if(elapsed>SHELL_IDLE_WAIT_MS){r->reason="SHELL_IDLE_TIMEOUT";r->parked=0;return 0;}
  if(ok){
   if(consecutive&&(previous.queue!=r->queue||previous.tid!=r->tid||
      memcmp(&previous.frame,&r->frame,sizeof r->frame)))consecutive=0;
   consecutive++;previous=*r;
  }else consecutive=0;
  r->consecutive=consecutive;
  if(consecutive>=SHELL_IDLE_REQUIRED&&elapsed>=(SHELL_IDLE_REQUIRED-1u)*SHELL_IDLE_SAMPLE_MS)return 1;
  if(elapsed>=SHELL_IDLE_WAIT_MS||sample==SHELL_IDLE_MAX_SAMPLES){
   if(ok)r->reason=elapsed<SHELL_IDLE_WAIT_MS?"SHELL_CLOCK_NOT_ADVANCING":"SHELL_IDLE_NOT_STABLE";
   r->parked=0;return 0;
  }
  unsigned remaining=SHELL_IDLE_WAIT_MS-(unsigned)elapsed;
  if(!ops->pause_ms(ctx,remaining<SHELL_IDLE_SAMPLE_MS?remaining:SHELL_IDLE_SAMPLE_MS)){
   r->reason="SHELL_SAMPLE_SLEEP_FAILED";r->parked=0;return 0;
  }
 }
 return 0;
}
#endif
