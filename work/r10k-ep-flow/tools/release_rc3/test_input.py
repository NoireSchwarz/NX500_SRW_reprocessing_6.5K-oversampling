from context import *
import shutil
shutil.copyfile(A.parent/'formal-rc2-authoring/cfunc.py',A/'cfunc.py')
from cfunc import function
sys.path[:0]=[str(PY),str(SRC/'tools')]
from post_file_model import PostFileModel
from unicorn.arm_const import UC_ARM_REG_R7,UC_ARM_REG_R0,UC_ARM_REG_R1
class AccessModel(PostFileModel):
 def interrupt(self,uc,number,data):
  if uc.reg_read(UC_ARM_REG_R7)!=33:return super().interrupt(uc,number,data)
  assert uc.reg_read(UC_ARM_REG_R1)==0
  path=self.fixture_path(self.string(uc.reg_read(UC_ARM_REG_R0))).resolve();assert path.is_relative_to(self.fixture.resolve())
  self.calls[33]=self.calls.get(33,0)+1;self.ret(0 if path.exists()else -2)
V=next(W/('tests/input-'+str(i))for i in range(1,100)if not(W/('tests/input-'+str(i))).exists());V.mkdir(parents=True);U=T/'base/nx-lut-lite/src';main=(U/'menu.c').read_text('utf8')
src=r'''#include <assert.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdarg.h>
static int grabbed[2],refs[2]={1,1},native_events,menu_events,calls,ungrabs,closed,fail_take=-1,fail_release=-1,interrupt_once;
static int model_ioctl(int fd,unsigned long request,...){
 va_list ap;va_start(ap,request);int take=va_arg(ap,int);va_end(ap);assert(request==0x40044590UL);assert(fd==10||fd==11);int i=fd-10;calls++;
 if(interrupt_once){interrupt_once=0;errno=EINTR;return -1;}
 if((take&&i==fail_take)||(!take&&i==fail_release)){errno=EBUSY;return -1;}
 if(take){assert(!grabbed[i]);grabbed[i]=1;}else{assert(grabbed[i]);grabbed[i]=0;ungrabs++;}return 0;
}
static int model_close(int fd){assert(fd==10||fd==11);closed++;if(--refs[fd-10]==0)grabbed[fd-10]=0;return 0;}
#define ioctl model_ioctl
#define close model_close
#include "../../build-tree/base/nx-lut-lite/src/menu_input_lease.h"
static int menu_input_fd[2]={10,11};static struct nxl_input_lease menu_input_lease={{-1,-1},0};
'''+function(main,'menu_input_child_close')+r'''
static void wheel(int i){menu_events++;if(!grabbed[i])native_events++;}
int main(void){
 int fds[2]={10,11},missing[2]={10,-1};assert(!nxl_input_lease_take(&menu_input_lease,missing)&&calls==0);
 fail_take=1;assert(!nxl_input_lease_take(&menu_input_lease,fds));assert(!menu_input_lease.mask&&!grabbed[0]&&!grabbed[1]&&ungrabs==1);fail_take=-1;
 interrupt_once=1;assert(nxl_input_lease_take(&menu_input_lease,fds));int prior=calls;
 assert(nxl_input_lease_take(&menu_input_lease,fds)&&calls==prior);
 for(int i=0;i<120;i++)wheel(i%2);assert(menu_events==120&&native_events==0);
 /* An actual child-close function closes duplicates, without UNGRAB. */
 struct nxl_input_lease parent=menu_input_lease;refs[0]=refs[1]=2;prior=ungrabs;menu_input_child_close();
 assert(ungrabs==prior&&grabbed[0]&&grabbed[1]&&refs[0]==1&&refs[1]==1);menu_input_lease=parent;menu_input_fd[0]=10;menu_input_fd[1]=11;
 assert(nxl_input_lease_release(&menu_input_lease));assert(!grabbed[0]&&!grabbed[1]);wheel(0);wheel(1);assert(native_events==2);
 assert(nxl_input_lease_take(&menu_input_lease,fds));fail_release=0;assert(!nxl_input_lease_release(&menu_input_lease));assert(!grabbed[0]&&!grabbed[1]&&closed==3&&!menu_input_lease.mask);
 puts("PASS modal input: 120 wheel events isolated, partial admission rollback, child duplicates, return/release and close fallback");return 0;
}
'''
p=V/'lease.c';p.write_text(src,'utf8',newline='\n');host('input-lease',p,V/'lease.arm')
fs=V/'fs';fs.mkdir();r=PostFileModel(V/'lease.arm',[],fs).run();assert r['exit']==0,r
prefix=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int video_enabled,active_strength=65;static char active_name[81]="stale";
'''+function(main,'name_ok')+'''static int strength_ok(int n){return n>=0&&n<=100&&n%5==0;}
'''+function(main,'refresh_lut_runtime')+r'''
int main(int argc,char**argv){assert(argc==2);refresh_lut_runtime();
 switch(argv[1][0]){
 case '0':assert(!video_enabled&&!active_name[0]&&active_strength==100);break;
 case '1':assert(video_enabled&&!strcmp(active_name,"NXLOG_REF_V1")&&active_strength==0);break;
 case '2':assert(!video_enabled&&!strcmp(active_name,"fuji_400h")&&active_strength==65);break;
 case '3':assert(!video_enabled&&!active_name[0]&&active_strength==100);break;
 }puts("PASS committed LUT state refresh");return 0;}
'''
p=V/'refresh.c';p.write_text(prefix,'utf8',newline='\n');host('input-refresh',p,V/'refresh.arm');results=[]
for i,files in enumerate([{},dict(current='NXLOG_REF_V1\n',strength='0\n',video_enabled=''),dict(current='fuji_400h\n',strength='65\n'),dict(current='bad/name\n',strength='103\n')]):
 fs=V/('state-'+str(i));root=fs/'tmp/nx-lut-lite/session';root.mkdir(parents=True)
 for name,value in files.items():(root/name).write_text(value,'ascii',newline='\n')
 result=AccessModel(V/'refresh.arm',[str(i)],fs).run();assert result['exit']==0,(i,result);results.append(result)
put(W/'input-validation.json',dict(passed=True,lease=r,runtime_refresh=results,actual_ARM_production_functions=True,
 kernel_ioctl_and_evdev_delivery_modeled=True,source_header_sha256=sha(U/'menu_input_lease.h'),hardware_tested=False))
print('PASS input isolation and 4 runtime-cache states',flush=True)
