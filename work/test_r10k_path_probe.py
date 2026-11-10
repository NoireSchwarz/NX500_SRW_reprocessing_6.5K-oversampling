"""Execute the production ARM reader with modeled OS and typed native RAM.
No hardware pixels, native interrupts, or camera performance are simulated.
"""
from pathlib import Path
import hashlib,importlib.util,json,struct,sys
W=Path(__file__).resolve().parent;R=W/'r10k-ep-flow';V=R/'validation/current/path-reader';V.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(W),str(W/'tooling/python'),str(R/'tools')]
import test_r10k_probe_os as base
sys.path.insert(0,str(R/'tools'))
from unicorn.arm_const import UC_ARM_REG_R7,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R3,UC_ARM_REG_R4
from analyze_video_counters import decode,decode_paths
base.R=R;base.V=V;base.BIN=R/'base/nx-lut-lite/bin/nxl-video-probe.arm'

class NativeRAM(base.Model):
 def __init__(self,*args,kind='stable',**kw):
  self.kind=kind;self.target_pages=[];self.max_pages=0
  super().__init__(*args,**kw)
 def ram_words(self):
  shift=self.sleeps*0x20000 if self.kind=='evict'else 0
  active=0x82010000+shift;src=0x82011000+shift;dst=0x82012000+shift;glob=0x82013000+shift
  image=[0x80588910,6608,3680,6480,3644,0,0,320,1080,0,0,92,36,16,9,1,0x202,1,9920,0]
  dest=[0x80588910,3840,2160,3840,2160,0,0,320,1080,14,12,0]
  words={}
  def p(a,*values):
   for i,v in enumerate(values):words[a+i*4]=v
  p(0x801cd9bc,0x801cda10);p(0x8014e7ec,0x8014e858)
  p(0x80592294,0x801cec38,0x801ceda0);p(0x805922bc,0x801ce78c)
  p(0x80592354,0x801cf628,0x801cf4e4);p(0x8059237c,0x801cf038)
  p(0x8153cbec,0xdeadbeef if self.kind=='bad_parent'else 0x80592110)
  p(0x8153d964,active);p(active,0x80592328)
  p(0x8153d910,2);p(0x8153d928,0x01000023,0x101,0x20810000 if self.kind=='mmio_source'else src,0,dst,0)
  p(0x81543d34,glob);p(glob,0,0x7d331,0x101,3840,2160,0,0,320,1080,12,2,0,0x10402,0)
  p(src,*image);p(dst,*dest)
  p(0x8142e018,0x80582ba8);p(0x8142ed3c,3);p(0x81430238,0x80583228);p(0x81430f5c,3)
  p(0x8142d0d8,1 if self.kind=='legacy_UD' else 0,0 if self.kind=='legacy_UD' else 1)
  p(0x8142f1e8,0x20823000 if self.kind=='mmio_op'else 0x82014000+shift);p(0x82014000+shift+0x1c,0x10100 if self.kind=='legacy_UD'else 0)
  p(0x8146e438,0x1717);p(0x813f1b30,0x1908);p(0x813ebf60,0xe005)
  return words
 def populate(self):
  words=self.ram_words()
  for at,m in self.maps.items():
   if m['path']!='/dev/mem':continue
   for a,v in words.items():
    if m['off']<=a and a+4<=m['off']+m['length']:self.uc.mem_write(at+a-m['off'],struct.pack('<I',v))
 def event(self):
  super().event();self.populate()
 def interrupt(self,uc,num,data):
  n=uc.reg_read(UC_ARM_REG_R7);flags=uc.reg_read(UC_ARM_REG_R3)
  super().interrupt(uc,num,data)
  if n==192 and not flags&0x20:
   self.populate();pages=[m['off']for m in self.maps.values()if m['path']=='/dev/mem'];self.max_pages=max(self.max_pages,len(pages));self.target_pages+=pages
   # The hot cache holds 32 pages; an idle bridge transaction can briefly
   # map one separate read-only page for its independently verified readback.
   assert len(pages)<=33
   assert all(0x80000000<=p<0x94000000 or p in [0x10400000,0x1ff04000,0x20820000,0x20821000,0x20810000,0x2012f000] for p in pages),pages

records=[]
def trial(kind,after=12,fail_sync=''):
 f=base.fixture();m=NativeRAM(f,['--run',base.REPORT],kind=kind,after=after,fail_sync=fail_sync);result=m.run()
 assert result['exit']==(1 if fail_sync else 0),(kind,result)
 output=f/(base.REPORT+'/counts.bin').lstrip('/');raw=output.read_bytes();summary,rows=decode(output)
 valid=struct.unpack_from('<I',raw,16384+28)[0]
 assert valid&0x8000 and all(r['IPC_setup_selector']=='0x801cda10'for r in rows)
 paths=summary['path_snapshots']
 if kind=='bad_parent':assert not valid&0x4000 and not paths['total_captured']
 else:
  assert valid&0x4000 and rows[0]['active_IPC_backend_vtable']=='0x80592328'
  assert rows[0]['IPC_init_function']=='0x801cf628'and rows[0]['IPC_update_function']=='0x801cf4e4'and rows[0]['IPC_setup_function']=='0x801cf038'
  for record in paths['records']:
   assert record['flags']==('0xfb'if kind=='mmio_source'else '0x7f'if kind=='mmio_op'else'0xff'),record
   if kind=='mmio_source':assert 'source_image'not in record
   else:assert record['source_image']['active_width']==6480 and record['destination_image']['active_width']==3840
   assert record['LDC_recipe_policy']==({'UD_flag':1,'QHD_flag':0}if kind=='legacy_UD'else {'UD_flag':0,'QHD_flag':1})
   if kind=='mmio_op':assert 'LDC_op_mode_bytes'not in record
   else:assert record['LDC_op_mode_bytes']==dict(offset_1d=1 if kind=='legacy_UD'else 0,offset_1e=1 if kind=='legacy_UD'else 0)
   assert record['LDC_backend_state']==3 and record['resize0_backend_state']==3
  assert all(b['elapsed_s']-a['elapsed_s']>=0.999 for a,b in zip(paths['records'],paths['records'][1:]))
 if kind=='evict':assert paths['total_captured']>11 and paths['retained']==3 and m.max_pages==32 and len(set(m.target_pages))>32
 records.append(dict(name=kind,passed=True,samples=summary['samples'],valid=hex(valid),snapshots=paths['total_captured'],max_cached_pages=m.max_pages,unique_pages=len(set(m.target_pages)),target_opens=m.target_opens,hot_SD_writes=m.hot_sd,binary_sha256=hashlib.sha256(base.BIN.read_bytes()).hexdigest()))
 print('PASS actual ARM production reader',kind,flush=True)
 return f,output,summary

if __name__=='__main__':
 trial('stable');trial('legacy_UD');trial('mmio_op');trial('bad_parent');trial('mmio_source');trial('evict',after=76)
 f,out,summary=trial('stable',fail_sync='/DONE');saved=out.read_bytes()
 m=base.Model(f,['--stop']);result=m.run();assert result['exit']==0 and out.read_bytes()==saved
 records.append(dict(name='extension_survives_export_retry',passed=True))
 # Existing committed output must never silently mask changed extension bytes.
 f,out,summary=trial('stable',fail_sync='/DONE');saved=out.read_bytes();bad=bytearray(saved);bad[1024+32+16]^=1;out.write_bytes(bad)
 result=base.Model(f,['--stop']).run();assert result['exit']==1 and 'TRACE_OUTPUT_CONFLICT'in result['stdout']and out.read_bytes()==bad
 try:decode(out)
 except ValueError as e:assert 'Path snapshot integrity'in str(e)
 else:raise AssertionError('Corrupt extension accepted')
 records.append(dict(name='corrupt_extension_refused_on_retry_and_decode',passed=True))
 legacy=W/'r9y-r9x-preview-evidence/NXDEMO51/VIDEO_STAGE_1794228518_666/counts.bin'
 summary,rows=decode(legacy);assert summary['samples']==168 and 'path_snapshots'not in summary and not summary['issues']
 records.append(dict(name='real_R9x_capture_backwards_compatible',passed=True,samples=168))
 legacy=W/'r9z-r9y-preview-evidence/NXDEMO51/VIDEO_STAGE_1794230763_554/counts.bin'
 summary,rows=decode(legacy);assert summary['samples']==131 and summary['path_snapshots']['schema']=='NXL_PATH_SNAPSHOTS1' and not summary['issues']
 assert all('LDC_recipe_policy'not in r for r in summary['path_snapshots']['records'])
 records.append(dict(name='real_R9y_schema1_capture_backwards_compatible',passed=True,samples=131))
 report=dict(passed=True,actual_ARM_binary=True,modeled_OS_and_RAM=True,hardware_tested=False,binary_sha256=hashlib.sha256(base.BIN.read_bytes()).hexdigest(),source_sha256=hashlib.sha256((R/'core/src/nxl-video-probe.c').read_bytes()).hexdigest(),cases=records)
 (R/'validation/current/path-reader.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8',newline='\n')
