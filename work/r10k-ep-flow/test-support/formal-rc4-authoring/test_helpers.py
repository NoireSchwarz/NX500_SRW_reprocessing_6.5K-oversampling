from context import *
import re,shutil
sys.path[:0]=[str(PY),str(SRC/'tools'),str(A.parent/'formal-rc2-authoring')]
from cfunc import function
from post_file_model import PostFileModel
V=W/'tests/helpers';V.mkdir(parents=True,exist_ok=True);U=T/'core/src'
def check(name,code):
 p=V/(name+'.c');p.write_text(code,'utf8',newline='\n');host(name,p,p.with_suffix('.arm'))
 f=V/(name+'-fs');f.mkdir(exist_ok=True);r=PostFileModel(p.with_suffix('.arm'),[],f).run();assert r['exit']==0,(name,r);return r
source=(U/'nxl-lutload.c').read_text('utf8')
source=source.replace(function(source,'camera_ok'),'static int camera_ok(void){return 1;}\n').replace(function(source,'barrier'),'static void barrier(void){}\n')
source=source.replace('int main(int argc,char**argv)','int loader_entry(int argc,char**argv)')
prefix=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <errno.h>
#include "../../build-tree/core/src/lutload_ep_abi.h"
static unsigned regs[1024];static int scenario,opens,closes,maps,unmaps,core_waits,acc_waits,access_seen,log_access_error;
static int t_open(const char*p,int flags,...){(void)flags;if(scenario==8&&!strcmp(p,"/dev/drime5_ep"))return -1;opens++;return 20+opens;}
static int t_close(int f){assert(f>=0);closes++;return 0;}
static int t_flock(int fd,int flags){(void)flags;return scenario==7||(scenario==11&&fd==22)?-1:0;}
static void*t_mmap(void*a,size_t len,int prot,int flags,int fd,off_t at){(void)a;(void)prot;(void)flags;(void)fd;assert(len==4096&&at==0x2082b000);if(scenario==9)return MAP_FAILED;maps++;return regs;}
static int t_munmap(void*a,size_t len){assert(a==regs&&len==4096);unmaps++;return 0;}
static int t_printf(const char*fmt,...){(void)fmt;if(regs[2]&1)log_access_error++;return 0;}
static int t_ioctl(int fd,unsigned long cmd,void*q){(void)fd;
 if(cmd==EP_IOCTL_CORE_INTR_WAIT){struct ep_k_core_intr_wait_info*w=q;assert(w->intr==14&&w->timeout_ms==120);core_waits++;if(scenario==1)return -1;if(scenario==10)regs[1]=0x88;return 0;}
 if(cmd==EP_IOCTL_ACC_INTR_WAIT){struct ep_k_acc_intr_wait_info*w=q;assert(w->intr==7&&w->timeout_ms==150);acc_waits++;assert(core_waits==1&&regs[0]&1&&regs[1]==0x100&&(regs[2]&1)&&!(regs[2]&0x100));access_seen++;if(scenario==2){errno=ETIMEDOUT;return -1;}if(scenario==3)w->jpeg_err=1;return 0;}
 return 0;}
#define open t_open
#define close t_close
#define flock t_flock
#define mmap t_mmap
#define munmap t_munmap
#define ioctl t_ioctl
#define printf t_printf
'''
tests=r'''
static void reset(int n){scenario=n;opens=closes=maps=unmaps=core_waits=acc_waits=access_seen=log_access_error=0;memset(regs,0,sizeof regs);regs[3]=0x81115200;stopping=0;}
static int once(const char*addr){char*a[]={"nxl-lutload","once",(char*)addr,0};return loader_entry(3,a);}
int main(void){
 reset(0);assert(!once("0x81119f00")&&regs[3]==0x81119f00&&access_seen==1&&!(regs[2]&1)&&!log_access_error&&opens==closes&&maps==unmaps);
 reset(0);assert(!once("0x81115200")&&regs[3]==0x81115200);
 reset(0);assert(once("0x2082b000")==2&&!opens&&!maps);
 reset(1);assert(once("0x81119f00")==9&&!access_seen&&!regs[0]&&!regs[2]&&regs[3]==0x81115200&&opens==closes&&maps==unmaps);
 for(int i=2;i<=3;i++){reset(i);assert(once("0x81119f00")==6&&access_seen==1&&!(regs[2]&1)&&!log_access_error&&opens==closes&&maps==unmaps);}
 reset(4);regs[1]=0x22;assert(once("0x81119f00")==5&&!access_seen&&!regs[2]);
 reset(7);assert(once("0x81119f00")==4&&opens==closes&&!maps);
 reset(8);assert(once("0x81119f00")==1&&opens==closes&&!maps);
 reset(9);assert(once("0x81119f00")==1&&opens==closes&&!maps);
 reset(10);assert(once("0x81119f00")==5&&!acc_waits&&!access_seen&&!regs[2]);
 reset(11);assert(once("0x81119f00")==4&&opens==2&&opens==closes&&!maps&&!core_waits&&!acc_waits);
 reset(0);char*c[]={"nxl-lutload","current",0};assert(!loader_entry(2,c)&&regs[3]==0x81115200&&access_seen==1);
 reset(0);regs[3]=0xdeadbeef;assert(loader_entry(2,c)==2&&!access_seen&&!regs[0]);
 reset(0);char*s[]={"nxl-lutload","source",0};assert(!loader_entry(2,s)&&opens==1&&maps==1&&closes==1&&unmaps==1&&!acc_waits&&!core_waits);
 puts("PASS loader: 13 cases, exact IRQ ABI, EOF before DMA, clear access before logs, all resources released");return 0;
}
'''
# The fixture uses the same production header, resolved through core/src.
loader=check('loader-cleanup',prefix+source+tests)

source=(U/'nxl-native-refresh.c').read_text('utf8');readers=(U/'native_refresh_readonly.inc').read_text('utf8')
prefix=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
struct camera{int pid,mem;uint64_t start;uint32_t movie_base,user_base;};
static volatile sig_atomic_t cancelled;static unsigned vp_ntsc,vp_size,vp_quality,vp_raw_preferences[4];
static int scenario,calls;static unsigned current[4];static char kind_seen[20],name_seen[20];static uint64_t time_now;
static int firmware(void){return scenario!=1;}static int camera_pid(void){return 77;}static uint64_t identity(int p){assert(p==77);return 123;}
static int vp_digest(const char*p,const char*h){assert(!strcmp(p,"/usr/bin/st")&&strlen(h)==64);return scenario!=9;}
static int busy(struct camera*c){(void)c;return scenario==2?1:0;}
static int vp_library_context(struct camera*c,const char**why){(void)c;(void)why;memcpy(vp_raw_preferences,current,sizeof current);vp_ntsc=current[0]==0;vp_size=current[1];vp_quality=current[2];return 1;}
static int refresh_lock(const char*p){(void)p;return scenario==3?-1:42;}
static int t_open(const char*p,int flags,...){assert(!strcmp(p,"/proc/77/mem")&&flags==(O_RDONLY|O_CLOEXEC));return 41;}
static int t_close(int f){assert(f==41||f==42);return 0;}
static int t_lstat(const char*p,struct stat*s){(void)s;assert(!strcmp(p,"/tmp/nx-lut-lite/engine.lock"));errno=ENOENT;return scenario==4?0:-1;}
static ssize_t t_pread(int f,void*b,size_t n,off_t at){assert(f==41&&n==4&&(at==0x4ac044||at==0x4ac138));*(unsigned*)b=(at==0x4ac044&&scenario==5)?0:1;return 4;}
static int submit(const char*kind,const char*name){calls++;strcpy(kind_seen,kind);strcpy(name_seen,name);if(scenario==6)return 0;if(!strcmp(kind,"record")&&scenario!=7)current[1]=!strcmp(name,"24f")?0:1;if(scenario==8)current[2]=0;return 1;}
static uint64_t ms(void){return time_now+=200;}
static int t_usleep(unsigned n){(void)n;return 0;}
#define open t_open
#define close t_close
#define lstat t_lstat
#define pread t_pread
#define usleep t_usleep
'''
actual=readers[readers.index('struct photo_spec'):]+function(source,'stop_refresh')+function(source,'native_state')+function(source.replace('int main(','static int main('),'main').replace('static int main(','int refresh_entry(')
tests=r'''
static void reset(int n){scenario=n;calls=0;time_now=0;cancelled=0;current[0]=0;current[1]=1;current[2]=2;current[3]=256;}
static int run(const char*verb){char*a[]={"nxl-native-refresh",(char*)verb,0};return refresh_entry(2,a);}
int main(void){
 reset(0);assert(!run("video24")&&calls==1&&!strcmp(kind_seen,"record")&&!strcmp(name_seen,"24f"));
 reset(0);current[1]=0;assert(!run("video30")&&calls==1&&!strcmp(name_seen,"30f"));
 reset(0);assert(!run("photo")&&calls==1&&!strcmp(kind_seen,"capture")&&!strcmp(name_seen,"28")&&current[1]==1);
 reset(0);assert(!run("snapshot")&&!calls);
 for(int i=1;i<=5;i++){reset(i);assert(run("video24")&&!calls);}
 for(int i=6;i<=8;i++){reset(i);assert(run("video24")&&calls==1);}
 reset(0);current[0]=1;assert(run("video24")&&!calls);
 reset(0);current[2]=0;assert(run("video24")&&!calls);
 reset(0);current[3]=0;assert(run("photo")&&!calls);
 reset(9);assert(run("video24")&&!calls);
 reset(0);assert(run("garbage")==64&&!calls);
 puts("PASS native resolution: fixed commands, same photo spec, busy/version/quality/ack guards; no writes to process RAM");return 0;
}
'''
native=check('native-resolution',prefix+actual+tests)
assert 'ptrace'not in readers and 'PTRACE_'not in source and 'pwrite('not in source+readers
assert '"st","app","nx",kind,"resolution",name' in source
put(W/'helpers-validation.json',dict(passed=True,loader=loader,native_resolution=native,actual_ARM_control_flow=True,hardware_MMIO_IRQ_and_native_dispatch_modeled=True,hardware_tested=False))
print('PASS RC4 helpers',flush=True)
