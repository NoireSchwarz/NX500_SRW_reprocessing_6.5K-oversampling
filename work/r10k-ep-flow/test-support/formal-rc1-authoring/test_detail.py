from context import *
from cfunc import function
import struct
sys.path[:0]=[str(PY),str(SRC/'tools')]
from post_file_model import PostFileModel
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R7
V=next(W/('tests/detail-'+str(i))for i in range(1,100)if not(W/('tests/detail-'+str(i))).exists());V.mkdir(parents=True)
U=T/'base/nx-lut-lite/src';s=(U/'menu_lut_detail_v2.inc').read_text('utf8')
prefix=r'''#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
struct{int prepared;}detail;
static int detail_visible=1,detail_fd=90,detail_eof,detail_done,detail_status,detail_overflow,detail_read_error,detail_dirty,detail_return,detail_running_mix;
static pid_t detail_child=77;static int dpy,win,detail_dialog,detail_hidden,detail_pa[4]={75},active_strength,closed,started;
static char detail_output[4096],detail_expected[400]="RECEIPT_EXACT\n",detail_a[81]="A",active_name[81],detail_message[160];static size_t detail_used;static unsigned long detail_due,detail_message_until;
#define RevertToPointerRoot 0
#define CurrentTime 0
static void XMapRaised(int d,int w){(void)d;(void)w;}
static void XSetInputFocus(int d,int w,int r,int t){(void)d;(void)w;(void)r;(void)t;}
static unsigned long ticks(void){return 0;}
static void detail_start(void){started++;}
static void detail_close(int where){closed=where;detail_visible=0;}
static int detail_cache_save(void){return 1;}
'''
src=V/'async.c';src.write_text(prefix+function(s,'detail_drain')+function(s,'detail_poll')+r'''
int main(int argc,char**argv){assert(argc==2);int mode=argv[1][0]-'0';
 int r=detail_poll();if(mode==5){assert(!r&&detail_child==77&&!active_name[0]);r=detail_poll();}
 assert(r&&detail_child==0);if(mode==0||mode==5)assert(!strcmp(active_name,"A")&&active_strength==75);else assert(!active_name[0]&&strstr(detail_message,"應用失敗"));
 puts("PASS exact child, EOF, nonblocking drain, no false receipt");return 0;}
''','utf8',newline='\n');host('detail-async',src,V/'async.arm')
class Async(PostFileModel):
 def __init__(self,mode,f):self.mode=mode;self.waits=0;self.at=0;super().__init__(V/'async.arm',[str(mode)],f)
 def interrupt(self,u,n,d):
  nr=u.reg_read(UC_ARM_REG_R7);a=[u.reg_read(r)for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]]
  if nr==114:
   self.waits+=1
   if self.mode==5 and self.waits==1:self.ret(0)
   else:u.mem_write(a[1],struct.pack('<I',256 if self.mode==1 else 0));self.ret(77)
   return
  if nr==3 and a[0]==90:
   if not self.waits:self.ret(-11);return
   b={0:b'RECEIPT_EXACT\n',1:b'RECEIPT_EXACT\n',2:b'RECEIPT_OTHER\n',3:b'x'*5000+b'RECEIPT_EXACT\n',4:b'RECEIPT_EXACT\n',5:b'RECEIPT_EXACT\n'}[self.mode]
   if self.at<len(b):x=b[self.at:self.at+a[2]];u.mem_write(a[1],x);self.at+=len(x);self.ret(len(x))
   else:self.ret(-5 if self.mode==4 else 0)
   return
  if nr==6 and a[0]==90:self.ret(0);return
  return super().interrupt(u,n,d)
asyncs=[]
for i in range(6):
 f=V/('async-'+str(i));(f/'tmp/nx-lut-lite').mkdir(parents=True);r=Async(i,f).run();assert r['exit']==0,r;asyncs.append(r)
prefix=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../../build-tree/base/nx-lut-lite/src/lut_detail_flow.h"
#include "../../build-tree/base/nx-lut-lite/src/look_info.h"
#define srw_lstat lstat
#define srw_fstat fstat
static struct ld_flow detail;static int detail_log_scope,detail_dirty,total_count=3,detail_view[3]={0,1,2};
static char names[3][81]={"ejay_5207_n","ejay_5207_n_fx","vlog_ejay_5207_n"},detail_committed_b[81],detail_message[160];
static int detail_committed_params[4];static void detail_queue(int m){(void)m;detail_dirty=1;}
static int effective_strength(void){return 100;}
static int name_ok(const char*s){return strlen(s)>0&&strlen(s)<=80&&strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==strlen(s);}
'''
actual=function((U/'browser.h').read_text('utf8'),'info_for')+function(s,'detail_name')+function(s,'detail_params_valid')+(A/'detail_cache.inc').read_text('utf8')
actual=actual.replace('if(n<=0)return 0;line[n]=0;','if(n!=st.st_size)return 0;line[n]=0;')
src=V/'cache.c';src.write_text(prefix+actual+r'''
int main(void){ld_begin(&detail,2,0,100,0);assert(!detail_load_parameters());
 detail.a[0]=65;detail.a[1]=3;detail.a[2]=-2;detail.a[3]=7;detail.b[0]=80;detail.prepared=1;memcpy(detail_committed_params,detail.b,sizeof detail.b);strcpy(detail_committed_b,names[1]);detail.b[0]=25;assert(detail_cache_save());
 memset(detail.a,0,sizeof detail.a);assert(detail_load_parameters()&&detail.a[0]==65&&detail.a[1]==3&&detail.prepared&&detail.b[0]==80);
 detail_switch_a(1);assert(!detail.prepared&&detail.a[0]==100&&detail_dirty);detail_switch_a(0);assert(detail.a[0]==65&&detail.prepared&&!detail_dirty);
 /* Cached values outside the five-percent UI grid never become actions. */
 detail.a[0]=3;assert(detail_cache_save());assert(!detail_load_parameters());detail.a[0]=65;
 detail_log_scope=1;detail.count=3;detail.cursor=2;detail.prepared=1;strcpy(detail_committed_b,names[2]);assert(detail_cache_save());assert(detail_load_parameters()&&!detail.prepared&&detail.b[0]==0);
 puts("PASS real per-LUT RAM cache, same-boot revisit, separate Log scope, invalid strength rejected");return 0;}
''','utf8',newline='\n');host('detail-cache',src,V/'cache.arm')
class Cache(PostFileModel):
 def interrupt(self,u,n,d):
  nr=u.reg_read(UC_ARM_REG_R7)
  if nr==194:
   fd=u.reg_read(UC_ARM_REG_R0);length=u.reg_read(UC_ARM_REG_R2);f=self.fds[fd];f['host'].write_bytes(f['host'].read_bytes()[:length]);f['data']=f['host'].read_bytes();f['at']=min(f['at'],length);self.ret(0);return
  return super().interrupt(u,n,d)
f=V/'cache';(f/'tmp/nx-lut-lite').mkdir(parents=True);cache=Cache(V/'cache.arm',[],f).run();assert cache['exit']==0,cache
put(W/'detail-async-cache-validation.json',dict(passed=True,actual_C_async_cases=asyncs,cache=cache,
 actual_ARM=True,filesystem_child_and_X11_endpoints_modeled=True,hardware_tested=False))
print('PASS actual ARM asynchronous receipt and per-LUT cache checks',flush=True)
