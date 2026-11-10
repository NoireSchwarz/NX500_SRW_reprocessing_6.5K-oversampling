#!/usr/bin/env python3
"""Actual controller wait function, virtual monotonic clock and event/IRQ fixtures.
No camera, scheduling performance, DMA or physical recovery is exercised.
"""
from pathlib import Path
import argparse,json,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <stdio.h>
#include "pipeline_timing.h"
static unsigned mode,ticks,interruptions,reads,polls;
static int stop_requested;
static uint64_t ns(void){return (uint64_t)ticks*1000000u;}
static int fake_poll(struct pollfd *p,nfds_t count,int timeout){
 assert(count==1&&timeout>0);polls++;
 if(mode==7){errno=EINVAL;return -1;}
 if(mode==4){ticks+=20000;stop_requested=1;errno=EINTR;return -1;}
 if(mode==9){unsigned n=(unsigned)timeout<10000?(unsigned)timeout:10000;ticks+=n;errno=EINTR;return -1;}
 if(mode==1&&interruptions<2){interruptions++;ticks+=20000;errno=EINTR;return -1;}
 unsigned due=mode==1?59000:mode==3?P7_WORKER_MS+5000:mode==8?P7_WORKER_MS:53000;
 if(mode==2||ticks+(unsigned)timeout<due){ticks+=(unsigned)timeout;return 0;}
 if(due>ticks)ticks=due;
 p->revents=mode==5?POLLHUP:POLLIN;return 1;
}
static ssize_t fake_read(int fd,void *data,size_t n){
 (void)fd;assert(n==1);reads++;
 if(mode==5)return 0;
 if(mode==10&&reads==1){ticks++;errno=EINTR;return -1;}
 *(char*)data=mode==6?'E':'D';return 1;
}
#define poll fake_poll
#define read fake_read
'''
POST=r'''
int main(void){
 for(mode=0;mode<11;mode++){
  ticks=interruptions=reads=polls=stop_requested=0;
  int ok=p7_wait_terminal(77,P7_WORKER_MS);
  assert(ok==(mode==0||mode==1||mode==8||mode==10));
  assert(ticks<=P7_WORKER_MS);
  if(mode==1)assert(ticks==59000&&interruptions==2);
  if(mode==8)assert(ticks==P7_WORKER_MS);
  if(mode==2||mode==3||mode==9)assert(ticks==P7_WORKER_MS);
  if(mode==4)assert(stop_requested&&polls==1);
 }
 mode=0;ticks=reads=polls=interruptions=stop_requested=0;
 assert(!p7_wait_terminal(77,30000)&&ticks==30000);
 puts("PASS 12 actual wait-loop timing/signal/event models; virtual time only");
 return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path);a=p.parse_args()
 s=(ROOT/'src/bitb_control.inc').read_text();first=s.index('static int p7_wait_terminal(');last=s.index('static int p7_task_states(')
 controller=(ROOT/'src/nxl-allocprobe.c').read_text()
 assert 'alarm(P7_CONTROLLER_SECONDS)' in controller and 'poll(&f,1,P7_WATCHDOG_MS)' in controller
 assert 'p7_wait_terminal(events[0],P7_WORKER_MS)' in s
 # Unknown completion and quarantine decisions remain byte-for-byte from p23.
 with tempfile.TemporaryDirectory() as t:
  t=Path(t);src=t/'model.c';exe=t/'model';src.write_text(PRE+s[first:last]+POST)
  subprocess.run(['gcc','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
 r={'passed':12,'worker_ms':300000,'controller_seconds':420,'watchdog_ms':435000,'unchanged_hardware_worker':False,'hardware_verified':False,'scope':__doc__}
 if a.output:a.output.write_text(json.dumps(r,indent=2)+'\n')
if __name__=='__main__':main()
