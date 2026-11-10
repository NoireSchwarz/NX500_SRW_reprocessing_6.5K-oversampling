#!/usr/bin/env python3
"""Compile actual old/new preflight callbacks and compare failure decisions.
No camera, firmware API, DMA, process control or hardware is exercised.
"""
from pathlib import Path
import argparse,subprocess,tempfile,json
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "ep_sync_core.h"
struct native_context {volatile uint32_t *top,*lut,*rtos;unsigned lut_mode;FILE *log,*gate_log;unsigned gate_state_calls,gate_idle_calls,gate_rejections;const char *gate_last_type,*gate_last_reason;};
static struct native_context c;
static unsigned char kernel_before[EP_KERNEL_STATE_BYTES+12],raw_state[EP_KERNEL_STATE_BYTES];
static unsigned char report_kernel[EP_KERNEL_STATE_BYTES+12];
static uint32_t mutex_words[3],top[2048],jpeg[1024],rtos[1024];
static unsigned owner_captured,read_failure,sleeps;static int mutation_bank;static unsigned mutation_offset,mutation_value,sleep_failure;
static int kernel_read(struct native_context *v,uint32_t va,void *out,size_t n){
 (void)v;if((va==EP_KERNEL_STATE_VA&&read_failure==1)||(va==EP_KERNEL_MUTEX_VA&&read_failure==2))return 0;
 if(va==EP_KERNEL_STATE_VA){assert(n==sizeof raw_state);memcpy(out,raw_state,n);return 1;}
 assert(va==EP_KERNEL_MUTEX_VA&&n==12);memcpy(out,mutex_words,n);return 1;
}
static int sample_sleep(const struct timespec *a,struct timespec *b){
 (void)a;(void)b;sleeps++;
 if(sleep_failure){errno=EINTR;return -1;}
 if(sleeps==1&&mutation_bank>=0){uint32_t *p=mutation_bank==0?top:mutation_bank==1?jpeg:rtos;p[mutation_offset/4]^=mutation_value;}
 return 0;
}
#define nanosleep sample_sleep
'''
POST=r'''
static void reset_model(void){
 memset(top,0,sizeof top);memset(jpeg,0,sizeof jpeg);memset(rtos,0,sizeof rtos);
 memcpy(kernel_before,report_kernel,sizeof kernel_before);memcpy(raw_state,kernel_before,sizeof raw_state);memcpy(mutex_words,kernel_before+EP_KERNEL_STATE_BYTES,12);
 owner_captured=1;read_failure=sleeps=sleep_failure=0;mutation_bank=-1;mutation_offset=mutation_value=0;
 c.top=top;c.lut=jpeg;c.rtos=rtos;c.gate_state_calls=c.gate_idle_calls=c.gate_rejections=0;c.gate_last_type=c.gate_last_reason=NULL;
}
struct idle_case {int bank;unsigned off,val,after_sleep;const char *reason;};
static const struct idle_case cases[]={CASES};
static void configure_idle(const struct idle_case *t){
 reset_model();
 if(t->bank==-2){sleep_failure=1;return;}
 if(t->bank<0)return;
 if(t->after_sleep){mutation_bank=t->bank;mutation_offset=t->off;mutation_value=t->val;return;}
 uint32_t *p=t->bank==0?top:t->bank==1?jpeg:rtos;p[t->off/4]=t->val;
}
static void configure_state(unsigned n,unsigned *locked){
 reset_model();*locked=0;
 if(n==1)owner_captured=0;
 if(n==2)read_failure=1;
 if(n==3)read_failure=2;
 if(n>=4&&n<=6)mutex_words[n-4]^=1;
 if(n>=7&&n<=11){uint32_t one=1;memcpy(kernel_before+0x64+(n-7)*4,&one,4);memcpy(raw_state,kernel_before,sizeof raw_state);}
 if(n==12)raw_state[0x100]^=1;
 if(n>=13&&n<=15){unsigned off=0xc0755504-EP_KERNEL_STATE_VA+(n-13)*4;uint32_t wrong=n==13?1:0;memcpy(kernel_before+off,&wrong,4);memcpy(raw_state,kernel_before,sizeof raw_state);}
 if(n==16){*locked=1;mutex_words[0]=0;}
 if(n==17){*locked=2;mutex_words[0]=0;uint32_t mask=0x24;memcpy(raw_state+0x74,&mask,4);}
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f&&fread(report_kernel,1,sizeof report_kernel,f)==sizeof report_kernel&&fgetc(f)==EOF);fclose(f);
 c.log=fopen("gate-model-main.log","w+");c.gate_log=fopen("gate-model-details.log","w+");assert(c.log&&c.gate_log);
 for(unsigned n=0;n<18;n++){
  unsigned locked=0;configure_state(n,&locked);int old=original_state(&c,locked);configure_state(n,&locked);int now=state(&c,locked);assert(old==now);assert(now==(n==0||n==16||n==17));
  if(!now)assert(c.gate_rejections==1&&c.gate_last_reason);
 }
 for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
  configure_idle(cases+i);int old=original_idle(&c);unsigned old_sleeps=sleeps;
  configure_idle(cases+i);int now=idle(&c);assert(old==now&&sleeps==old_sleeps);assert(now==(i==0));
  if(!now)assert(c.gate_rejections==1&&c.gate_last_reason&&!strcmp(c.gate_last_reason,cases[i].reason));
 }
 fclose(c.log);fclose(c.gate_log);printf("PASS 18 state models including real p21 kernel snapshot; %u idle models; identical decisions and sampling counts; no hardware\n",(unsigned)(sizeof cases/sizeof *cases));return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('original_worker_source',type=Path);p.add_argument('real_kernel_snapshot',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 s=a.original_worker_source.read_text();old=s[s.index('static int queue_empty('):s.index('\nint main(')].replace('queue_empty','original_queue_empty').replace('static int state(','static int original_state(').replace('static int idle(','static int original_idle(')
 cases=[(-1,0,0,0,'PASS'),(0,0x1020,0x80,0,'JPEG_CLOCK_GATED'),(0,0x1024,0x20000,0,'JPEG_CLOCK_GATED')]
 cases += [(2,0xaac+4*i,1,0,'RTOS_MASK_NONZERO') for i in range(5)]
 cases += [(2,0xc98,1,0,'RTOS_OWNER_NONZERO')]+[(0,x,1,0,'OTF_ACTIVE') for x in [0x1118,0x1218]]
 cases += [(0,0x1070+4*i,32,0,'TOP_MASK_DIFF') for i in range(5)]+[(0,0x10c8+4*i,32,0,'RAW_PENDING') for i in range(5)]
 cases += [(1,x,1,1,'JPEG_REGISTER_CHANGED') for x in [0,4,8,0x280,0x800,0x804,0x808,0x80c,0x810,0x814,0x818,0x820]]
 cases += [(0,(0x1700 if (i//2)%2 else 0x1300)+(i//8)*256+((i//4)%2)*128+(i%2)*4,16,1,'DMA_ADDRESS_CHANGED') for i in range(32)]
 cases += [(2,0xaac+4*i,1,1,'RTOS_MASK_CHANGED') for i in range(5)]
 cases += [(0,0x1070+4*i,1,1,'TOP_MASK_DIFF') for i in range(5)]+[(0,0x10c8+4*i,1,1,'RAW_PENDING') for i in range(5)]+[(-2,0,0,0,'SAMPLE_SLEEP_FAILED')]
 literals=','.join('{%d,%du,%du,%du,"%s"}'%t for t in cases)
 with tempfile.TemporaryDirectory() as td:
  source=Path(td)/'model.c';exe=Path(td)/'model'
  source.write_text(PRE+old+'\n#include "jpeg_gate_diag.inc"\n'+POST.replace('CASES',literals))
  subprocess.run(['gcc','-O1','-g','-fsanitize=undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(ROOT/'src'),str(source),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe),str(a.real_kernel_snapshot.resolve())],cwd=td,capture_output=True,text=True)
  if result.returncode:raise RuntimeError(result.stderr)
  print(result.stdout,end='')
 if a.output:a.output.write_text(json.dumps({'state_models':18,'idle_models':len(cases),'passed':18+len(cases),'same_acceptance_and_sleep_count':True,'real_input':'Failed p21 report kernel snapshot','hardware_tested':False,'stdout':result.stdout},indent=2)+'\n')
if __name__=='__main__':main()
