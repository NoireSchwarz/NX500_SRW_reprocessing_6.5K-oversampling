"""Execute final ARM collector with modeled pending/completion transitions.
Device memory is modeled; positive test hits are not camera observations.
"""
from pathlib import Path
import sys,struct,json,hashlib
W=Path(__file__).resolve().parent;sys.path.insert(0,str(W))
import test_r10j_path_probe as p
from unicorn import UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
from analyze_video_counters import decode
class Hardware(p.NativeRAM):
 def __init__(self,*a,scenario='hardware',**kw):
  self.scenario=scenario;self.short_waits=0;self.total_followups=0;self.status_reads=[];self.target_writes=[]
  super().__init__(*a,**kw)
  self.uc.hook_add(UC_HOOK_MEM_READ,self.audit_read)
  self.uc.hook_add(UC_HOOK_MEM_WRITE,self.audit_write)
 def audit_read(self,u,kind,at,n,value,data):
  for v,m in self.maps.items():
   if m['path']=='/dev/mem'and v<=at<v+m['length']:
    a=m['off']+at-v
    assert not(a<0x20810444 and a+n>0x20810440),'ACK read'
    assert not 0x06002060<=a<0x06002068,'shared native timer read'
    if a in [0x20810418,0x20810468]:self.status_reads.append(a)
 def audit_write(self,u,kind,at,n,value,data):
  for v,m in self.maps.items():
   if m['path']=='/dev/mem'and v<=at<v+m['length']:self.target_writes.append((hex(m['off']+at-v),value))
 def ram_words(self):
  w=super().ram_words();n=self.sleeps*6;flags=3|(1<<16);request=100000+n*1170000
  if self.scenario=='delivered'and self.short_waits:flags|=4
  if self.scenario=='wrong-controller':flags|=1<<24
  for i,v in enumerate([0x33544c56,3,n,flags,request,request+100,request+1120600 if flags&4 else 0,0]):w[0x817dbbe0+4*i]=v
  w[0x20810418]=0 if self.scenario=='hardware'else 4
  w[0x20810468]=0 if self.scenario=='disabled'else 4;w[0x813f09bc]=0x801cdf6c
  return w
 def event(self):self.short_waits=0;super().event()
 def interrupt(self,u,num,data):
  if u.reg_read(UC_ARM_REG_R7)==265 and not u.reg_read(UC_ARM_REG_R1):
   sec,ns=struct.unpack('<ii',u.mem_read(u.reg_read(UC_ARM_REG_R2),8))
   if(sec,ns)==(0,1000000):
    self.short_waits+=1;self.total_followups+=1;assert self.short_waits<=2
    self.wall+=ns;self.calls[265]=self.calls.get(265,0)+1;self.populate();self.ret(0);return
  return super().interrupt(u,num,data)
rows=[]
for case in ['hardware','pending','delivered','disabled','wrong-controller']:
 f=p.base.fixture();m=Hardware(f,['--run',p.base.REPORT],scenario=case,after=12);out=m.run();assert out['exit']==0,out
 s,intervals=decode(f/(p.base.REPORT+'/counts.bin').lstrip('/'));c=s['hardware_completion'];assert c['samples']==12
 if case=='hardware':assert all(r['classification']=='completion-not-observed-pending'for r in c['records'])and not m.total_followups
 if case=='pending':assert all(r['classification']=='completion-latched-before-tracked-IRQ'and r['observed_pending_lower_ms']>=2 for r in c['records'])and m.total_followups==24
 if case=='delivered':assert all(r['classification']=='completion-latched-before-tracked-IRQ'and not r['observed_pending_lower_ms']and r['context_changed_during_followup']for r in c['records'])and m.total_followups==12
 if case=='disabled':assert all(r['classification']=='completion-latched-IRQ-disabled'for r in c['records'])and not m.total_followups
 if case=='wrong-controller':assert all(r['classification']=='outside-qualified-hardware-request'for r in c['records'])and not m.total_followups
 assert not m.hot_sd and not m.target_writes and 0x20810418 in m.status_reads and 0x20810468 in m.status_reads
 rows.append(dict(case=case,passed=True,samples=12,completion=c,followups=m.total_followups,target_writes=m.target_writes,MMIO_read_count=len(m.status_reads),hot_SD_writes=m.hot_sd))
 print('PASS final ARM collector hardware-status',case,flush=True)
report=dict(passed=True,cases=rows,binary_sha256=hashlib.sha256(p.base.BIN.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((p.R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),actual_ARM=True,hardware_tested=False,limits=__doc__)
(p.R/'validation/current/completion-production.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
