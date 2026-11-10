from pathlib import Path
import hashlib,json,struct,sys
W=Path(__file__).resolve().parent;R=W/'r8k-ep-idle';P=W/'r8k-ep-idle-full-v1';B=W/'r8j-compare-full-v1';V=R/'validation/current';M='nx-lut-demo/nx-lut-lite/'
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from post_file_model import PostFileModel
from unicorn.arm_const import *
class RuntimeModel(PostFileModel):
 def interrupt(self,uc,number,data):
  n=uc.reg_read(UC_ARM_REG_R7)
  if n==369:
   assert uc.reg_read(UC_ARM_REG_R0)==0 and uc.reg_read(UC_ARM_REG_R1)==7 and not uc.reg_read(UC_ARM_REG_R2)
   uc.mem_write(uc.reg_read(UC_ARM_REG_R3),struct.pack('<QQ',1024,1024));self.calls[n]=self.calls.get(n,0)+1;self.ret(0);return
  if n==6 and uc.reg_read(UC_ARM_REG_R0)not in self.fds:self.calls[n]=self.calls.get(n,0)+1;self.ret(-9);return
  return super().interrupt(uc,number,data)
cases=[]
for name in ['nxl-init-test.arm','nxl-owned-ep.arm','nxl-lens-watch.arm']:
 r=RuntimeModel(P/M/'bin'/name,['--version'],V).run();assert r['exit']==0 and'R8K'in r['stdout'].upper(),(name,r);cases.append(dict(binary=name,**r))
frozen={}
for name in ['nxl-demo-front.arm','nxl-init-pipeline.arm','nxl-demo-post.arm','nxl-hw-controller.arm','nxl-hw-worker.arm','nxl-session.arm','nxl-session-rearm.arm','nxl-oem-restore.arm']:
 p=P/M/'bin'/name;old=B/M/'bin'/name;assert p.read_bytes()==old.read_bytes(),name;frozen[name]=hashlib.sha256(p.read_bytes()).hexdigest()
assert b'R8k SAME PHOTO / ISP COMPARE'in(P/M/'bin/nxl-menu').read_bytes()
(V/'runtime.json').write_text(json.dumps(dict(passed=True,cases=cases,unchanged_pixel_session_and_recovery_binaries=frozen,hardware_tested=False),indent=2)+'\n',encoding='utf8');print('PASS final packaged helper versions; pixel pipeline/session/recovery byte-identical')
