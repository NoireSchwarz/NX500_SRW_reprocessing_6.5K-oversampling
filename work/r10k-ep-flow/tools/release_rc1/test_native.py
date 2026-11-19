from context import *
import re,shutil,struct
sys.path[:0]=[str(PY),str(SRC/'tools')]
from arm_model import ArmModel
from post_file_model import PostFileModel
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R7
V=next(W/('tests/native-'+str(i))for i in range(1,100)if not(W/('tests/native-'+str(i))).exists());V.mkdir(parents=True)
class Guard(PostFileModel):
 def __init__(self,mode):
  self.mode=mode;self.locks=[];self.handoff=False;self.opens=[]
  f=V/('guard-'+str(mode));(f/'tmp/nx-lut-lite').mkdir(parents=True);(f/'mnt/mmc/NXDEMO51').mkdir(parents=True)
  super().__init__(M/'bin/nxl-lutguard.arm',['/candidate/lut-core.sh','detail','a','100'],f)
 def interrupt(self,u,n,d):
  nr=u.reg_read(UC_ARM_REG_R7);a=[u.reg_read(r)for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2]]
  if nr==5:self.opens.append((self.string(a[0]),a[1]));assert not(a[1]&0x80000)
  if nr==143:
   self.locks.append(a[0]);assert a[1]==6
   self.ret(-11 if self.mode==len(self.locks)else 0);return
  if nr==11:
   assert self.string(a[0])=='/bin/sh';args=[]
   for i in range(8):
    at=struct.unpack('<I',u.mem_read(a[1]+4*i,4))[0]
    if not at:break
    args.append(self.string(at))
   assert args==['sh','/candidate/lut-core.sh','detail','a','100'];assert len(self.locks)==1
   self.handoff=True;self.exit_code=0;u.emu_stop();return
  return super().interrupt(u,n,d)
guards=[]
for mode in [0,1,2]:
 m=Guard(mode);r=m.run();assert r['exit']==(75 if mode==1 else 0),(mode,r)
 assert m.handoff==(mode!=1);assert[m[0]for m in m.opens]==['/tmp/nx-lut-lite/video-mode.fd']
 guards.append(dict(case=mode,result=r,handoff=m.handoff,locks=m.locks))
assert ArmModel(M/'bin/nxl-lutguard.arm',['relative']).run()['exit']==64
# Actual production read-only helper refuses an unverified camera before any
# process memory or device access. The ISP and kernel are not modeled here.
f=V/'idle';(f/'etc').mkdir(parents=True);(f/'etc/version.info').write_text('1.00\nOTHER\n','ascii')
idle=PostFileModel(M/'bin/nxl-lutidle.arm',[],f).run();assert idle['exit']==1 and 'ready=0'in idle['stdout']
state=PostFileModel(M/'bin/nxl-lutidle.arm',['--gamma-state'],f).run();assert state['exit']==1 and 'phase='in state['stdout']
assert not(f/'mnt').exists()and not(f/'dev').exists()
# The actual capped progress routine executes 100 times, including real SHA
# digests and last-record recovery. File/lock syscalls are modeled locally.
src=V/'progress.c';src.write_text(r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../../build-tree/session/src/sha256.h"
#define NP_MAX_RECORDS 32u
#include "../../build-tree/common/progress_journal.h"
int main(void){const char*p="/progress.events";struct np_record r;struct stat s;
 for(int i=1;i<=100;i++){assert(np_publish(p,"PIPE_SRW","RUNNING","model-only"));assert(np_last(p,&r)&&r.sequence==(unsigned)i);assert(!stat(p,&s)&&s.st_size<=65536);}
 assert(!np_publish(p,"INVALID PHASE","RUNNING","x"));puts("PASS actual SHA progress records: 100 events, <=32 records, monotonic sequence");return 0;}
''','utf8',newline='\n');host('progress-cap',src,V/'progress.arm')
class CapModel(PostFileModel):
 def interrupt(self,u,n,d):
  nr=u.reg_read(UC_ARM_REG_R7);fd=u.reg_read(UC_ARM_REG_R0);length=u.reg_read(UC_ARM_REG_R1)
  if nr==143:assert length==2;self.ret(0);return
  if nr==194:length=u.reg_read(UC_ARM_REG_R2)
  if nr in [93,194]:
   f=self.fds[fd];q=f['host'];q.write_bytes(q.read_bytes()[:length]);f['data']=q.read_bytes();f['at']=min(f['at'],length);self.ret(0);return
  if nr in [4,146]and fd in self.fds and self.fds[fd].get('flags',0)&0x400:
   self.fds[fd]['at']=self.fds[fd]['host'].stat().st_size
  return super().interrupt(u,n,d)
f=V/'progress';f.mkdir();progress=CapModel(V/'progress.arm',[],f).run();assert progress['exit']==0,progress
assert(f/'progress.events').stat().st_size==4*2048
# Real session status on its unchanged journal authority with the new fixed
# diagnostic leaf. No mutating/native command is admitted by this fixture.
f=V/'session';(f/'mnt/mmc/NXDEMO51/logs').mkdir(parents=True)
(f/'mnt/mmc/NXDEMO51/logs/session.log').write_bytes(b'OLD_HISTORY')
session=PostFileModel(M/'bin/nxl-session.arm',['status'],f).run();assert session['exit']==0 and 'state=NEED_STILL_INIT'in session['stdout'],session
assert(f/'mnt/mmc/NXDEMO51/logs/session.log').read_bytes()==b'OLD_HISTORY'
put(W/'native-aux-validation.json',dict(passed=True,guard=guards,guard_order_and_inherited_fds=True,
 SRW_supervisor_run_lock_not_reacquired=True,idle_rejects_unverified_camera=idle,gamma_status_rejects_unverified_camera=state,progress=progress,session=session,
 progress_capped_32_records=True,old_session_log_preserved=True,actual_ARM=True,OS_files_locks_modeled=True,hardware_tested=False))
print('PASS actual ARM guard locks/handoff, read-only rejection, progress cap and production session status',flush=True)
