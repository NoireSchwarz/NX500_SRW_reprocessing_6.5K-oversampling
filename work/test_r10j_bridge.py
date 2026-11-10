"""Production ARM single-word bridge transaction and recovery under modeled Linux/devices.
These tests verify code paths, not camera contention or physical performance.
"""
from pathlib import Path
import sys,struct,json,hashlib,stat
W=Path(__file__).resolve().parent;sys.path.insert(0,str(W))
import test_r10j_path_probe as p
from unicorn.arm_const import *
from unicorn import UC_HOOK_MEM_WRITE
from analyze_video_counters import decode
from video_hardware_progress import decode_qos_compare
from video_completion import fnv
DEVICE='/dev/mem';STATE='/dev/test-bridge-state'
class Bridge(p.NativeRAM):
 def __init__(self,f,*a,commands=None,initial=0,store_fail='',race='',available=True,**kw):
  self.bridge=int((f/STATE.lstrip('/')).read_text())if(f/STATE.lstrip('/')).exists()else initial
  self.commands=commands or {};self.store_fail=store_fail;self.race=race;self.store_log=[];self.hardware_writes=[];self.displaced=[];self.available=available;self.readback_once=False
  super().__init__(f,*a,**kw)
 def audit_write(self,u,kind,at,n,value,data):
  physical=None
  for base,m in self.maps.items():
   if m['path']=='/dev/mem'and base<=at<base+m['length']:physical=m['off']+at-base;break
  assert physical==0x2012f138 and n==4,(physical,n)
  meta=None
  for base,m in self.maps.items():
   if m['path'].startswith(p.base.RAM+'/')and m['prot']&2:meta=bytes(u.mem_read(base+3968,128));break
  assert meta and struct.unpack_from('<I',meta,32)[0]==1 and struct.unpack_from('<I',meta,124)[0]==fnv(meta[:124])^0x73a19c46
  assert not self.busy_camera
  self.store_log.append(dict(target=hex(value),preimage=hex(self.bridge),failed=False,sample=self.sleeps,busy=self.busy_camera))
  self.hardware_writes.append((hex(physical),value));self.bridge=value
  self.populate();p.base.put(self.fixture,STATE,str(self.bridge))
  if self.store_fail=='readback':self.readback_once=True;self.store_fail=''
 def model_stat(self,path):
  b=super().model_stat(path)
  if b is not None and path==DEVICE and self.available:
   b=bytearray(b);struct.pack_into('<I',b,16,stat.S_IFCHR|0o600);struct.pack_into('<Q',b,32,0x101);b=bytes(b)
  return b
 def openfile(self,path,flags):
  assert path!='/dev/d5_mptop','The shared native codec clock backup must not be touched'
  if path!=DEVICE or flags&3!=2:return super().openfile(path,flags)
  if self.store_fail=='all'or(self.store_fail=='restore'and self.bridge==0x14a):return -13
  assert flags&0x20000 and flags&0x1000
  self.target_opens.append((path,flags));self.serial+=1;self.fds[self.serial]=dict(path=path,host=self.fixture_path(path),data=b'',at=0,flags=flags);return self.serial
 def ram_words(self):
  words=super().ram_words();words[0x2012f138]=self.bridge
  request=100000+self.sleeps*6*1170000
  for i,v in enumerate([0x33544c56,3,self.sleeps*6,3|(1<<16),request,request+100,0,0]):words[0x817dbbe0+4*i]=v
  words[0x20810418]=0;words[0x20810468]=4;words[0x813f09bc]=0x801cdf6c
  words[0x208210b0]=2;words[0x208210e0]=1
  words[0x208210c8]=0x102;words[0x208210cc]=0x20;words[0x208210d0]=0x4;words[0x208210d4]=0x40;words[0x208210d8]=0x10
  return words
 def event(self):
  super().event();act=self.commands.get(self.sleeps)
  if act=='toggle':
   for at,m in self.maps.items():
    if m['path'].startswith(p.base.RAM+'/')and m['prot']&2:
     cookie=struct.unpack('<I',self.uc.mem_read(at+100,4))[0];req=struct.unpack('<I',self.uc.mem_read(at+116,4))[0]+1
     self.uc.mem_write(at+116,struct.pack('<I',req));self.uc.mem_write(at+120,struct.pack('<I',cookie^0x75e38b91));break
  elif act=='external':self.bridge=7;self.populate()
  elif act=='busy':
   self.busy_camera=True;p.base.put(self.fixture,'/proc/247/fd/9',b'');p.base.put(self.fixture,'/proc/247/fdinfo/9','flags:\t0100001\n')
  elif act=='idle':self.busy_camera=False
  elif act=='fail-restore':self.store_fail='restore'
  p.base.put(self.fixture,STATE,str(self.bridge))
 def interrupt(self,u,num,data):
  syscall=u.reg_read(UC_ARM_REG_R7)
  if syscall==54:
   fd=u.reg_read(UC_ARM_REG_R0);assert fd not in self.fds or self.fds[fd]['path']not in ['/dev/mem','/dev/d5_mptop']
  if syscall==192 and u.reg_read(UC_ARM_REG_R2)==3 and not u.reg_read(UC_ARM_REG_R3)&0x20 and u.reg_read(UC_ARM_REG_R4)in self.fds and self.fds[u.reg_read(UC_ARM_REG_R4)]['path']==DEVICE:
   fd=u.reg_read(UC_ARM_REG_R4);length=u.reg_read(UC_ARM_REG_R1);off=u.reg_read(UC_ARM_REG_R5)*4096
   assert self.fds[fd]['path']==DEVICE and length==4096 and off==0x2012f000
   if self.race=='enter':self.bridge=7;self.race=''
   elif self.race=='same-candidate':self.bridge=0x14a;self.race=''
   elif self.race=='restore'and len(self.store_log)==1:self.bridge=9;self.race=''
   at=self.map_next;self.map_next+=8192;u.mem_map(at,4096);self.maps[at]=dict(path=DEVICE,off=off,length=length,prot=3);self.populate()
   u.hook_add(UC_HOOK_MEM_WRITE,self.audit_write,begin=at,end=at+4095);self.ret(at);return
  result=super().interrupt(u,num,data)
  if syscall==192 and self.readback_once and u.reg_read(UC_ARM_REG_R0)in self.maps:
   at=u.reg_read(UC_ARM_REG_R0);m=self.maps[at]
   if m['path']==DEVICE and m['off']==0x2012f000 and m['prot']==1:self.uc.mem_write(at+0x138,struct.pack('<I',0xffffffff));self.readback_once=False
  return result

rows=[]
def trial(name,commands=None,action='stop',after=18,available=True,**kw):
 f=p.base.fixture()
 # Device identity is modeled as unavailable without breaking passive profile reads.
 p.base.put(f,'/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor','interactive\n')
 m=Bridge(f,['--run',p.base.REPORT],commands=commands,action=action,after=after,available=available,**kw);out=m.run();_,b=p.base.header(f);state=decode_qos_compare(b)
 assert all(a=='0x2012f138'for a,v in m.hardware_writes)and not m.hot_sd
 assert(f/'sys/devices/system/cpu/cpu0/cpufreq/scaling_governor').read_text()=='interactive\n'
 return f,m,out,state
def add(name,m,out,state):
 rows.append(dict(name=name,passed=True,actual_ARM_result=out,single_word_stores=m.store_log,state=state,MMIO_writes=m.hardware_writes,hot_SD_writes=m.hot_sd));print('PASS bridge',name,flush=True)
f,m,o,d=trial('ABA',commands={3:'toggle',10:'toggle'});assert o['exit']==0 and [x['target']for x in m.store_log]==['0x14a','0x0']and d['verified_restores']==1 and not d['restore_pending'];add('A-B-A-single-variable-exact-restore',m,o,d)
summary,_=decode(f/(p.base.REPORT+'/counts.bin').lstrip('/'));progress=summary['hardware_progress'];assert progress['samples']==18
assert all(r['coherent']and r['inflight']and r['first_valid_mask']=='0xff'and r['first']['0x208210e0']=='0x1'for r in progress['records'])
assert any(r['first']['0x2012f138']=='0x14a'for r in progress['records'])
for action in ['stop','usb','unknown_usb','camera_gone']:
 f,m,o,d=trial(action,commands={3:'toggle'},action=action);assert o['exit']==0 and m.bridge==0 and len(m.store_log)==2 and not d['restore_pending'];add('restore-on-'+action,m,o,d)
for initial,available in [(0,False),(0x14a,True),(7,True)]:
 f,m,o,d=trial('unavailable',initial=initial,available=available,commands={3:'toggle'});assert o['exit']==0 and not m.store_log and d['phase']=='unavailable';add('refuse-unsupported-or-no-contrast-'+str(initial)+'-'+str(available),m,o,d)
f,m,o,d=trial('busy',commands={2:'busy',3:'toggle',6:'idle'});assert o['exit']==0 and not m.store_log and d['result']==3;add('recording-interlock',m,o,d)
f,m,o,d=trial('external',commands={3:'toggle',7:'external',10:'toggle'});assert o['exit']==0 and len(m.store_log)==1 and m.bridge==7 and not d['restore_pending']and d['phase']=='externally-changed';add('external-setting-preserved',m,o,d)
f,m,o,d=trial('failure',commands={3:'toggle'},store_fail='all');assert o['exit']==0 and m.bridge==0 and not d['restore_pending']and d['result']==4;add('failed-write-no-false-B',m,o,d)
f,m,o,d=trial('readback',commands={3:'toggle'},store_fail='readback');assert o['exit']==0 and m.bridge==0 and len(m.store_log)==2 and not d['restore_pending'];add('unknown-readback-retains-owner-then-restores',m,o,d)
for race,expected in [('enter',7),('same-candidate',0x14a),('restore',9)]:
 f,m,o,d=trial('race',commands={3:'toggle',10:'toggle'}if race=='restore'else{3:'toggle'},race=race)
 assert o['exit']==0 and m.bridge==expected and not d['restore_pending'],(race,m.store_log,d);add('native-change-before-store-'+race+'-actual-backup-preserved',m,o,d)
f,m,o,d=trial('restore-failure',commands={3:'toggle',7:'fail-restore'});assert o['exit']==1 and d['restore_pending']and m.bridge==0x14a and not(f/(p.base.REPORT+'/counts.bin').lstrip('/')).exists();add('restore-error-retains-RAM-without-false-save',m,o,d)
retry=Bridge(f,['--stop']);out=retry.run();_,b=p.base.header(f);d=decode_qos_compare(b);assert out['exit']==0 and not d['restore_pending']and retry.bridge==0;add('dead-worker-stop-retries-restore-before-save',retry,out,d)
bad=bytearray(b);bad[3968+28]^=1
try:decode_qos_compare(bad)
except ValueError:pass
else:raise AssertionError('Corrupt bridge state accepted')

# Kill the modeled worker at the exact gap after the single store has changed the
# bridge, before normal post-write publication. Pre-write ownership must
# still let the real --stop client restore and export the existing samples.
class InjectedWorkerDeath(Exception):pass
class DiesAfterWrite(Bridge):
 def audit_write(self,u,kind,at,n,value,data):
  super().audit_write(u,kind,at,n,value,data)
  raise InjectedWorkerDeath()
f=p.base.fixture();p.base.put(f,DEVICE,b'');dead=DiesAfterWrite(f,['--run',p.base.REPORT],commands={3:'toggle'})
try:dead.run()
except InjectedWorkerDeath:dead.syncmap()
else:raise AssertionError('Injected worker death did not occur')
_,b=p.base.header(f);assert decode_qos_compare(b)['restore_pending']and dead.bridge==0x14a
retry=Bridge(f,['--stop']);out=retry.run();_,b=p.base.header(f);state=decode_qos_compare(b)
assert out['exit']==0 and retry.bridge==0 and not state['restore_pending'];add('death-after-write-before-ACK-recovered-from-prewrite-owner',retry,out,state)

class Phased(Bridge):
 def process_words(self):
  modules=0x10000000;core=0x11000000;camera=0x20000000;source=0x20002000
  words={modules+0x5b0e4:camera,camera:modules+0x4c608,camera+0x55c:core+0x35738,camera+0x560:source,source:core+0x59c28}
  n=self.sleeps;count=0;active=False
  for start,end in [(5,35),(45,80),(90,125)]:
   count+=max(0,min(n-start,end-start))*6
   if start<n<=end:active=True
  words[source+0x138+8*4]=count;words[source+0x138+9*4]=count;words[source+0x138+11*4]=2 if active else 1
  return words
 def ram_words(self):
  words=super().ram_words();words[0x20810908]=0x50000000 if self.bridge==0x14a else 0x40000000
  return words
 def event(self):
  super().event();p.base.put(self.fixture,'/sys/devices/platform/drime5-ddrfreq/ch_a','699000000\n'if self.bridge==0x14a else'700000000\n')
 def interrupt(self,u,num,data):
  if u.reg_read(UC_ARM_REG_R7)==180:
   addr=u.reg_read(UC_ARM_REG_R4);ptr=u.reg_read(UC_ARM_REG_R1);n=u.reg_read(UC_ARM_REG_R2);raw=bytearray(n)
   for at,val in self.process_words().items():
    if addr<=at and at+4<=addr+n:struct.pack_into('<I',raw,at-addr,val)
   u.mem_write(ptr,bytes(raw));self.ret(n);return
  return super().interrupt(u,num,data)
f=p.base.fixture();p.base.put(f,DEVICE,b'');m=Phased(f,['--run',p.base.REPORT],commands={40:'toggle',85:'toggle'},after=135);out=m.run()
assert out['exit']==0,out
saved=f/(p.base.REPORT+'/counts.bin').lstrip('/');summary,_=decode(saved);phases=summary['pinned_hardware_phases']['records']
assert [r['phase']for r in phases]==['A-original','B-bridge-qos','A-original'],phases
assert [r['DDR_A_readback_Hz']for r in phases]==[700000000,699000000,700000000]
assert [r['DMA_registers']['0x20810908']for r in phases]==['0x40000000','0x50000000','0x40000000']
assert all(r['DMA_status']==1 and r['clock_status']==1 for r in phases)and not m.hot_sd and all(a=='0x2012f138'for a,v in m.hardware_writes)
bad=bytearray(saved.read_bytes());bad[4096+32+704+80]^=1;corrupt=saved.parent/'bad-pinned.bin';corrupt.write_bytes(bad)
try:decode(corrupt)
except ValueError as e:assert 'Pinned phase integrity'in str(e)
else:raise AssertionError('Corrupt pinned B snapshot was accepted')
add('A-B-A-recording-phase-DMA-DDR-retained-after-final-A',m,out,summary['HEVC_bridge_comparison'])
report=dict(passed=True,cases=rows,progress=progress,actual_ARM=True,hardware_tested=False,binary_sha256=hashlib.sha256(p.base.BIN.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((p.R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),limits=__doc__)
(p.R/'validation/current/bridge-comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
