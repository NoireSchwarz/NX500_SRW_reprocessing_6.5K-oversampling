#!/usr/bin/env python3
"""Analyzer fault injection with synthetic report fixtures. No camera evidence."""
import csv, io, json, struct, sys, tempfile, unittest, zlib
from pathlib import Path
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from analyze_p21_report import analyze, LAYOUT, META
HERE=Path(__file__).resolve().parents[1]
def sealed(data):return data+struct.pack('<I',zlib.crc32(data))
class Report(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.r=Path(self.tmp.name);q=[0x3751424e,8,0xa1c00000,131072,0x12345678,99,0x91000000,0]
  self.write('copy_request.bin',sealed(struct.pack('<8I',*q)));qcrc=zlib.crc32(struct.pack('<8I',*q))
  self.w={name:([0]*n if n>1 else 0) for name,n,sign in LAYOUT}
  ones='ready paused_contract safe_to_release idle_verified fresh_completion source_ok target_ok guards_ok restore_verified registers_restored clocks_restored kernel_restored lock_acquired lock_released irq_disabled queue_initialized queue_fresh owner_stable inactive_start_verified cold_restart_required reset_after route_verified'.split()
  self.w.update({k:1 for k in ones});self.w.update(magic=0x3752424e,version=8,bytes=636,request_crc=qcrc,pa=q[2],alias_va=0x90000000,engine_jobs=1,local_reset_calls=2,rtos_guards=2,translations=8,matches=8,pixel_format=420,owner_records=2,code_guards=18,wait_return=900)
  before=bytearray((((i*73+(i>>2)*19+(q[4]>>((i&3)*8)))^(i>>7)^0xa7)&255) for i in range(131072));before[:2048]=bytes([16,80,160,235][x//16] for y in range(32) for x in range(64));before[2048:3072]=bytes([128])*1024
  im=Image.new('RGB',(64,32));im.putdata([(v,v,v) for y in range(32) for x in range(64) for v in [[16,80,160,235][x//16]]]);buf=io.BytesIO();im.save(buf,'JPEG',qtables=[[16]*64,[16]*64],subsampling=2);jpeg=buf.getvalue();after=before.copy();after[0x8000:0x8000+len(jpeg)]=jpeg
  self.write('p11_before.bin',before);self.write('p11_after.bin',after);self.write('input_64x32.yuv',before[:3072]);self.write('p21_native_output.jpg',jpeg);self.write('p21_native_stream.bin',jpeg)
  self.w.update(pixel_bytes=len(jpeg),source_crc=zlib.crc32(before[:3072]),target_crc=zlib.crc32(jpeg),output_changed=sum(a!=b for a,b in zip(before,after)))
  self.meta=dict.fromkeys(META,0);self.meta.update(magic=0x3147504a,version=1,bytes=76,width=64,height=32,format=420,stream_bits=len(jpeg)*8,stream_bytes=len(jpeg),structural=1,source_ok=1,guards_ok=1,saved=1,old_selector=3,quant_crc=zlib.crc32(bytes([16])*128));self.save_meta()
  self.write('p21_native_packet.bin',struct.pack('<16I',64,32,2,2,64,0,123,456,0,0,0x90000000,0x90000800,0x90008000,789,853,0)+bytes([16])*128)
  for label,size in [('rtos_irq',544),('kernel',1376)]:
   for phase in ['before','after']:self.write(f'p11_{label}_{phase}.bin',bytes(size))
  self.w['owner_crc']=zlib.crc32(bytes(544));g=json.loads((HERE/'p13_irq_guards.json').read_text());self.write('p13_rtos_irq_guards.bin',b''.join(struct.pack('<9I',x['address'],*x['words']) for x in g))
  self.steps=[]
  for i,step in enumerate([101,201,301,302,401,501,999]):
   rec=[0]*26;rec[0]=step;rec[1]=i*10;rec[6]=1;rec[17]=3;rec[19:21]=[0xc0755508]*2
   if step in [301,302,401,501]:rec[4]=rec[15]=0x24
   if step in [302,401,501]:rec[17]=5
   if step in [401,501]:rec[10]=len(jpeg)*8;rec[12:15]=[q[2],q[2]+2048,q[2]+0x8000]
   self.steps.append(rec)
  self.save_steps();self.save_worker()
  self.root={k:1 for k in ['ownership_verified','request_returned','release_returned','allocator_return_verified','original_code_restored','detached','copy_result_valid','copy_safe_to_release','copy_rehearsal_verified','jpeg_writeback_verified','idle_registers_verified']}
  self.root.update(format='NXJPEG1',version='0.3.4-p21',exit=0,reason='NATIVE_JPEG_FILE_VERIFIED_RESTART_REQUIRED',camera_pauses=2,camera_resumes=2,camera_resumed_verified=2,camera_stopped_at_exit=0,copy_memory_quarantined=0,api_inflight_at_exit=0,allocator_index=7);self.save_root()
  self.write('probe.log',b'RELEASE_WAIT_AFTER_DETACH\n')
  with (self.r/'allocator.csv').open('w') as f:
   writer=csv.DictWriter(f,fieldnames=['stage','stable','index','pa','entry_word0','span_blocks']);writer.writeheader()
   for i in range(3):writer.writerow(dict(stage='release_wait',stable=1,index=7,pa='0xa1c00000',entry_word0=0,span_blocks=0))
 def tearDown(self):self.tmp.cleanup()
 def write(self,name,data):(self.r/name).write_bytes(bytes(data))
 def save_worker(self):
  values=[]
  for name,n,sign in LAYOUT:
   if name=='crc':continue
   v=self.w[name];values.extend(v if n>1 else [v&0xffffffff])
  self.write('copy_worker.bin',sealed(struct.pack('<158I',*values)))
 def save_meta(self):self.write('p21_jpeg_meta.bin',sealed(struct.pack('<18I',*[self.meta[k] for k in META[:-1]])))
 def save_root(self):(self.r/'result.json').write_text(json.dumps(self.root))
 def save_steps(self):
  raw=struct.pack('<3I',0x31534a4e,1,len(self.steps))+b''.join(struct.pack('<26I',*x) for x in self.steps)+bytes(104*(16-len(self.steps)))
  self.w.update(route_samples=len(self.steps),route_crc=zlib.crc32(raw));self.write('p21_steps.bin',sealed(raw))
 def check_fail(self,key=None):
  result=analyze(self.r);self.assertEqual(result['verdict'],'NOT_VERIFIED')
  if key:self.assertFalse(result['checks'][key])
 def test_complete_model_report(self):self.assertEqual(analyze(self.r)['verdict'],'NATIVE_JPEG_WRITEBACK_VERIFIED')
 def test_native_timeout(self):self.w['wait_return']=0;self.save_worker();self.check_fail('fresh_wait')
 def test_native_error(self):self.w['error']=13;self.save_worker();self.check_fail('worker_error_zero')
 def test_guard_overwrite(self):
  b=bytearray((self.r/'p11_after.bin').read_bytes());b[-1]^=1;self.write('p11_after.bin',b);self.check_fail('all_guard_bytes')
 def test_source_overwrite(self):
  b=bytearray((self.r/'p11_after.bin').read_bytes());b[0]^=1;self.write('p11_after.bin',b);self.check_fail('input_unchanged')
 def test_wrong_image_file(self):self.write('p21_native_output.jpg',b'fake JPEG');self.check_fail('jpeg_file_equals_hardware_buffer')
 def test_no_image_file(self):(self.r/'p21_native_output.jpg').unlink();self.check_fail('jpeg_file_equals_hardware_buffer')
 def test_stale_queue(self):self.steps[2][18]=1;self.save_steps();self.save_worker();self.check_fail('fresh_queue_before_submit')
 def test_wrong_completion_selector(self):self.steps[4][17]=3;self.save_steps();self.save_worker();self.check_fail('encoder_completion_after_wait')
 def test_Rtos_ownership_changed(self):self.steps[4][25]=1;self.save_steps();self.save_worker();self.check_fail('RTOS_masks_quiet')
 def test_JPEG_valid_but_release_failed(self):
  self.root.update(exit=1,allocator_return_verified=0,reason='NATIVE_RELEASE_NOT_CONFIRMED');self.save_root();r=analyze(self.r);self.assertTrue(r['jpeg_decodable']);self.assertTrue(r['native_encoder_verified']);self.assertEqual(r['verdict'],'NOT_VERIFIED')
 def test_mismatched_state(self):self.write('p11_kernel_after.bin',b'x'*1376);self.check_fail('kernel_state_unchanged')
 def test_quarantine(self):self.w['safe_to_release']=0;self.w['quarantine_reason']=1;self.save_worker();self.check_fail('safe_to_release')
 def test_original_code_not_restored(self):self.root['original_code_restored']=0;self.save_root();self.check_fail('controller_original_code_restored')
 def test_wrong_native_packet(self):self.write('p21_native_packet.bin',bytes(192));self.check_fail('native_packet')
 def test_metadata_error(self):self.meta['error_after']=1;self.save_meta();self.check_fail('metadata_flags')
if __name__=='__main__':unittest.main(verbosity=2)
