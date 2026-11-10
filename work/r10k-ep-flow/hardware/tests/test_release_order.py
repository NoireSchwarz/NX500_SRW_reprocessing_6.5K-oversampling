#!/usr/bin/env python3
"""Execute the actual controller release fragment against async scheduling models.
No camera, native API, process control or physical allocator is exercised.
The worker-dependent completion model is a hypothesis, not a causal finding.
"""
from pathlib import Path
import argparse, json, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "alloc_core.h"
#include "memory_core.h"
static unsigned mode,ticks,attached,release_calls,detaches,return_verified,
 idle_registers_verified,free_after,index_owned,cap_base,shell_base,initial_ui=12;
static int stop_requested,queue;
static uint32_t response[5],meta[4],f;
static const char *trace_phase,*reason;
static struct alloc_regs regs,call_original;
static FILE *log_file;
static int invoke(uint32_t fn,uint32_t arg,unsigned timeout){
 (void)fn;(void)arg;(void)timeout;release_calls++;assert(release_calls==1);return 1;
}
static int get_regs(struct alloc_regs *r){r->r[0]=mode==3?1:0;return 1;}
static int save_regs(const char *d,const char *s,const struct alloc_regs *r){(void)d;(void)s;(void)r;return 1;}
static int read_word(uint32_t p,uint32_t *out){(void)p;*out=initial_ui;return 1;}
static int set_regs(const struct alloc_regs *r){(void)r;return 1;}
static int cleanup_trace(void){if(mode==2)return 0;attached=0;detaches++;return 1;}
static int stabilize_worker(const char *d,int q){(void)d;(void)q;return mode!=7;}
static void event(const char *s){(void)s;}
static void pause_ms(unsigned n){ticks+=n;}
static void row(const char *s,uint32_t i){(void)s;(void)i;}
static int free_now(void){return mode==5 || (!attached&&mode!=1&&ticks>=50);}
static int pool_head(uint32_t *out){*out=free_now()?26396:26388;return mode!=4;}
static int entry(uint32_t i,uint32_t words[4]){
 (void)i;words[0]=free_now()?0:0x23;words[1]=free_now()&&mode!=6?0:8;return 1;
}
#define REQUIRE(c,msg) do{if(!(c)){reason=msg;goto finish;}}while(0)
#define REQUIRE_TRACE(c,msg) REQUIRE(c,msg)
static unsigned release_done;
static int execute(unsigned test_mode){
 mode=test_mode;ticks=0;attached=1;release_calls=detaches=return_verified=idle_registers_verified=release_done=0;
 const char *dir="model";
'''
POST=r'''
 return 1;
finish:return 0;
}
int main(void){log_file=fopen("release-model.log","w");assert(log_file);
 for(unsigned m=0;m<8;m++){
  int ok=execute(m);assert(release_calls==1);
  assert(ok==(m==0||m==5));
  if(ok)assert(!attached&&detaches==1&&return_verified&&idle_registers_verified);
  if(m==1||m==4||m==6)assert(!return_verified);
  if(m==2||m==3)assert(!detaches&&!return_verified);
 }
 fclose(log_file);puts("PASS 8 actual controller-fragment async release models; no hardware");return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path);a=p.parse_args()
 s=(ROOT/'src/nxl-allocprobe.c').read_text()
 start=s.index(' trace_phase="release_return";event("RELEASE_BEGIN")')
 end=s.index(' reason=!ep_contract_verified',start)
 fragment=s[start:end]
 assert fragment.index('REQUIRE(regs.r[0]==0')<fragment.index('REQUIRE(cleanup_trace()')<fragment.index('row("release_wait"')
 with tempfile.TemporaryDirectory() as tmp:
  src=Path(tmp)/'release.c';exe=Path(tmp)/'release'
  src.write_text(PRE+fragment+POST)
  subprocess.run(['gcc','-std=c11','-fsanitize=undefined','-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,cwd=tmp)
 result={'passed':8,'scope':__doc__.strip(),'cases':['worker-dependent completion','never completes','detach failure','release API error','metadata unreadable','already freed','malformed free span','worker idle verification failure']}
 if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
