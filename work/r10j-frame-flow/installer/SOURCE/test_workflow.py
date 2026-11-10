#!/usr/bin/env python3
"""Production shell + production C supervisor, host workers; no camera APIs."""
from pathlib import Path
import hashlib,json,os,re,shlex,signal,subprocess,tempfile,time,unittest
R=Path(__file__).resolve().parents[2]
SOURCE='dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c'
PLAN='3f703da4a395d3909e6d10ace5ee324afc6dff7ef4be800f05b33dab3621c3af'
OLD='07f64d2ab1a3bc073a49a2b45fffa4ec4ae41fc5ad7dfddfb6384563be9c7a14'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def exe(p,s):p.write_text(s);p.chmod(0o755)
class Fixture:
 def __init__(self,session,ops):
  self.tmp=tempfile.TemporaryDirectory(prefix='nx52-workflow-');self.w=Path(self.tmp.name)
  self.run=self.w/'run';self.out=self.w/'NXDEMO51';self.old=self.w/'old';self.payload=self.w/'payload';self.mod=self.w/'mod';self.events=self.w/'events.jsonl';self.control=self.w/'control.txt'
  for p in [self.run,self.out,self.old,self.payload/'bin',self.mod/'bin',self.mod/'state',self.out/'logs']:p.mkdir(parents=True,exist_ok=True)
  self.control.write_text('success')
  (self.mod/'state/p49-experiment.state').write_text('NXP33INSTALL1\nREADY=0.3.6-demo50\n')
  self.session=self.mod/'bin/nxl-session.arm';self.session.write_bytes(session.read_bytes());self.session.chmod(0o755)
  self.ops=self.mod/'bin/nxl-fileops';self.ops.write_bytes(ops.read_bytes());self.ops.chmod(0o755)
  common='import sys,os,json,hashlib,time,signal\nfrom pathlib import Path\nA=Path('+repr(str(self.events))+')\nCONTROL=Path('+repr(str(self.control))+')\nmode=CONTROL.read_text().strip()\ndef note(kind,**kw):\n with A.open("a") as f:f.write(json.dumps(dict(kind=kind,**kw))+"\\n")\nh=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()\n'
  self.generate=self.w/'generate_front.py'
  self.generate.write_text(common+f'''
r=Path(sys.argv[1]);d=r/'case_00';d.mkdir(parents=True)
(d/'p39_output.y8').write_bytes(b'HOST_Y');(d/'p39_output.uv8').write_bytes(b'HOST_UV');(d/'result.json').write_text('{{"MODEL":true}}\\n');(d/'p49_param_dma_audit.bin').write_bytes(b'HOST_AUDIT')
p=d/'post-input.txt';p.write_text('\\n'.join(['NXP49POSTINPUT1','version=0.3.6-demo50','source_srw_sha256={SOURCE}','y_sha256='+h(d/'p39_output.y8'),'uv_sha256='+h(d/'p39_output.uv8'),'width=6480','height=4320','stride=6496','nonce=4294967295','source_wb_complete=1','cc_source_reloaded=0','native_frame_complete=1','source_origin_x=0','source_origin_y=0','front_result_sha256='+h(d/'result.json'),'param_dma_audit_sha256='+h(d/'p49_param_dma_audit.bin')])+'\\n')
(r/'result.json').write_text('{{"MODEL":true}}\\n')
(r/'P49_FRONT_COMPLETE').write_text('\\n'.join(['NXP49FRONTCOMPLETE1','plan_sha256={PLAN}','result_sha256='+h(r/'result.json'),'post_input_sha256='+h(p),'frames_complete=1','status=PASS','cold_restart_required=1'])+'\\n')
''')
  self.front=self.payload/'bin/nxl-demo-front.arm'
  exe(self.front,'#!/usr/bin/env python3\n'+common+f'''
if sys.argv[1]=='--version':print('NXL_DEMO_FRONT=0.3.6-demo50; HOST_MODEL');sys.exit(0)
note('front_run',report=sys.argv[1])
if mode=='front_fail':sys.exit(8)
if mode=='front_cancel':
 Path(sys.argv[1],'RUNNING').touch()
 while True:time.sleep(.01)
os.execv(sys.executable,[sys.executable,{str(self.generate)!r},sys.argv[1]])
''')
  self.post=self.payload/'bin/nxl-demo-post.arm'
  exe(self.post,'#!/usr/bin/env python3\n'+common+f'''
if sys.argv[1]=='--version':print('NXL_RAW422_POST=0.4.0-demo51-422; HOST_MODEL; FINAL_CHECK_DELEGATION_R6');sys.exit(0)
if sys.argv[1]=='--clock':
 if mode=='clock_fail':sys.exit(1)
 print('NXPOSTCLOCK1 monotonic_ms='+str(time.monotonic_ns()//1000000));sys.exit(0)
if sys.argv[1]=='--verify-output':
 if mode=='external_verify_fail':sys.exit(2)
 d=Path(sys.argv[2]);lines=(d/'P52_POST_COMPLETE').read_text().splitlines();assert lines[1]=='result_sha256='+h(d/'result.json');assert lines[2]=='native_sha256='+h(d/'native.jpg');note('post_verified');sys.exit(0)
verify=sys.argv[1]=='--verify-input';args=sys.argv[2:] if verify else sys.argv[1:];opts=dict(zip(args[0::2],args[1::2]));assert not any(x.startswith('--lut') for x in opts);assert '--strength' not in opts
assert opts['--width']=='6480' and opts['--height']=='4320' and opts['--stride']=='6496'
assert verify or opts.get('--final-check')=='runner-independent'
d=None
if not verify:
 d=Path(opts['--output-dir']);assert not d.exists();d.mkdir()
try:
 assert opts['--producer-report-sha256']==h(Path(opts['--producer-report']));assert opts['--y-sha256']==h(Path(opts['--y']));assert opts['--uv-sha256']==h(Path(opts['--uv']))
except (AssertionError,OSError):note('post_pre_submit_rejected');sys.exit(42)
if mode=='input_fail':note('post_pre_submit_rejected');sys.exit(42)
if verify:note('input_verified');sys.exit(0)
hw=d/'hardware';hw.mkdir();note('post_run',output=str(d),options=opts)
if mode=='post_before_native':sys.exit(42)
(d/'P50_HARDWARE_STARTED').write_text('ONE_NATIVE_ATTEMPT\\n')
if mode=='post_cancel':
 (d/'RUNNING').touch()
 while True:time.sleep(.01)
if mode=='missing_outcome':sys.exit(8)
classification='SAFE_NO_SUBMIT' if mode=='pre_submit_fail' else 'UNKNOWN_REQUIRE_RESTART' if mode=='unknown_fail' else 'SAFE_RELEASED'
cold=0 if mode in ['pre_submit_fail','released_fail'] else 1
(hw/'result.json').write_text(json.dumps(dict(MODEL=True,classification=classification,cold_restart_required=cold))+'\\n')
lines=['NX51CONTROLLER1','classification='+classification,'controller_pid='+str(os.getpid()),'reason=MODEL_OUTCOME','result_sha256='+h(hw/'result.json'),'cold_restart_required='+str(cold),'result_complete=1']
(hw/'controller-outcome.txt').write_text('\\n'.join(lines)+'\\n'+('extra' if mode=='trailing_outcome' else ''))
if mode=='changed_outcome_result':(hw/'result.json').write_text('CHANGED')
if mode not in ['success','partial_output','external_verify_fail','clock_fail']:sys.exit(8)
(d/'native.jpg').write_bytes(b'HOST_NATIVE_JPEG_422_MODEL');(d/'result.json').write_text('{{"format":"NX52RAW422POST1","MODEL":true,"final_check_mode":"runner-independent","final_verification_delegated":true,"successful_return_contract":"PUBLISHED_PENDING_RUNNER_VERIFICATION"}}\\n')
(d/'P52_POST_COMPLETE').write_text('\\n'.join(['NX52POSTCOMPLETE1','result_sha256='+h(d/'result.json'),'native_sha256='+h(d/'native.jpg'),'source_srw_sha256={SOURCE}','width=6480','height=4320','sampling=422','status=PASS'])+'\\n')
if mode=='partial_output':(d/'native.jpg').write_bytes(b'PARTIAL')
print('RAW422_POST_PUBLISHED_PENDING_RUNNER_VERIFY')
''')
  self.restore=self.mod/'bin/nxl-oem-restore.arm'
  exe(self.restore,'#!/usr/bin/env python3\n'+common+'''
if sys.argv[1]=='--version':print('NXL_OEM_RESTORE=0.4.0-demo51-422; OEM_LIVEVIEW_RESTORE_R5 HOST_MODEL');sys.exit(0)
assert sys.argv[1] in ['--supervised','--restore-camera'];d=Path(sys.argv[2]);assert not d.exists();d.mkdir()
note('restore_run',mode=sys.argv[1],report=str(d))
if mode=='restore_cancel':
 (d/'RUNNING').touch()
 while True:time.sleep(.01)
(d/'result.json').write_text(json.dumps(dict(MODEL=True,classification='UNKNOWN_REQUIRE_RESTART' if mode=='restore_fail' else 'RESTORED'))+'\\n')
if mode=='restore_fail':sys.exit(1)
if mode=='restore_no_receipt':sys.exit(0)
lines=['NXOEMRESTORE1','classification=RESTORED','native_calls=1','request_submitted=1','api_return_verified=1','stack_restored=1','code_restored=1','detached=1','api_inflight=0','ui_running=1','app_expected=0','real_state=2','request_flag=1','fresh_start_completion=1','completion_stable=1','result_sha256='+h(d/'result.json'),'end=NXOEMRESTOREEND1']
if mode=='restore_bad_receipt':lines[11]='real_state=1'
if mode=='restore_trailing_receipt':lines.append('EXTRA')
(d/'restore-complete.txt').write_text('\\n'.join(lines)+'\\n')
if mode=='restore_bad_sha':(d/'result.json').write_text('CHANGED')
''')
  self.hwctl=self.payload/'bin/nxl-hw-controller.arm';self.hwwork=self.payload/'bin/nxl-hw-worker.arm'
  for p in [self.hwctl,self.hwwork]:exe(p,'#!/bin/sh\nexit 99\n')
  self.menu=self.mod/'bin/nxl-menu';exe(self.menu,'#!/usr/bin/env python3\n'+common+f'note("notice",argv=sys.argv[1:],locks=[x.name for x in Path({str(self.run)!r}).iterdir() if x.name.endswith(".lock")])\n')
  wrapper=(R/'base/nx-lut-lite/restore-camera.sh').read_text().replace('@DEMO51_RESTORE_SHA256@',sha(self.restore)).replace('@DEMO51_MENU_SHA256@',sha(self.menu)).replace('OUT=/mnt/mmc/NXDEMO51','OUT='+shlex.quote(str(self.out))).replace('RUN=/tmp/nx-lut-lite','RUN='+shlex.quote(str(self.run)))
  wrapper=re.sub(r'^OPS_SHA=[0-9a-f]{64}$','OPS_SHA='+sha(self.ops),wrapper,flags=re.M)
  self.restore_script=self.mod/'restore-camera.sh';exe(self.restore_script,wrapper)
  self.engine=self.mod/'lut.sh';engine_py=self.mod/'off-model.py';engine_py.write_text(common+'assert sys.argv[1:]==["off"];note("off")\n');exe(self.engine,'#!/bin/sh\nexec python3 '+shlex.quote(str(engine_py))+' "$@"\n')
  (self.mod/'process.sh').write_text('nxl_require_normal_mode(){ return 0; }\n')
  (self.mod/'progress.sh').write_text((R/'base/nx-lut-lite/progress.sh').read_text())
  text=(R/'installer/SOURCE/runner.sh.in').read_text()
  for token,p in [('SESSION',self.session),('FRONT',self.front),('POST',self.post),('HWCTL',self.hwctl),('HWWORK',self.hwwork),('MENU',self.menu),('ENGINE',self.engine),('RESTORE',self.restore),('RESTORE_SCRIPT',self.restore_script)]:text=text.replace('@DEMO'+('51' if token in ['SESSION','RESTORE','RESTORE_SCRIPT'] else '49')+'_'+token+'_SHA256@',sha(p))
  for a,b in [('RUN=/tmp/nx-lut-lite','RUN='+shlex.quote(str(self.run))),('OUT=/mnt/mmc/NXDEMO51','OUT='+shlex.quote(str(self.out))),('OLD_OUT=/mnt/mmc/NXDEMO50','OLD_OUT='+shlex.quote(str(self.old))),('PAYLOAD=/mnt/mmc/nx-lut-demo','PAYLOAD='+shlex.quote(str(self.payload))),('OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d','OPS_SHA='+sha(self.ops)),('/tmp/nx-isp-full-capture.lock',str(self.w/'other-trace')),('/mnt/mmc/NXISP_FULL_ARM',str(self.w/'trace-arm'))]:text=text.replace(a,b)
  text=re.sub(r'^OPS_SHA=[0-9a-f]{64}$','OPS_SHA='+sha(self.ops),text,flags=re.M)
  text=text.replace('@DEMO51_RETIRE_MANIFEST_SHA256@',sha(R/'base/nx-lut-lite/data/prior-front-retire.sha256'))
  assert '@DEMO' not in text
  self.runner=self.mod/'memory-test.sh';exe(self.runner,text)
  self.env=dict(os.environ,NX51_TEST_BOOT_ID='boot-A',NXL_PROGRESS_SD=str(self.w))
 def set_mode(self,mode):self.control.write_text(mode)
 def start(self):return subprocess.Popen(['/bin/sh',str(self.runner),'arbitrary_user_lut','25'],env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 def finish(self):
  p=self.start();o,e=p.communicate(timeout=20);time.sleep(.04);return p.returncode,o,e
 def status(self):
  p=subprocess.run([str(self.session),str(self.out),'status'],env=self.env,text=True,capture_output=True,check=True);return dict(x.split('=',1) for x in p.stdout.splitlines() if '=' in x)
 def job(self):return Path(self.status()['job'])
 def events_of(self,kind):return [x for x in self.events_all() if x['kind']==kind]
 def events_all(self):return [json.loads(x) for x in self.events.read_text().splitlines()] if self.events.exists() else []
 def released(self):return all(not (self.run/n).exists() for n in ['engine.lock','trace.lock'])
 def import_cache(self,unknown=False):
  report=self.old/'DEMO_old/front';report.mkdir(parents=True);subprocess.run([os.sys.executable,str(self.generate),str(report)],check=True,env=self.env)
  h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
  (self.old/'front-cache.state').write_text('\\n'.join([])) if False else None
  text='\n'.join(['NXDEMO49FRONTCACHE1','version=0.3.6-demo50','front_sha256='+('0'*64 if unknown else OLD),'plan_sha256='+PLAN,'source_srw_sha256='+SOURCE,'report_dir='+str(report),'front_marker_sha256='+h(report/'P49_FRONT_COMPLETE'),'post_input_sha256='+h(report/'case_00/post-input.txt')])+'\n';(self.old/'front-cache.state').write_text(text);return report
 def close(self):self.tmp.cleanup()
class Workflow(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.build=tempfile.TemporaryDirectory(prefix='nx52-workflow-build-');b=Path(cls.build.name);cls.session=b/'session';cls.ops=b/'fileops'
  subprocess.run(['cc','-O2','-Wno-unused-function','-DHOST_MODEL',str(R/'session/src/session.c'),'-o',str(cls.session)],check=True)
  subprocess.run(['cc','-O2',str(R/'installer/SOURCE/fileops.c'),'-o',str(cls.ops)],check=True)
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def f(self):f=Fixture(self.session,self.ops);self.addCleanup(f.close);return f
 def front(self,f):
  rc,o,e=f.finish();self.assertEqual(rc,0,(o,e,[(p.name,p.read_text()) for p in f.out.glob('JOB_*/collector.log')]))
  self.assertEqual(f.status()['state'],'NEED_JPEG_INIT');self.assertTrue(f.released());self.assertEqual(len(f.events_of('post_run')),0);return f.job()
 def test_two_stages_one_job_no_lut_single_jpeg(self):
  f=self.f();job=self.front(f);self.assertEqual(f.finish()[0],0);self.assertEqual(f.job(),job);self.assertEqual(f.status()['state'],'COMPLETE');self.assertTrue(f.released());self.assertEqual(len(f.events_of('front_run')),1);self.assertEqual(len(f.events_of('post_run')),1);self.assertTrue((job/'jpeg/attempt_1/native.jpg').is_file());self.assertFalse(list(job.rglob('look.native.bin')));self.assertIn('state=COMPLETE',(job/'RESULT.txt').read_text());self.assertEqual(f.finish()[0],0);self.assertEqual(len(f.events_of('post_run')),1)
 def test_front_restores_camera_before_publishing_album_ready(self):
  f=self.f();job=self.front(f);events=f.events_all();kinds=[x['kind'] for x in events]
  self.assertLess(kinds.index('input_verified'),kinds.index('restore_run'));self.assertEqual(f.events_of('restore_run')[0]['mode'],'--supervised');self.assertTrue((job/'restore-camera-front-1/restore-complete.txt').is_file());self.assertIn('FRONT_VERIFIED_CAMERA_RESTORED_PREPARE_JPEG_IN_ALBUM',(job/'RESULT.txt').read_text())
 def test_restore_unconfirmed_never_publishes_front_ok_or_runs_jpeg(self):
  for mode in ['restore_fail','restore_no_receipt','restore_bad_receipt','restore_trailing_receipt','restore_bad_sha']:
   with self.subTest(mode=mode):
    f=self.f();f.set_mode(mode);self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertEqual(f.status()['front_verified'],'0');self.assertFalse(f.released());self.assertEqual(len(f.events_of('front_run')),1);self.assertFalse(f.events_of('post_run'));self.assertIn('RESTORE_UNCONFIRMED',(f.job()/'RESULT.txt').read_text());self.assertTrue((f.job()/'front_1/front/P49_FRONT_COMPLETE').is_file())
 def test_restore_identity_checked_before_native_front(self):
  f=self.f();f.restore.write_text('CHANGED');self.assertNotEqual(f.finish()[0],0);self.assertFalse(f.events_of('front_run'));self.assertFalse(f.events_of('restore_run'));self.assertEqual(f.status()['state'],'RETRYABLE');self.assertIn('RESTORE_IDENTITY_FAILED',(f.job()/'RESULT.txt').read_text())
 def test_manual_restore_routes_outside_session_even_after_complete(self):
  f=self.f();job=self.front(f);self.assertEqual(f.finish()[0],0);before=f.status();ledger={str(p.relative_to(f.out)):p.read_bytes() for p in f.out.rglob('*') if p.is_file() and ('journal' in str(p) or p.name in ['current.job','SESSION.state','SUMMARY.txt'])}
  q=subprocess.run(['/bin/sh',str(f.runner),'--restore-camera'],env=f.env,capture_output=True,text=True,timeout=10);self.assertEqual(q.returncode,0,(q.stdout,q.stderr));self.assertEqual(f.status(),before);self.assertEqual(f.job(),job);self.assertEqual(f.events_of('restore_run')[-1]['mode'],'--restore-camera');self.assertEqual(len(f.events_of('front_run')),1);self.assertEqual(len(f.events_of('post_run')),1)
  self.assertEqual({n:(f.out/n).read_bytes() for n in ledger},ledger)
 def test_manual_restore_bypasses_unrelated_session_identity_but_not_restore_pin(self):
  f=self.f();f.session.write_text('CHANGED SESSION');q=subprocess.run(['/bin/sh',str(f.runner),'--restore-camera'],env=f.env,capture_output=True,text=True,timeout=10);self.assertEqual(q.returncode,0,(q.stdout,q.stderr));self.assertFalse(list(f.out.glob('JOB_*')));self.assertFalse(f.events_of('front_run'))
  f.restore.write_text('CHANGED RESTORE');q=subprocess.run(['/bin/sh',str(f.runner),'--restore-camera'],env=f.env,capture_output=True,text=True,timeout=10);self.assertNotEqual(q.returncode,0);self.assertEqual(len(f.events_of('restore_run')),1)
 def test_only_front_standalone_input_scan_but_actual_backend_revalidates(self):
  f=self.f();self.front(f);self.assertEqual(len(f.events_of('input_verified')),1);self.assertEqual(f.finish()[0],0);self.assertEqual(len(f.events_of('input_verified')),1);self.assertEqual(f.events_of('post_run')[0]['options']['--final-check'],'runner-independent');self.assertEqual(len(f.events_of('post_verified')),1);self.assertEqual(f.status()['state'],'COMPLETE')
 def test_source_changed_after_front_rejected_with_no_submit_then_fresh_retry(self):
  for plane in ['p39_output.y8','p39_output.uv8']:
   with self.subTest(plane=plane):
    f=self.f();job=self.front(f);path=Path(f.status()['front'])/'case_00'/plane;original=path.read_bytes();path.write_bytes(b'CHANGED_BETWEEN_STAGES');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RETRYABLE');self.assertEqual(f.status()['resume'],'backend');self.assertEqual(f.status()['backend_attempt'],'1');self.assertTrue((job/'jpeg/attempt_1').is_dir());self.assertTrue(f.released());self.assertFalse(f.events_of('post_run'));self.assertEqual(len(f.events_of('post_pre_submit_rejected')),1);self.assertIn('native_disposition=SAFE_NO_SUBMIT',(job/'RESULT.txt').read_text());path.write_bytes(original);self.assertEqual(f.finish()[0],0);self.assertEqual(f.status()['backend_attempt'],'2');self.assertEqual(f.status()['state'],'COMPLETE');self.assertEqual(len(f.events_of('post_run')),1)
 def test_changed_producer_front_or_audit_still_rejected_before_backend_submit(self):
  for name in ['post-input.txt','result.json','p49_param_dma_audit.bin']:
   with self.subTest(name=name):
    f=self.f();self.front(f);path=Path(f.status()['front'])/'case_00'/name;path.write_bytes(path.read_bytes()+b'CHANGED');self.assertNotEqual(f.finish()[0],0);self.assertFalse(f.events_of('post_run'));self.assertEqual(f.status()['state'],'RETRYABLE');self.assertTrue(f.released())
 def test_external_final_verifier_remains_mandatory_after_delegated_success(self):
  for mode in ['partial_output','external_verify_fail']:
   with self.subTest(mode=mode):
    f=self.f();job=self.front(f);f.set_mode(mode);self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertEqual(f.status()['reason'],'JPEG_OUTPUT_VERIFICATION_FAILED');self.assertTrue(f.released());self.assertEqual(len(f.events_of('post_run')),1);self.assertFalse(f.events_of('post_verified'));self.assertNotIn('event=backend-ok',(job/'events.log').read_text());self.assertTrue((job/'jpeg/attempt_1/P52_POST_COMPLETE').is_file())
 def test_phase_markers_use_verified_C_monotonic_clock(self):
  f=self.f();job=self.front(f);self.assertEqual(f.finish()[0],0);text=(job/'collector.log').read_text()
  for phase in ['HUMAN_STATUS_COMMIT','NATIVE_FRONT','FRONT_INPUT_VERIFY','OEM_CAMERA_RESTORE','BACKEND_POST','FINAL_PERSISTED_VERIFY']:
   self.assertRegex(text,'NX_R6_PHASE phase='+phase+' event=BEGIN monotonic_ms=[0-9]+');self.assertRegex(text,'NX_R6_PHASE phase='+phase+' event=END monotonic_ms=[0-9]+ begin_monotonic_ms=[0-9]+ result_code=0')
 def test_clock_failure_remains_diagnostic_and_does_not_change_workflow(self):
  f=self.f();f.set_mode('clock_fail');job=self.front(f);self.assertEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'COMPLETE');self.assertIn('NX_R6_PHASE phase=BACKEND_POST event=END monotonic_ms=UNAVAILABLE',(job/'collector.log').read_text());self.assertEqual(len(f.events_of('post_run')),1)
 def test_bootstrap_failure_before_job_has_SD_log(self):
  f=self.f();f.session.write_bytes(f.session.read_bytes()+b'CHANGED');self.assertNotEqual(f.finish()[0],0)
  self.assertFalse(f.out.exists() and list(f.out.glob('JOB_*')))
  text=(f.out/'logs/run.log').read_text();self.assertIn('SCRIPT_ENTERED',text);self.assertIn('SESSION_MANAGER_IDENTITY_FAILED',text)
 def test_installed_manager_used_even_if_SD_copy_missing(self):
  f=self.f();self.assertFalse((f.payload/'bin/nxl-session.arm').exists());self.front(f);self.assertTrue(f.job().is_dir())
 def test_result_inplace_publication_keeps_inode_and_updates_terminal_state(self):
  f=self.f();job=self.front(f);result=job/'RESULT.txt';inode=result.stat().st_ino
  self.assertIn('state=NEED_JPEG_INIT',result.read_text());self.assertEqual(f.finish()[0],0)
  self.assertEqual(result.stat().st_ino,inode);self.assertIn('state=COMPLETE',result.read_text())
 def test_failed_text_copy_cannot_start_native_front(self):
  f=self.f();mock=f.w/'mockbin';mock.mkdir();exe(mock/'cp','#!/bin/sh\nexit 23\n');f.env['PATH']=str(mock)+os.pathsep+os.environ.get('PATH','')
  self.assertNotEqual(f.finish()[0],0);self.assertFalse(f.events_of('front_run'));self.assertFalse(f.events_of('post_run'));self.assertTrue(f.released())
 def test_false_success_text_copy_rejected_before_native(self):
  f=self.f();mock=f.w/'mockbin';mock.mkdir();exe(mock/'cp','#!/bin/sh\nexit 0\n');f.env['PATH']=str(mock)+os.pathsep+os.environ.get('PATH','')
  self.assertNotEqual(f.finish()[0],0);self.assertFalse(f.events_of('front_run'));self.assertFalse(f.events_of('post_run'));self.assertTrue(f.released())
 def test_stale_install_marker_ignored_and_progress_written(self):
  f=self.f();(f.run/'demo49-install-session').write_text('STALE_INSTALL_ONLY\n');job=self.front(f)
  self.assertEqual(f.status()['state'],'NEED_JPEG_INIT');self.assertTrue((f.run/'demo49-install-session').is_file())
  status=(f.out/'logs/status.txt').read_text();self.assertIn('phase=CAMERA_RECOVERY',status);self.assertIn('state=NEED_JPEG_INIT',status)
  self.assertIn('job='+str(job),status);self.assertFalse((f.w/'NX_LUT_DEMO51_STATUS.txt').exists());self.assertFalse((f.w/'NX_LUT_DEMO51_PROGRESS.log').exists());self.assertTrue((f.out/'logs/progress.log').is_file());self.assertTrue((f.out/'logs/run.log').is_file())
 def test_prior_cache_is_ignored_and_current_job_runs_full_front(self):
  f=self.f();old=f.import_cache();job=self.front(f);self.assertNotEqual(f.status()['front'],str(old));self.assertEqual(f.status()['front'],str(job/'front_1/front'));self.assertEqual(len(f.events_of('front_run')),1);self.assertEqual(len(f.events_of('off')),1);self.assertEqual(f.finish()[0],0);self.assertEqual(f.job(),job);self.assertTrue(f.released());self.assertFalse((f.out/'front-cache.state').exists())
 def test_unknown_prior_cache_does_not_affect_fresh_front(self):
  f=self.f();f.import_cache(unknown=True);job=self.front(f);self.assertEqual(f.status()['front'],str(job/'front_1/front'));self.assertEqual(len(f.events_of('front_run')),1);self.assertTrue(f.released())
 def test_corrupt_cached_plane_blocks_backend_without_native(self):
  f=self.f();self.front(f);Path(f.status()['front'],'case_00/p39_output.y8').write_bytes(b'CORRUPT');self.assertNotEqual(f.finish()[0],0);self.assertFalse(f.events_of('post_run'));self.assertEqual(f.status()['state'],'RETRYABLE');self.assertTrue(f.released())
 def test_safe_pre_submit_failure_releases_and_explicit_retry_new_attempt(self):
  f=self.f();job=self.front(f);f.set_mode('pre_submit_fail');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RETRYABLE');self.assertTrue(f.released());self.assertFalse((job/'jpeg/attempt_1/native.jpg').exists());f.set_mode('success');self.assertEqual(f.finish()[0],0);self.assertTrue((job/'jpeg/attempt_1/hardware/controller-outcome.txt').is_file());self.assertTrue((job/'jpeg/attempt_2/native.jpg').is_file());self.assertEqual(len(f.events_of('post_run')),2)
 def test_safe_released_cold_requires_reset_but_releases_generic_locks(self):
  f=self.f();self.front(f);f.set_mode('released_cold_fail');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertTrue(f.released());count=len(f.events_of('post_run'));self.assertNotEqual(f.finish()[0],0);self.assertEqual(len(f.events_of('post_run')),count);f.env['NX51_TEST_BOOT_ID']='boot-B';f.set_mode('success');self.assertEqual(f.finish()[0],0)
 def test_unknown_cleanup_holds_locks_and_never_same_boot_retry(self):
  f=self.f();self.front(f);f.set_mode('unknown_fail');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertFalse(f.released());count=len(f.events_of('post_run'));self.assertNotEqual(f.finish()[0],0);self.assertEqual(len(f.events_of('post_run')),count);self.assertTrue((f.run/'engine.lock/NX51_OWNER').is_file());self.assertFalse((f.run/'engine.lock/pid').exists())
 def test_missing_trailing_and_changed_outcome_all_quarantine(self):
  for mode in ['missing_outcome','trailing_outcome','changed_outcome_result']:
   with self.subTest(mode=mode):
    f=self.f();self.front(f);f.set_mode(mode);self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertFalse(f.released());self.assertIn('state=RESTART_REQUIRED',(f.job()/'RESULT.txt').read_text())
 def test_failure_before_hardware_marker_is_retryable(self):
  f=self.f();self.front(f);f.set_mode('post_before_native');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RETRYABLE');self.assertTrue(f.released())
 def test_front_fail_conservative_hold_and_persistent_result(self):
  f=self.f();f.set_mode('front_fail');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertFalse(f.released());self.assertIn('FRONT_FAILED_NATIVE_STATE_UNVERIFIED',(f.job()/'RESULT.txt').read_text())
 def test_spent_front_grant_stops_before_begin_without_native_or_hold(self):
  f=self.f();(f.mod/'state/p49-experiment.state').write_text('NXP33INSTALL1\nSTARTED=0.3.6-demo50\npid=111\n');self.assertNotEqual(f.finish()[0],0);s=f.status();self.assertEqual(s['state'],'RETRYABLE');self.assertEqual(s['front_attempt'],'0');self.assertEqual(s['reason'],'FRONT_REINSTALL_REQUIRED');self.assertFalse(f.events_of('front_run'));self.assertFalse(f.events_of('off'));self.assertTrue(f.released())
 def test_prior_cache_does_not_bypass_spent_front_grant(self):
  f=self.f();f.import_cache();(f.mod/'state/p49-experiment.state').write_text('NXP33INSTALL1\nSTARTED=0.3.6-demo50\n');self.assertNotEqual(f.finish()[0],0);self.assertEqual(f.status()['reason'],'FRONT_REINSTALL_REQUIRED');self.assertEqual(f.status()['front_verified'],'0');self.assertFalse(f.events_of('front_run'));self.assertFalse(f.events_of('input_verified'));self.assertTrue(f.released())
 def test_each_trace_gate_has_precise_failure(self):
  for file,reason in [('other-trace','OTHER_ISP_TRACE_LOCK_PRESENT'),('trace-arm','SD_ISP_TRACE_ARM_PRESENT')]:
   with self.subTest(file=file):
    f=self.f();(f.w/file).touch();self.assertNotEqual(f.finish()[0],0);self.assertIn(reason,(f.job()/'RESULT.txt').read_text());self.assertFalse(f.events_of('front_run'));self.assertTrue(f.released())
 def test_cancel_backend_writes_restart_summary_and_holds(self):
  f=self.f();self.front(f);f.set_mode('post_cancel');p=f.start()
  for _ in range(500):
   if list(f.job().glob('jpeg/attempt_*/RUNNING')):break
   time.sleep(.01)
  else:self.fail('model backend not running')
  p.send_signal(signal.SIGTERM);p.communicate(timeout=10);self.assertNotEqual(p.returncode,0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertFalse(f.released());self.assertIn('RESTART_REQUIRED',(f.job()/'SUMMARY.txt').read_text())
if __name__=='__main__':
 suite=unittest.defaultTestLoader.loadTestsFromTestCase(Workflow);result=unittest.TextTestRunner(verbosity=2).run(suite)
 (R/'validation').mkdir(exist_ok=True);(R/'validation/workflow-tests.json').write_text(json.dumps(dict(host_model=True,hardware_tested=False,tests_run=result.testsRun,passed=result.wasSuccessful(),failures=len(result.failures),errors=len(result.errors),runner_sha256=sha(R/'installer/SOURCE/runner.sh.in'),manager_source_sha256=sha(R/'session/src/session.c'),manager_source_files={str(p.relative_to(R)):sha(p) for p in [R/'session/src/session.c',R/'session/src/journal.inc']},source_sha256={str(p.relative_to(R)):sha(p) for p in [R/'installer/SOURCE/runner.sh.in',R/'installer/SOURCE/test_workflow.py',R/'installer/SOURCE/fileops.c',R/'session/src/session.c',R/'session/src/journal.inc']}),indent=2)+'\n');raise SystemExit(not result.wasSuccessful())
