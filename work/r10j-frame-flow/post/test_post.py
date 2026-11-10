#!/usr/bin/env python3
"""PC-only: real byte import, strict source proof, native-output proof rejection.
JPEG fixture is encoded by Pillow on PC only; no camera hardware is called.
"""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,tempfile,unittest
from PIL import Image
R=Path(__file__).resolve().parent
PROJECT=R.parent
W=PROJECT.parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
FLAGS=['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation']
class Post(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='raw422-post-pc-');cls.root=Path(cls.tmp.name)
  cls.binary=cls.root/'post';cls.fixture_tool=cls.root/'fixture';cls.geometry=cls.root/'import';cls.run_model=cls.root/'post-host-controller-model'
  for source,out in [('hw-post.c',cls.binary),('test_output_fixture.c',cls.fixture_tool),('test_import.c',cls.geometry),('test_run_fixture.c',cls.run_model)]:subprocess.run(FLAGS+[str(R/'src'/source),'-o',str(out)],check=True)
  cls.host_binary_sha=sha(cls.binary)
  cls.clock_fail=cls.root/'clock-fail.so';fail_source=cls.root/'clock-fail.c';fail_source.write_text('#include <time.h>\n#include <errno.h>\nint clock_gettime(clockid_t id, struct timespec *value){(void)id;(void)value;errno=EIO;return -1;}\n');subprocess.run(FLAGS+['-shared','-fPIC',str(fail_source),'-o',str(cls.clock_fail)],check=True)
  cls.jpeg=cls.root/'pc-422.jpg';Image.new('RGB',(6480,4320),(93,117,145)).save(cls.jpeg,quality=90,subsampling=1)
  cls.output=cls.root/'valid-output';fixture=subprocess.run([str(cls.fixture_tool),'fixture',str(cls.output),str(cls.jpeg)],capture_output=True,text=True)
  if fixture.returncode:raise AssertionError(fixture.stdout+fixture.stderr)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def run_post(self,*args):return subprocess.run([str(self.binary),*map(str,args)],capture_output=True,text=True,timeout=30)
 def clone(self):
  t=tempfile.TemporaryDirectory(prefix='raw422-output-');self.addCleanup(t.cleanup);p=Path(t.name)/'out';shutil.copytree(self.output,p,copy_function=os.link);return p
 def put(self,path,data):
  if path.exists() or path.is_symlink():path.unlink()
  path.write_bytes(data)
 def rehash_result(self,p):
  lines=(p/'P52_POST_COMPLETE').read_text().splitlines();lines[1]='result_sha256='+sha(p/'result.json');self.put(p/'P52_POST_COMPLETE',('\n'.join(lines)+'\n').encode())
 def base_args(self,out,frame=None):
  frame=frame or W/'demo-p50/field-report-02/DEMO_20261105_141315_391/front/case_00'
  producer=frame/'post-input.txt';lines=producer.read_text().splitlines();values=dict(x.split('=',1) for x in lines[1:])
  return ['--y',str(frame/'p39_output.y8'),'--uv',str(frame/'p39_output.uv8'),'--y-sha256',values['y_sha256'],'--uv-sha256',values['uv_sha256'],'--source-srw-sha256',values['source_srw_sha256'],'--producer-report',str(producer),'--producer-report-sha256',sha(producer),'--output-dir',str(out),'--yuv','native-nv16','--plane-width','6480','--plane-height','4320','--stride','6496','--crop-x','0','--crop-y','0','--width','6480','--height','4320']
 def test_version_exact(self):self.assertEqual(self.run_post('--version').stdout.strip(),'NXL_RAW422_POST=0.4.0-demo51-422; NATIVE_JPEG_ONLY; NV16_NO_CHROMA_AVERAGING; FINAL_CHECK_DELEGATION_R6')
 def cloned_frame(self):
  source=Path(self.base_args(self.root/'unused')[1]).parent;t=tempfile.TemporaryDirectory(prefix='raw422-source-');self.addCleanup(t.cleanup);frame=Path(t.name)/'frame';shutil.copytree(source,frame,copy_function=os.link);return frame
 def test_clock_is_readonly_strict_and_monotonic(self):
  values=[]
  for _ in range(2):
   r=self.run_post('--clock');self.assertEqual(r.returncode,0);self.assertEqual(r.stderr,'');m=re.fullmatch(r'NXPOSTCLOCK1 monotonic_ms=([0-9]+)\n',r.stdout);self.assertIsNotNone(m);values.append(int(m.group(1)))
  self.assertGreaterEqual(values[1],values[0])
 def test_clock_failure_is_explicit_but_phase_clock_does_not_gate_input(self):
  env=dict(os.environ,LD_PRELOAD=str(self.clock_fail));r=subprocess.run([str(self.binary),'--clock'],env=env,capture_output=True,text=True);self.assertEqual(r.returncode,1);self.assertEqual(r.stdout,'');out=self.root/'no-clock-input';r=subprocess.run([str(self.binary),'--verify-input',*self.base_args(out)],env=env,capture_output=True,text=True,timeout=30);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('clock_error=1',r.stderr);self.assertFalse(out.exists())
 def test_phase_logs_use_monotonic_begin_end_without_output_creation(self):
  out=self.root/'timing-readonly';r=self.run_post('--verify-input',*self.base_args(out));self.assertEqual(r.returncode,0,r.stderr);self.assertFalse(out.exists());rows=[]
  for line in r.stderr.splitlines():
   m=re.fullmatch(r'NXPOSTTIMING1 event=(BEGIN|END) phase=([A-Z_]+) monotonic_ms=([0-9]+) elapsed_ms=([0-9]+) result=(RUNNING|PASS|FAIL|DELEGATED)',line);self.assertIsNotNone(m,line);rows.append(m.groups())
  self.assertEqual([(x[0],x[1],x[4]) for x in rows],[('BEGIN','ARGS','RUNNING'),('END','ARGS','PASS'),('BEGIN','VERIFY_INPUT','RUNNING'),('END','VERIFY_INPUT','PASS')]);self.assertEqual([int(x[2]) for x in rows],sorted(int(x[2]) for x in rows))
 def test_final_check_default_internal_and_valid_explicit_modes(self):
  for mode in [None,'internal','runner-independent']:
   with self.subTest(mode=mode):
    out=self.root/('verify-mode-'+str(mode));args=self.base_args(out)+([] if mode is None else ['--final-check',mode]);r=self.run_post('--verify-input',*args);self.assertEqual(r.returncode,0,r.stderr);self.assertFalse(out.exists())
 def test_final_check_invalid_or_duplicate_enum_rejected_before_any_output(self):
  for mode in ['', 'runner', 'RUNNER-INDEPENDENT','none']:
   with self.subTest(mode=mode):
    out=self.root/'reject-final-mode';r=self.run_post(*self.base_args(out),'--final-check',mode);self.assertEqual(r.returncode,42);self.assertFalse(out.exists())
  out=self.root/'reject-duplicate-mode';r=self.run_post(*self.base_args(out),'--final-check','internal','--final-check','runner-independent');self.assertEqual(r.returncode,42);self.assertFalse(out.exists())
 def test_delegation_contract_marker_and_independent_verifier(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);d=Path(t.name)/'delegated';r=subprocess.run([str(self.fixture_tool),'fixture',str(d),str(self.jpeg),'runner-independent'],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr);j=json.loads((d/'result.json').read_text());self.assertEqual(j['final_check_mode'],'runner-independent');self.assertTrue(j['final_verification_delegated']);self.assertEqual(j['successful_return_contract'],'PUBLISHED_PENDING_RUNNER_VERIFICATION');self.assertEqual(self.run_post('--verify-output',d).returncode,0)
  (d/'native.jpg').unlink();r=subprocess.run([str(self.fixture_tool),'final-check',str(d),'runner-independent'],capture_output=True,text=True);self.assertEqual(r.returncode,0);self.assertIn('external_verify_output_required=1',r.stderr);self.assertNotEqual(subprocess.run([str(self.fixture_tool),'final-check',str(d),'internal'],capture_output=True).returncode,0);self.assertEqual(self.run_post('--verify-output',d).returncode,2)
 def test_actual_main_full_success_default_and_delegated_contracts(self):
  for mode in [None,'internal','runner-independent']:
   with self.subTest(mode=mode):
    t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);out=Path(t.name)/'out';args=self.base_args(out)+([] if mode is None else ['--final-check',mode]);r=subprocess.run([str(self.run_model),*args],env=dict(os.environ,NXPOST_TEST_CONTROLLER=str(self.fixture_tool),NXPOST_TEST_JPEG=str(self.jpeg)),capture_output=True,text=True,timeout=30);self.assertEqual(r.returncode,0,r.stderr);j=json.loads((out/'result.json').read_text());delegated=mode=='runner-independent';self.assertEqual(j['final_check_mode'],'runner-independent' if delegated else 'internal');self.assertEqual(j['final_verification_delegated'],delegated);self.assertEqual(j['successful_return_contract'],'PUBLISHED_PENDING_RUNNER_VERIFICATION' if delegated else 'PUBLISHED_AND_INTERNAL_VERIFICATION');self.assertIn('RAW422_POST_PUBLISHED_PENDING_RUNNER_VERIFY' if delegated else 'RAW422_POST_COMPLETE',r.stdout);self.assertIn('phase=FINAL_OUTPUT_CHECK',r.stderr);self.assertIn('result=DELEGATED' if delegated else 'result=PASS',r.stderr);self.assertEqual(self.run_post('--verify-output',out).returncode,0);self.assertIn('classification=COMPLETE',(out/'post-outcome.txt').read_text());self.assertIn('controller_started=1',(out/'post-outcome.txt').read_text());self.assertEqual(sha(out/'native.jpg'),sha(self.jpeg))
 def test_internal_contract_and_legacy_R5_output_still_verify(self):
  p=self.clone();j=json.loads((p/'result.json').read_text());self.assertEqual(j['final_check_mode'],'internal');self.assertFalse(j['final_verification_delegated']);self.assertEqual(j['successful_return_contract'],'PUBLISHED_AND_INTERNAL_VERIFICATION')
  for k in ['final_check_mode','final_verification_delegated','successful_return_contract']:del j[k]
  self.put(p/'result.json',json.dumps(j).encode());self.rehash_result(p);self.assertEqual(self.run_post('--verify-output',p).returncode,0)
 def test_partial_or_inconsistent_delegation_marker_rejected(self):
  for field,value in [('final_check_mode','unexpected'),('final_verification_delegated',True),('successful_return_contract','PUBLISHED_PENDING_RUNNER_VERIFICATION')]:
   with self.subTest(field=field):
    p=self.clone();j=json.loads((p/'result.json').read_text());j[field]=value;self.put(p/'result.json',json.dumps(j).encode());self.rehash_result(p);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
  p=self.clone();j=json.loads((p/'result.json').read_text());del j['final_check_mode'];self.put(p/'result.json',json.dumps(j).encode());self.rehash_result(p);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_each_source_proof_corruption_fails_before_native_in_both_modes(self):
  for target in ['p39_output.y8','p39_output.uv8','post-input.txt','result.json','p49_param_dma_audit.bin']:
   for mode in ['internal','runner-independent']:
    with self.subTest(target=target,mode=mode):
     frame=self.cloned_frame();path=frame/target;out=frame.parent/'failed';args=self.base_args(out,frame);data=bytearray(path.read_bytes());data[len(data)//2]^=1;self.put(path,data);r=self.run_post(*args,'--final-check',mode);self.assertEqual(r.returncode,42,r.stderr);self.assertFalse((out/'P50_HARDWARE_STARTED').exists());self.assertFalse((out/'P52_POST_COMPLETE').exists());self.assertIn('classification=SAFE_NO_SUBMIT',(out/'post-outcome.txt').read_text())
 def test_delegate_cannot_bypass_controller_exec_failure(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);out=Path(t.name)/'delegate-controller-fail';r=self.run_post(*self.base_args(out),'--final-check','runner-independent');self.assertEqual(r.returncode,1,r.stderr);self.assertNotIn('RAW422_POST_PUBLISHED_PENDING_RUNNER_VERIFY',r.stdout);self.assertFalse((out/'P52_POST_COMPLETE').exists());self.assertIn('controller_started=1',(out/'post-outcome.txt').read_text());self.assertIn('classification=CONTROLLER_RESULT_REQUIRED',(out/'post-outcome.txt').read_text())
 def test_geometry_all_y_uv_rows_and_padding(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);r=subprocess.run([str(self.geometry),t.name],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('all4/4 UV rows',r.stdout)
 def test_full_native_output_fixture_passes(self):self.assertEqual(self.run_post('--verify-output',self.output).returncode,0)
 def test_sampling420_marker_rejected(self):
  p=self.clone();self.put(p/'P52_POST_COMPLETE',(p/'P52_POST_COMPLETE').read_bytes().replace(b'sampling=422',b'sampling=420'));self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_sampling420_header_rejected(self):
  p=self.clone();b=bytearray((p/'native.jpg').read_bytes());sof=b.index(b'\xff\xc0');self.assertEqual(b[sof+11],0x21);b[sof+11]=0x22;self.put(p/'native.jpg',b)
  r=subprocess.run([str(self.fixture_tool),'header',str(p/'native.jpg')]);self.assertNotEqual(r.returncode,0);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_missing_eoi_rejected(self):
  p=self.clone();self.put(p/'native.jpg',(p/'native.jpg').read_bytes()[:-2]);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_native_stream_crc_corruption_rejected(self):
  p=self.clone();b=bytearray((p/'native.jpg').read_bytes());b[-10]^=1;self.put(p/'native.jpg',b);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_native_proof_crc_corruption_rejected(self):
  p=self.clone();b=bytearray((p/'hardware/j422-proof.bin').read_bytes());b[-1]^=1;self.put(p/'hardware/j422-proof.bin',b);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_source422_corruption_rejected(self):
  p=self.clone();self.put(p/'hardware/input422.bin',b'CORRUPT\n');self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_controller_cleanup_failure_rejected(self):
  p=self.clone();j=json.loads((p/'hardware/result.json').read_text());j['camera_resumed_verified']=0;self.put(p/'hardware/result.json',json.dumps(j).encode());self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_lut_claim_rejected_even_if_result_hash_updated(self):
  p=self.clone();j=json.loads((p/'result.json').read_text());j['LUT_applied']=True;self.put(p/'result.json',json.dumps(j).encode());self.rehash_result(p);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_duplicate_json_field_rejected(self):
  p=self.clone();b=(p/'result.json').read_text().rstrip();self.put(p/'result.json',(b[:-1]+',"sampling":420}\n').encode());self.rehash_result(p);self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_extra_jpeg_rejected(self):
  p=self.clone();self.put(p/'OTHER.JPG',b'OTHER');self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_failed_journal_rejected(self):
  p=self.clone();self.put(p/'post-status.txt',(p/'post-status.txt').read_bytes().replace(b'phase=COMPLETE',b'phase=FAILED'));self.assertEqual(self.run_post('--verify-output',p).returncode,2)
 def test_real_front_input_proof_accepted_readonly(self):
  out=self.root/'must-not-create';r=self.run_post('--verify-input',*self.base_args(out));self.assertEqual(r.returncode,0,r.stderr);self.assertIn('RAW422_POST_INPUT_VERIFIED',r.stdout);self.assertFalse(out.exists())
 def test_source_proof_mismatch_rejected_readonly(self):
  out=self.root/'must-not-create-bad';a=self.base_args(out);a[a.index('--producer-report-sha256')+1]='0'*64;r=self.run_post('--verify-input',*a);self.assertEqual(r.returncode,42);self.assertFalse(out.exists())
 def test_before_submit_failure_has_durable_failed_status_and_42(self):
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);out=Path(t.name)/'failed';a=self.base_args(out);a[a.index('--producer-report-sha256')+1]='0'*64;r=self.run_post(*a);self.assertEqual(r.returncode,42,r.stderr)
  status=(out/'post-status.txt').read_text();self.assertIn('phase=FAILED',status);self.assertIn('reason=SOURCE_OR_NATIVE_PROOF',status);self.assertFalse((out/'P50_HARDWARE_STARTED').exists());self.assertEqual((out/'post-outcome.txt').read_text().splitlines(),['NX52POSTOUTCOME1','exit=42','controller_started=0','controller_pid=0','classification=SAFE_NO_SUBMIT'])
 def test_unknown_lut_arguments_rejected_before_submit(self):
  out=self.root/'reject-old-lut';a=self.base_args(out)+['--lut','anything.bin'];r=self.run_post(*a);self.assertEqual(r.returncode,42);self.assertFalse(out.exists())
 def test_chroma_profile_or_geometry_changes_rejected(self):
  for key,bad in [('--yuv','601-limited'),('--plane-height','2160'),('--crop-y','1'),('--stride','6480')]:
   with self.subTest(key=key):
    out=self.root/('reject'+key);a=self.base_args(out);a[a.index(key)+1]=bad;r=self.run_post('--verify-input',*a);self.assertEqual(r.returncode,42);self.assertFalse(out.exists())
 def test_child_exec_failure_is_not_safe_no_submit(self):
  # The fixed camera controller is intentionally absent on this PC. This
  # checks wait/outcome semantics without launching any replacement hardware.
  self.assertFalse(Path('/mnt/mmc/nx-lut-demo/bin/nxl-hw-controller.arm').exists())
  t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);out=Path(t.name)/'controller-exec-failure';r=self.run_post(*self.base_args(out));self.assertEqual(r.returncode,1,r.stderr)
  self.assertTrue((out/'P50_HARDWARE_STARTED').is_file());self.assertIn('phase=FAILED',(out/'post-status.txt').read_text());self.assertIn('reason=NATIVE_CONTROLLER_FAILED',(out/'post-status.txt').read_text());lines=(out/'post-outcome.txt').read_text().splitlines();self.assertEqual(len(lines),5);self.assertIn('controller_started=1',lines);self.assertIn('classification=CONTROLLER_RESULT_REQUIRED',lines);self.assertFalse((out/'P52_POST_COMPLETE').exists())
if __name__=='__main__':
 result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Post));d=R/'validation';d.mkdir(exist_ok=True)
 (d/'host-tests.json').write_text(json.dumps(dict(format='NXPOST_R6_HOST_TESTS1',hardware_called=False,host_model=True,fixture_JPEG_encoded_on_PC=True,host_binary_sha256=Post.host_binary_sha,production_arm_sha256=sha(R/'bin/nxl-demo-post.arm'),source_sha256={str(p.relative_to(PROJECT)):sha(p) for p in [R/'src/hw-post.c',R/'test_post.py',R/'src/test_output_fixture.c',R/'src/test_run_fixture.c',R/'src/test_import.c',R/'tools/build_post.py',PROJECT/'hardware/src/bitb_job.h',PROJECT/'hardware/src/pipeline_proof.h',PROJECT/'core/src/param_dma_contract.h',PROJECT/'core/src/cc_contract.h']},tests_run=result.testsRun,passed=result.wasSuccessful(),errors=len(result.errors),failures=len(result.failures),sources={str(p.relative_to(R)):sha(p) for p in (R/'src').glob('*') if p.is_file()}),indent=2)+'\n')
 raise SystemExit(0 if result.wasSuccessful() else 1)
