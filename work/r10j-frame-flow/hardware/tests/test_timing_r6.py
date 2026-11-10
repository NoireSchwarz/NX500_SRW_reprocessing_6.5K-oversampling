#!/usr/bin/env python3
"""Exercise diagnostic clock fallback and both actual ARM libc clock ABIs."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile
R=Path(__file__).resolve().parents[1]; W=R.parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
HOST=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int mode,call;
static int mocked_clock(clockid_t id,struct timespec *t){
 assert(id==CLOCK_MONOTONIC);unsigned n=call++;
 if((mode==1&&!n)||(mode==2&&n))return -1;
 t->tv_sec=mode==3&&n?9:10;t->tv_nsec=mode==4&&!n?1000000000:mode==5&&n?-1:100+(long)n;
 return 0;
}
#define clock_gettime mocked_clock
#include "r6_timing.h"
int main(void){
 for(mode=0;mode<6;mode++){
  call=0;FILE *f=fopen("clock-test.log","w+");assert(f);r6_timing_set(f);r6_timing("CLOCK_TEST","EVENT",19,1);rewind(f);
  char b[500];assert(fgets(b,sizeof b,f));
  assert(strstr(b,mode?"relative_ns=0 clock_available=0":"relative_ns=1 clock_available=1"));
  assert(strstr(b,"amount=19 ok=1"));fclose(f);
 }
 r6_timing_set(NULL);int before=call;r6_timing("NO_STREAM","EVENT",0,0);assert(call==before);
 puts("PASS7 clock diagnostics: available, origin failure, current failure, backwards, invalid origin/current nsec, absent stream");return 0;
}
'''
ARM=r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "r6_timing.h"
int main(void){
 printf("CLOCK_ABI public_timespec=%u kernel_timespec=8\n",(unsigned)sizeof(struct timespec));
 r6_timing_set(stdout);if(!r6_timing_origin)return 1;
 r6_timing("ARM_ABI","BEGIN",0,1);usleep(10000);r6_timing("ARM_ABI","END",0,1);
 return r6_timing_ns()<=r6_timing_origin;
}
'''
def main():
 z=W/'wb-load-p42/deps/ziglang/zig';lib=z.parent/'lib';cross=W/'wb-load-p42/cross-runtime/arm-linux-musleabi-cross';sysroot=cross/'arm-linux-musleabi';qemu=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';camera=W/'demo-p49/research/backend-reuse/camera-root'
 results=[]
 with tempfile.TemporaryDirectory(prefix='r6-clock-',dir=W) as td:
  t=Path(td);env={**os.environ,'TMPDIR':str(t),'ZIG_GLOBAL_CACHE_DIR':str(t/'zig-cache')};host=t/'host.c';host.write_text(HOST);exe=t/'host';subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(R/'src'),str(host),'-o',str(exe)],check=True,env=env)
  p=subprocess.run([str(exe)],check=True,capture_output=True,text=True,cwd=t);results.append({'name':'host clock failure diagnostics','tests_run':7,'stdout':p.stdout.strip()})
  worker=(R/'src/nxl-bitbworker.c').read_text();fragment=worker[worker.index('timing_file=fopen(path,"w")'):worker.index('if(!full_open_inputs(argv[3]))')]
  assert 'goto' not in fragment and 'return' not in fragment and 'r6_timing_set(timing_file)' in fragment
  results.append({'name':'optional worker timing log failure has no failure branch','tests_run':1,'actual_source_fragment':fragment.strip()})
  c=t/'arm.c';c.write_text(ARM);base=[str(z),'clang','-target','arm-linux-gnueabi','-mcpu=cortex-a9','-mfloat-abi=soft','-marm','-nostdinc','-O2','-fno-pie','-fno-pic','-fno-stack-protector','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(R/'src')]
  for abi in ['original-glibc-worker','musl-static-controller']:
   obj=t/(abi+'.o');out=t/(abi+'.arm');cmd=base.copy()
   includes=['include','libc/include/arm-linux-gnueabi','libc/include/generic-glibc','libc/include/arm-linux-any','libc/include/any-linux-any'] if abi.startswith('original') else ['include']
   for inc in includes:cmd+=['-I',str(lib/inc)]
   if abi.startswith('musl'):cmd+=['-I',str(sysroot/'include')]
   subprocess.run(cmd+['-c',str(c),'-o',str(obj)],check=True,env=env)
   link=[str(z),'ld.lld','-m','armelf_linux_eabi','-e','_start','-z','noexecstack','--build-id=none','--strip-all','-o',str(out)]
   if abi.startswith('original'):
    start=t/'start.o';subprocess.run(cmd+['-c',str(R/'src/bitb_start.S'),'-o',str(start)],check=True,env=env)
    subprocess.run(link+['-dynamic-linker','/lib/ld-linux.so.3',str(start),str(obj),str(camera/'lib/libc-2.13.so')],check=True,env=env)
    run=[str(qemu),'-L',str(camera),str(out)]
   else:
    rt=sysroot/'lib';gcc=next((cross/'lib/gcc/arm-linux-musleabi').glob('*/libgcc.a'))
    subprocess.run(link+['-static',str(rt/'crt1.o'),str(rt/'crti.o'),str(obj),'--start-group',str(rt/'libc.a'),str(gcc),'--end-group',str(rt/'crtn.o')],check=True,env=env)
    run=[str(qemu),str(out)]
   p=subprocess.run(run,check=True,capture_output=True,text=True);lines=p.stdout.splitlines();rows=[dict(re.findall(r'(\w+)=(\w+)',s)) for s in lines if s.startswith('R6_TIMING ')]
   assert len(rows)==2 and all(x['clock_available']=='1' for x in rows)
   ns=[int(x['monotonic_ns']) for x in rows];rel=[int(x['relative_ns']) for x in rows]
   assert ns[1]>ns[0]>0 and rel[1]>rel[0]>=0 and 1000000<rel[1]-rel[0]<5000000000
   results.append({'name':abi,'tests_run':1,'binary_sha256':sha(out),'stdout':p.stdout.strip()})
 out={'format':'NX51_R6_TIMING_DIAGNOSTICS1','passed':True,'hardware_tested':False,'tests_run':10,'source_sha256':{'hardware/src/r6_timing.h':sha(R/'src/r6_timing.h'),'hardware/src/nxl-bitbworker.c':sha(R/'src/nxl-bitbworker.c'),'hardware/tests/test_timing_r6.py':sha(Path(__file__))},'ARM_kernel_abi':'clock_gettime32 syscall263, explicit int32 sec/nsec, 8 bytes; independent of public libc timespec','cases':results,'limits':['Actual ARM instruction execution under QEMU, not camera hardware or clock performance measurement.']}
 (R/'validation/r6-timing-tests.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
if __name__=='__main__':main()
