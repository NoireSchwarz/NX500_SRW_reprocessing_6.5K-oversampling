#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json
R=Path(__file__).resolve().parents[1]
s=(R/'src/nxl-allocprobe.c').read_text();start=s.index(' if(p7_quarantined){',s.index('static int cleanup_trace'))
end=s.index(' if(attached&&!trace_stopped)',start);fragment=s[start:end]
assert s.index('controller_report(dir,rc,"UNKNOWN_REQUIRE_RESTART")',s.index('finish:\n alarm(0)'))<s.index('if(!cleanup_trace()',s.index('finish:\n alarm(0)'))
assert "c=='D'||c=='P'" in s
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
static int p7_quarantined=1,pid=247,tid=259,attached,trace_stopped,detached_ok,call_boundary_active,watch_pipe,watch_pid,code_restored,mode,waits,stops,parked;
static uint64_t clock_ns;
static struct {int armed;} patches[2];
static int restore_patch(unsigned i){patches[i].armed=0;return 1;}
static uint64_t ns(void){return clock_ns;}
static void pause_ms(unsigned n){clock_ns+=(uint64_t)n*1000000;}
static int model_kill(int p,int sig){assert(p==247&&sig==SIGSTOP);stops++;return 0;}
static int model_ptrace(int op,int t,void *a,void *sig){(void)a;assert(op==PTRACE_DETACH&&t==259&&(uintptr_t)sig==SIGSTOP);return 0;}
static int model_write(int fd,const void *p,unsigned n){assert(fd==5&&n==1&&*(const char*)p=='P');parked++;return 1;}
static int model_close(int fd){assert(fd==5);return 0;}
static int model_waitpid(int p,int *status,int options){assert(p==77&&options==WNOHANG);waits++;*status=0;if(mode==1)return 0;if(mode==2&&waits==1){errno=EINTR;return -1;}return 77;}
#define kill model_kill
#define ptrace model_ptrace
#define write model_write
#define close model_close
#define waitpid model_waitpid
static int execute(void){int ok=1;
'''
post=r'''
return 0;}
int main(void){for(mode=0;mode<3;mode++){
 attached=trace_stopped=call_boundary_active=1;detached_ok=code_restored=0;watch_pipe=5;watch_pid=77;patches[0].armed=patches[1].armed=1;clock_ns=0;waits=stops=parked=0;
 assert(execute()==(mode!=1));assert(stops==1&&parked==1&&detached_ok&&code_restored&&!attached&&watch_pipe==-1&&watch_pid==-1);assert(clock_ns<=1000000000ULL);
}return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'model.c').write_text(pre+fragment+post);subprocess.run(['cc','-O2',str(p/'model.c'),'-o',str(p/'model')],check=True);subprocess.run([str(p/'model')],check=True)
(R/'validation/quarantine-tests.json').write_text(json.dumps(dict(passed=3,hardware_tested=False,actual_controller_fragment=True,no_SIGCONT=True,detach_SIGSTOP=True,guardian_terminal='P',join_bounded_ms=1000,unknown_checkpoint_precedes_cleanup=True),indent=2)+'\n')
print('PASS 3 actual quarantinecleanup fragment models; noSIGCONT, SIGSTOPdetach, guardianP, boundedjoin, durablecheckpointordering')
