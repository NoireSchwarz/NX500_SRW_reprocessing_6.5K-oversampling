import hashlib,importlib.util,json,os,struct,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('p39',ROOT/'analyze_front.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Report(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name);self.base=Path(os.environ['P39_MODEL_REPORT'])
  for f in self.base.iterdir():
   if f.is_file():(self.p/f.name).symlink_to(f)
  self.j=json.loads((self.p/'result.json').read_text())
 def tearDown(self):self.tmp.cleanup()
 def put(self,name,data):
  f=self.p/name
  if f.is_symlink():f.unlink()
  f.write_bytes(data)
 def result(self):self.put('result.json',(json.dumps(self.j)+'\n').encode());return m.analyze(self.p)['real_srw_full_frame_writeback_verified']
 def proof_record(self,name,offset,word,value,field,hashfield):
  b=bytearray((self.p/name).read_bytes());struct.pack_into('<I',b,word*4,value);self.put(name,b);self.j[field]=list(struct.unpack('<%dI'%(len(b)//4),b)) if name=='p39_stop.bin' else self.j[field]
  self.j[hashfield]=hashlib.sha256(b).hexdigest()
  for n,key in [('p39_cap_observed.bin','cap_observed_sha256'),('p39_cap_expected.bin','cap_expected_sha256')]:
   proof=bytearray((self.p/n).read_bytes());proof[offset:offset+len(b)]=b;self.put(n,proof);self.j[key]=hashlib.sha256(proof).hexdigest()
 def test_valid(self):self.assertTrue(self.result())
 def test_full_frame_and_scope(self):
  for key,value in [('tile_columns',1),('luma_tiles_written',1),('srw_decode_jobs',0),('ep_jobs',1),('camera_resumed_verified',0),('task_quiescent_verified',0)]:
   with self.subTest(key=key):
    old=self.j[key];self.j[key]=value;self.assertFalse(self.result());self.j[key]=old
 def test_last_tile_missing_even_with_new_output_hash(self):
  b=bytearray((self.p/'p39_output.y8').read_bytes())
  for y in range(2944,4320):b[y*6496+13*464:y*6496+6480]=bytes([0xa5])*448
  self.put('p39_output.y8',b);self.j['luma_sha256']=hashlib.sha256(b).hexdigest();self.assertFalse(self.result())
 def test_rehashed_old_stop_flag(self):
  self.proof_record('p39_stop.bin',0x9400,22,0xbaadf00d,'native_stop_record_words','native_stop_record_sha256');self.assertFalse(self.result())
 def test_rehashed_irq_pending_error(self):
  b=bytearray((self.p/'p39_diagnostics.bin').read_bytes());struct.pack_into('<I',b,7*512+9*4,0xa0);self.put('p39_diagnostics.bin',b)
  self.j['diagnostic_snapshots'][7][9]=0xa0;self.j['diagnostic_sha256']=hashlib.sha256(b).hexdigest()
  for n,key in [('p39_cap_observed.bin','cap_observed_sha256'),('p39_cap_expected.bin','cap_expected_sha256')]:
   proof=bytearray((self.p/n).read_bytes());proof[0x8000:0x9200]=b;self.put(n,proof);self.j[key]=hashlib.sha256(proof).hexdigest()
  self.assertFalse(self.result())
 def test_full_source_row_and_boundary_guard(self):
  b=bytearray((self.p/'p39_rows.csv').read_bytes());b[-5]^=1;self.put('p39_rows.csv',b);self.assertFalse(self.result())
  self.put('p39_rows.csv',(self.base/'p39_rows.csv').read_bytes());b=bytearray((self.p/'p39_boundary_guards.bin').read_bytes());b[-1]^=1;self.put('p39_boundary_guards.bin',b);self.assertFalse(self.result())
 def test_coherently_rehashed_wb_restore_corruption(self):
  b=bytearray((self.p/'p41_wb_audit.bin').read_bytes());b[144*4]^=1;self.put('p41_wb_audit.bin',b)
  for name,key in [('p39_cap_observed.bin','cap_observed_sha256'),('p39_cap_expected.bin','cap_expected_sha256')]:
   cap=bytearray((self.p/name).read_bytes());cap[0xa000:0xa400]=b;self.put(name,cap);self.j[key]=hashlib.sha256(cap).hexdigest()
  self.assertFalse(self.result())
 def wb_change(self,edit):
  q=list(struct.unpack('<256I',(self.p/'p41_wb_audit.bin').read_bytes()));edit(q)
  audit=struct.pack('<256I',*q);self.put('p41_wb_audit.bin',audit)
  for name,key in [('p39_cap_observed.bin','cap_observed_sha256'),('p39_cap_expected.bin','cap_expected_sha256')]:
   cap=bytearray((self.p/name).read_bytes());cap[0xa000:0xa400]=audit;self.put(name,cap);self.j[key]=hashlib.sha256(cap).hexdigest()
  return q
 def test_unconfirmed_wb_keeps_frame_verdict_separate(self):
  def ignore(q):
   for off in [96,112,128,228]:q[off:off+6]=q[80:86]
   q[75:79]=[0]*4
  self.wb_change(ignore)
  self.j.update(wb_readback_verified=False,wb_after_setter_match=0,wb_prestart_match=0,wb_after_start_match=0,wb_after_completion_match=0,reason='SRW_NATIVE_FRAME_SAVED_WB_UNCONFIRMED_RESTART_REQUIRED')
  self.assertTrue(self.result());self.assertFalse(m.analyze(self.p)['wb_readback_verified'])
 def test_forged_wb_success_rejected(self):
  def change(q):q[96+2]^=0x100000
  self.wb_change(change);self.assertFalse(self.result())
 def test_secondary_write_claim_rejected(self):
  self.j['wb_secondary_setter_calls']=1;self.assertFalse(self.result())
 def test_wb_success_requires_restore(self):
  def change(q):q[144+2]^=0x100000;q[66]=0;q[73]=134
  self.wb_change(change);self.j.update(wb_readback_verified=False,wb_restore_readback_match=0,reason='SRW_NATIVE_FRAME_SAVED_WB_UNCONFIRMED_RESTART_REQUIRED')
  self.assertTrue(self.result());self.assertFalse(m.analyze(self.p)['wb_readback_verified'])
if __name__=='__main__':unittest.main(verbosity=2)
