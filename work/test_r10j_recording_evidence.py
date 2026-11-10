"""Run the production ARM recorder across preview, record, then long preview.

OS, original RAM objects and device reads are modeled, not physical pixels,
register side effects, interrupt timing, or camera throughput.
"""
from pathlib import Path
import json,struct,sys
W=Path(__file__).resolve().parent;sys.path.insert(0,str(W))
import test_r10j_path_probe as p
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R4,UC_ARM_REG_R7
from analyze_video_counters import decode

class Recording(p.NativeRAM):
 def ram_words(self):
  words=super().ram_words()
  for i,at in enumerate([0x10400038,0x1040003c,0x10400040,0x10400044,0x10400048,0x1040004c,0x10400050,0x10400058,0x10400028]):words[at]=0x500+i
  return words
 def process_words(self):
  modules=0x10000000;core=0x11000000;camera=0x20000000;source=0x20002000
  words={modules+0x5b0e4:camera,camera:modules+0x4c608,camera+0x55c:core+0x35738,camera+0x560:source,source:core+0x59c28}
  count=max(0,min(self.sleeps-5,50))*5
  words[source+0x138+8*4]=count;words[source+0x138+9*4]=count
  return words
 def interrupt(self,u,num,data):
  if u.reg_read(UC_ARM_REG_R7)==180:
   addr=u.reg_read(UC_ARM_REG_R4);ptr=u.reg_read(UC_ARM_REG_R1);n=u.reg_read(UC_ARM_REG_R2);raw=bytearray(n)
   for at,val in self.process_words().items():
    if addr<=at and at+4<=addr+n:struct.pack_into('<I',raw,at-addr,val)
   u.mem_write(ptr,bytes(raw));self.ret(n);return
  return super().interrupt(u,num,data)

f=p.base.fixture();m=Recording(f,['--run',p.base.REPORT],after=130,kind='stable')
out=m.run();assert out['exit']==0,out
saved=f/(p.base.REPORT+'/counts.bin').lstrip('/');summary,rows=decode(saved)
assert any(float(r.get('source_forwarded_per_s')or 0)>0 for r in rows)
assert summary['path_snapshots']['retained']>0
assert max(x['elapsed_s'] for x in summary['path_snapshots']['records'])<13,summary['path_snapshots']
clocks=summary['clock_snapshots'];assert clocks['total_captured']>=2,clocks
assert all(r['status']==1 and r['scene']=='0x1908' and r['selector_words']==[hex(0x500+i)for i in range(9)] for r in clocks['records']),clocks
assert not m.hot_sd and all(x[0] in ['/dev/mem','/proc/247/mem'] for x in m.target_opens)
assert all(mapping['prot']==1 for mapping in m.maps.values() if mapping['path']=='/dev/mem')
bad=bytearray(saved.read_bytes());bad[1824+32+20]^=1
bad_path=saved.parent/'bad-clock.bin';bad_path.write_bytes(bad)
try:decode(bad_path)
except ValueError as e:assert 'Clock snapshot integrity' in str(e)
else:raise AssertionError('Corrupted clock evidence accepted')
report=dict(passed=True,actual_ARM_binary=True,hardware_tested=False,samples=summary['samples'],path=summary['path_snapshots'],clocks=clocks,no_hot_SD_writes=True,clock_corruption_rejected=True,
            limits=__doc__)
(p.R/'validation/current/recording-evidence.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print('PASS actual ARM recorder retains recording snapshots through later preview, reads and checksums clock selectors, rejects corruption; no hot SD writes.')
