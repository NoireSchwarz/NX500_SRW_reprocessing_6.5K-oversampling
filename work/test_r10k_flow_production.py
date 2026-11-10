"""Production ARM sampler over three modeled mode/preview/record transitions.
Synthetic register/time values validate capture/decoding, not camera performance.
"""
from pathlib import Path
import hashlib,json,struct,sys
W=Path(__file__).resolve().parent;sys.path.insert(0,str(W))
import test_r10k_path_probe as p
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R4,UC_ARM_REG_R7
from analyze_video_counters import decode

class Flow(p.NativeRAM):
 def __init__(self,*args,**kwargs):
  self.target_stores=[];super().__init__(*args,**kwargs);self.uc.hook_add(UC_HOOK_MEM_WRITE,self.audit_write)
 def audit_write(self,u,k,a,n,v,d):
  for at,m in self.maps.items():
   if m['path']=='/dev/mem' and at<=a<at+m['length']:self.target_stores.append((m['off']+a-at,n))
 def segment(self):
  n=self.sleeps
  if n<90:return 0,21<=n<66,21
  if n<180:return 1,111<=n<156,111
  return 2,201<=n<246,201
 def ram_words(self):
  d,recording,begin=self.segment();w=super().ram_words();driver=[0x1818,0x1919,0x1717][d];preset=0xe005 if d==2 else 0xe004
  w[0x8146e438]=driver;w[0x813ebf60]=preset;w[0x82010000]=0x80592328 if d==2 else 0x80592268
  w[0x82013000+12]=4096 if d==0 else 3840
  w[0x813f09b4+8]=0x801cdf6c;w[0x813f09b4+12]=0x801aed5c;w[0x813f09b4+24]=0x801cdef0
  w[0x20810468]=0xcc
  for i,a in enumerate([0x10400038,0x1040003c,0x10400040,0x10400044,0x10400048,0x1040004c,0x10400050,0x10400058,0x10400028]):w[a]=0x500+i
  seq=12+self.sleeps*6;flags=15|(1<<8)|(1<<16)|(1<<26)|(1<<28)
  data=[0x33544c56,4,seq,3|(1<<16),0,1000000+(seq+1)*867534,0,0]+[0]*56
  for k in range(seq-7,seq+1):
   hw=(1000000+k*867534)&0xffffffff;data[8+(k&7)*7:15+(k&7)*7]=[k,hw,(hw+100)&0xffffffff,(hw+300)&0xffffffff,(hw+750000)&0xffffffff,(hw+750100)&0xffffffff,flags]
  for i,v in enumerate(data):w[0x817dbbe0+4*i]=v
  for j,base in enumerate([0x20821400,0x20821800,0x20821480,0x20821880]):
   values=([0xb1000000+j*0x100000,0xb1047040+j*0x100000,(720<<12)|0x51,0x08000000,0x100]if recording else [0]*5)
   for k,v in enumerate(values):w[base+4*k]=v
  return w
 def process_words(self):
  d,recording,begin=self.segment();modules=0x10000000;core=0x11000000;camera=0x20000000;source=0x20002000+d*0x10000;hal=0x20004000+d*0x10000
  count=max(0,self.sleeps-begin+1)*6 if recording else 0
  w={modules+0x5b0e4:camera,camera:modules+0x4c608,camera+0x55c:core+0x35738,camera+0x560:source,
     source:core+0x59c28,source+0x158:count,source+0x15c:count,source+0x164:2 if recording else 1,
     modules+0x5b144:hal,hal:modules+0x4c9c8,hal+0x174:count,
     hal+0x64:4096 if d==0 else 3840,hal+0x68:2160,hal+0x80:24 if d==0 else 30,
     hal+0x7c:80000 if d==2 else 70000,hal+0x78:15}
  for off,value in [(0x30,24. if d==0 else 30.),(0x34,24. if d==0 else 30000/1001)]:w[camera+off]=struct.unpack('<I',struct.pack('<f',value))[0]
  return w
 def interrupt(self,u,num,data):
  if u.reg_read(UC_ARM_REG_R7)==180:
   addr=u.reg_read(UC_ARM_REG_R4);ptr=u.reg_read(UC_ARM_REG_R1);n=u.reg_read(UC_ARM_REG_R2);raw=bytearray(n)
   for at,val in self.process_words().items():
    if addr<=at and at+4<=addr+n:struct.pack_into('<I',raw,at-addr,val)
   u.mem_write(ptr,bytes(raw));self.ret(n);return
  return super().interrupt(u,num,data)

f=p.base.fixture();m=Flow(f,['--run',p.base.REPORT],kind='stable',after=275)
out=m.run();assert out['exit']==0,out
saved=f/(p.base.REPORT+'/counts.bin').lstrip('/');s,intervals=decode(saved)
assert s['samples']==275 and not m.hot_sd and not m.target_stores
assert s['frame_timing']['schema']=='NXL_FRAME_FLOW4'
assert s['frame_timing']['valid_frames']>1500
groups=s['frame_flow_comparison']['groups'];assert {g['driver_mode']for g in groups}=={'0x1717','0x1818','0x1919'}
assert {g['source_state']for g in groups}=={1,2}
contexts=s['frame_timing']['flow_contexts']
assert {c['encoder_width']for c in contexts}=={3840,4096}
assert {c['encoder_fps_rounded']for c in contexts}=={24,30}
pins=s['pinned_hardware_phases'];assert pins['capacity']==16 and not pins['overflow'] and len(pins['records'])>=9,pins
assert all(r['DMA_status']==1 and r['clock_status']==1 for r in pins['records'])
assert {r['driver']for r in pins['records']}=={'0x1717','0x1818','0x1919'}
assert not s['flow_bookmarks']['restore_pending']
ep=s['EP_readback_comparison'];assert ep['samples']==275 and ep['EP_reopened_by_user'] and not ep['device_writes']
assert {g['driver_mode']for g in ep['groups']}=={'0x1717','0x1818','0x1919'}
assert all(g['retained_samples'] for g in ep['groups'])
assert all(r['all_groups_stable'] and r['same_IPC_header'] and r['same_mode']for r in ep['records'])
assert {r['DMA']['RDMA1_bank0']['stride_bytes']for r in ep['records']}=={0,720}
# New schema corruption must be caught before any interpretation.
bad=bytearray(saved.read_bytes());bad[16384+255*4+8]^=1;corrupt=saved.parent/'corrupt-flow.bin';corrupt.write_bytes(bad)
try:decode(corrupt)
except ValueError as e:assert 'Invalid sample'in str(e)
else:raise AssertionError('Corrupt flow context accepted')
# Preserve decoding of real field captures, including schema5.
legacy=[]
for name in ['r10h-field-evidence','r10i-field-evidence']:
 capture=next((W/name).rglob('counts.bin'));summary,_=decode(capture);assert summary['frame_timing']['valid_frames']>0;legacy.append(dict(path=str(capture),samples=summary['samples']))
report=dict(passed=True,actual_ARM=True,hardware_tested=False,samples=s['samples'],frames=s['frame_timing']['valid_frames'],
            EP_readback_groups=ep['groups'],EP_readback_samples=ep['samples'],
            mode_groups=groups,pinned_modes=pins,contexts_retained=len(contexts),target_writes=m.target_stores,hot_SD_writes=m.hot_sd,
            backward_compatible_actual_captures=legacy,corruption_rejected=True,
            binary_sha256=hashlib.sha256(p.base.BIN.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((p.R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),limits=__doc__)
(p.R/'validation/current/flow-production.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS production ARM three-mode flow, settled preview/record snapshots, mode/FPS decode, corruption rejection and legacy captures')
