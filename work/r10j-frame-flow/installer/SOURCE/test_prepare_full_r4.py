#!/usr/bin/env python3
"""R4 proof-only false-hold retirement. Production shell/C, host data only."""
from pathlib import Path
import fcntl,hashlib,json,os,shutil,subprocess,tempfile,time,unittest
from test_workflow import Fixture,exe,sha,R

TAIL='seq=2 event=configure state=NEED_STILL_INIT stage=idle outcome=NONE reason=NOT_STARTED\nseq=3 event=begin state=FRONT_RUNNING stage=front outcome=RUNNING reason=TASK_STARTED\nseq=4 event=fail state=RESTART_REQUIRED stage=front outcome=UNKNOWN_REQUIRE_RESTART reason=FRONT_RESTART_OR_PATH_INVALID\n'
class FullFixture(Fixture):
 def __init__(self,session,ops):
  super().__init__(session,ops)
  subprocess.run([str(self.session),str(self.out),'new'],env=self.env,check=True,capture_output=True,text=True)
  self.legacy_job=self.job();self.report=self.legacy_job/'front_1/front'
  subprocess.run([os.sys.executable,str(self.generate),str(self.report)],check=True,env=self.env)
  self.initial_state=(self.legacy_job/'STATE.journal').read_bytes();self.initial_pointer=(self.out/'CURRENT.journal').read_bytes();self.initial_events=(self.legacy_job/'events.log').read_text()
  (self.legacy_job/'STATE.journal').unlink();(self.out/'CURRENT.journal').unlink()
  with (self.legacy_job/'events.log').open('a') as out:out.write(TAIL)
  self.proc=self.w/'proc';(self.proc/'self').mkdir(parents=True);(self.proc/'self/status').write_text('Name:\tsh\n')
  self.manifest=self.mod/'data/prior-front-retire.sha256';self.manifest.parent.mkdir()
  self.manifest.write_text(''.join(sha(p)+'  '+str(p.relative_to(self.report))+'\n' for p in sorted(self.report.rglob('*')) if p.is_file()))
  text=self.runner.read_text();text=text.replace('RETIRE_JOB_NAME=JOB_1793869199_415_10dc3936','RETIRE_JOB_NAME='+self.legacy_job.name)
  text=text.replace('RETIRE_MANIFEST_SHA='+sha(R/'base/nx-lut-lite/data/prior-front-retire.sha256'),'RETIRE_MANIFEST_SHA='+sha(self.manifest)).replace('PREPARE_PROC=/proc','PREPARE_PROC='+str(self.proc));exe(self.runner,text)
  for n in ['engine.lock','trace.lock']:self.hold(n)
  (self.mod/'state/p49-experiment.state').write_text('NXP33INSTALL1\nSTARTED=0.3.6-demo50\npid=111\n')
  self.original={str(p.relative_to(self.report)):p.read_bytes() for p in self.report.rglob('*') if p.is_file()}
 def hold(self,name):
  p=self.run/name;p.mkdir(exist_ok=True);(p/'NX51_OWNER').write_text('NX51LOCK1\njob='+str(self.legacy_job)+'\nboot=boot-A\n');(p/'RESTART_REQUIRED').write_text('NX51HOLD1\njob='+str(self.legacy_job)+'\nboot=boot-A\nreason=FRONT_RESTART_OR_PATH_INVALID\n');return p
 def prepare(self):
  p=subprocess.run(['/bin/sh',str(self.runner),'--prepare-full'],env=self.env,capture_output=True,text=True,timeout=20);time.sleep(.06);return p
 def process(self,pid,name):
  p=self.proc/str(pid);p.mkdir();(p/'status').write_text('Name:\t'+name+'\n')
 def native_calls(self):return [x for x in self.events_all() if x['kind'] in ['front_run','post_run','off']]
 def check_old_unchanged(self):return self.original=={str(p.relative_to(self.report)):p.read_bytes() for p in self.report.rglob('*') if p.is_file()}

class PrepareFull(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.build=tempfile.TemporaryDirectory(prefix='nx52-prepare-build-');b=Path(cls.build.name);cls.session=b/'session';cls.ops=b/'fileops'
  subprocess.run(['cc','-O2','-Wno-unused-function','-DHOST_MODEL',str(R/'session/src/session.c'),'-o',str(cls.session)],check=True)
  subprocess.run(['cc','-O2',str(R/'installer/SOURCE/fileops.c'),'-o',str(cls.ops)],check=True)
 @classmethod
 def tearDownClass(cls):cls.build.cleanup()
 def f(self):f=FullFixture(self.session,self.ops);self.addCleanup(f.close);return f
 def assertBlocked(self,f,reason=None):
  p=f.prepare();self.assertNotEqual(p.returncode,0,p.stdout+p.stderr);status=subprocess.run([str(f.session),str(f.out),'status'],env=f.env,text=True,capture_output=True);self.assertTrue(status.returncode!=0 or 'state=RESTART_REQUIRED\n' in status.stdout);self.assertFalse(f.native_calls());self.assertNotIn('READY=',(f.mod/'state/p49-experiment.state').read_text());self.assertTrue((f.run/'engine.lock/RESTART_REQUIRED').exists());self.assertTrue((f.run/'trace.lock/RESTART_REQUIRED').exists())
  if reason:self.assertIn(reason,(f.legacy_job/'RESULT.txt').read_text())
 def test_prepare_proofs_only_then_next_call_full_fresh_front_2(self):
  f=self.f();self.assertEqual(f.status()['reason'],'LEGACY_STATE_DIVERGENCE');p=f.prepare();self.assertEqual(p.returncode,0,(p.stdout,p.stderr,(f.legacy_job/'collector.log').read_text()));s=f.status();self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0');self.assertEqual(s['front_attempt'],'1');self.assertTrue(f.released());self.assertFalse(f.native_calls());self.assertEqual(len(f.events_of('input_verified')),1);self.assertTrue(f.check_old_unchanged());self.assertFalse((f.legacy_job/'jpeg').exists());self.assertFalse((f.legacy_job/'unused-full-preparation-verify').exists());self.assertEqual((f.mod/'state/p49-experiment.state').read_text(),'NXP33INSTALL1\nREADY=0.3.6-demo50\n');self.assertIn('FULL_TEST_PREPARED_NO_REUSE',(f.legacy_job/'RESULT.txt').read_text());self.assertTrue(any(x['argv']==['--demo-full-ready'] for x in f.events_of('notice')))
  self.assertEqual(f.finish()[0],0);s=f.status();self.assertEqual(s['front_attempt'],'2');self.assertEqual(s['front'],str(f.legacy_job/'front_2/front'));self.assertEqual(len(f.events_of('front_run')),1);self.assertFalse(f.events_of('post_run'));self.assertTrue(f.check_old_unchanged());self.assertEqual(f.finish()[0],0);self.assertEqual(f.status()['state'],'COMPLETE');self.assertEqual(len(f.events_of('post_run')),1);self.assertEqual(f.events_of('post_run')[0]['options']['--y'],str(f.legacy_job/'front_2/front/case_00/p39_output.y8'))
 def test_known_false_path_error_with_authoritative_journal_can_prepare(self):
  f=self.f();(f.legacy_job/'STATE.journal').write_bytes(f.initial_state);(f.out/'CURRENT.journal').write_bytes(f.initial_pointer);(f.legacy_job/'events.log').write_text(f.initial_events)
  worker=f.w/'known-false-worker.sh';worker.write_text('#!/bin/sh\nset -e\n"'+str(f.session)+'" "$NX51_SESSION_ROOT" configure raw422 100\n"'+str(f.session)+'" "$NX51_SESSION_ROOT" begin front\n"'+str(f.session)+'" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART FRONT_RESTART_OR_PATH_INVALID\n')
  p=subprocess.run([str(f.session),str(f.out),'run',str(worker)],env=f.env,text=True,capture_output=True);self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(f.status()['reason'],'FRONT_RESTART_OR_PATH_INVALID');self.assertEqual(f.prepare().returncode,0);self.assertEqual(f.status()['state'],'NEED_STILL_INIT');self.assertEqual(f.status()['front'],'-');self.assertFalse(f.native_calls());self.assertTrue(f.released())
 def test_actual_uploaded_front_all_49_pinned_files_proof_only(self):
  f=self.f();source=R.parent/'demo51-field-r3/NXDEMO51/JOB_1793869199_415_10dc3936/front_1/front'
  self.assertTrue(source.is_dir());shutil.rmtree(f.report);shutil.copytree(source,f.report)
  original_manifest=sha(f.manifest);f.manifest.write_bytes((R/'base/nx-lut-lite/data/prior-front-retire.sha256').read_bytes())
  exe(f.runner,f.runner.read_text().replace('RETIRE_MANIFEST_SHA='+original_manifest,'RETIRE_MANIFEST_SHA='+sha(f.manifest)))
  f.original={str(p.relative_to(f.report)):p.read_bytes() for p in f.report.rglob('*') if p.is_file()}
  self.assertEqual(len(f.manifest.read_text().splitlines()),49);p=f.prepare();self.assertEqual(p.returncode,0,(p.stdout,p.stderr,(f.legacy_job/'RESULT.txt').read_text()));self.assertEqual(f.status()['front'],'-');self.assertEqual(f.status()['state'],'NEED_STILL_INIT');self.assertTrue(f.check_old_unchanged());self.assertFalse(f.native_calls());self.assertTrue(f.released());self.assertFalse((f.legacy_job/'jpeg').exists())
 def test_real_held_menu_listener_and_ui_flocks_are_preserved(self):
  f=self.f();held=[];inodes={}
  for name in ['menu.lock','memory-ui.lock','keys.lock','trace-ui.lock']:
   p=f.run/name;h=p.open('w');h.write('UI_FLOCK_ONLY\n');h.flush();fcntl.flock(h.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB);held.append(h);inodes[name]=p.stat().st_ino
  try:
   p=f.prepare();self.assertEqual(p.returncode,0,(p.stdout,p.stderr,(f.legacy_job/'RESULT.txt').read_text()));self.assertFalse(f.native_calls());self.assertTrue(f.released())
   for name in inodes:
    p=f.run/name;self.assertEqual(p.stat().st_ino,inodes[name]);self.assertEqual(p.read_text(),'UI_FLOCK_ONLY\n')
    with p.open('r') as other:
     with self.assertRaises(BlockingIOError):fcntl.flock(other.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
  finally:
   for h in held:h.close()
 def test_unknown_lock_regular_directory_symlink_and_invalid_ui_types_refused(self):
  for name,kind in [('unknown.lock','regular'),('unknown.lock','directory'),('unknown.lock','link'),('menu.lock','directory'),('menu.lock','link'),('memory-ui.lock','directory'),('keys.lock','link'),('trace-ui.lock','directory')]:
   with self.subTest(name=name,kind=kind):
    f=self.f();p=f.run/name
    if kind=='regular':p.write_text('FOREIGN\n')
    elif kind=='directory':p.mkdir()
    else:
     other=f.w/'foreign-lock';other.write_text('FOREIGN\n');p.symlink_to(other)
    self.assertBlocked(f,'FULL_PREPARE_OTHER_HOLD_PRESENT');self.assertTrue(p.exists() or p.is_symlink())
 def test_absent_old_own_holds_need_no_deletion(self):
  f=self.f()
  for name in ['engine.lock','trace.lock']:
   p=f.run/name
   for x in p.iterdir():x.unlink()
   p.rmdir()
  self.assertEqual(f.prepare().returncode,0);self.assertTrue(f.released());self.assertFalse(f.native_calls())
 def test_changed_any_front_proof_is_rejected(self):
  for relative in ['P49_FRONT_COMPLETE','result.json','case_00/result.json','case_00/p49_param_dma_audit.bin','case_00/post-input.txt','case_00/p39_output.y8','case_00/p39_output.uv8']:
   with self.subTest(relative=relative):
    f=self.f();p=f.report/relative;p.write_bytes(p.read_bytes()+b'changed');self.assertBlocked(f,'RETIRE_FRONT_EVIDENCE_CHANGED')
 def test_manifest_missing_changed_symlink_or_truncated_rejected(self):
  for mode in ['missing','changed','symlink']:
   with self.subTest(mode=mode):
    f=self.f()
    if mode=='missing':f.manifest.unlink()
    elif mode=='changed':f.manifest.write_text(f.manifest.read_text()+'extra\n')
    else:
     other=f.w/'manifest-copy';other.write_bytes(f.manifest.read_bytes());f.manifest.unlink();f.manifest.symlink_to(other)
    self.assertBlocked(f,'RETIRE_MANIFEST_IDENTITY_FAILED')
 def test_foreign_hold_pid_extra_file_and_symlink_are_never_removed(self):
  for mode in ['foreign_job','foreign_reason','different_boot','pid','extra','hidden','linked_marker','linked_lock']:
   with self.subTest(mode=mode):
    f=self.f();p=f.run/'engine.lock'
    if mode=='foreign_job':(p/'NX51_OWNER').write_text((p/'NX51_OWNER').read_text().replace(str(f.legacy_job),'FOREIGN'))
    elif mode=='foreign_reason':(p/'RESTART_REQUIRED').write_text((p/'RESTART_REQUIRED').read_text().replace('FRONT_RESTART_OR_PATH_INVALID','UNVERIFIED_NATIVE_STATE'))
    elif mode=='different_boot':(p/'RESTART_REQUIRED').write_text((p/'RESTART_REQUIRED').read_text().replace('boot-A','boot-B'))
    elif mode=='pid':(p/'pid').write_text('123\n')
    elif mode=='extra':(p/'extra').touch()
    elif mode=='hidden':(p/'.secret').touch()
    elif mode=='linked_marker':
     other=f.w/'owner-copy';other.write_bytes((p/'NX51_OWNER').read_bytes());(p/'NX51_OWNER').unlink();(p/'NX51_OWNER').symlink_to(other)
    else:
     other=f.w/'hold-copy';p.rename(other);p.symlink_to(other,target_is_directory=True)
    self.assertBlocked(f,'FULL_PREPARE_ENGINE_HOLD_MISMATCH')
 def test_jpeg_attempt_and_other_front_or_unknown_lock_refused(self):
  for path in ['jpeg','jpeg/attempt_1','front_2/front','unknown.lock','.unknown.lock']:
   with self.subTest(path=path):
    f=self.f();p=(f.run/path if path.endswith('.lock') else f.legacy_job/path);p.mkdir(parents=True);self.assertBlocked(f)
 def test_active_native_or_old_session_worker_refused(self):
  for name in ['nxl-demo-front.','nxl-demo-post.a','nxl-hw-controll','nxl-hw-worker.a','nxl-allocprobe.','nxl-session.arm']:
   with self.subTest(name=name):
    f=self.f();f.process(777777,name);self.assertBlocked(f,'FULL_PREPARE_NATIVE_WORKER_PRESENT')
 def test_noneligible_job_reason_event_and_input_verifier_refused(self):
  for mode in ['job','event','event_extra','event_truncated','event_seq','process_view','trace_worker','input_fail','disabled','video']:
   with self.subTest(mode=mode):
    f=self.f()
    if mode=='job':exe(f.runner,f.runner.read_text().replace('RETIRE_JOB_NAME='+f.legacy_job.name,'RETIRE_JOB_NAME=JOB_other'))
    elif mode=='event':
     p=f.legacy_job/'events.log';p.write_text(p.read_text().replace('reason=FRONT_RESTART_OR_PATH_INVALID','reason=OTHER_UNKNOWN'))
    elif mode=='event_extra':
     p=f.legacy_job/'events.log';p.write_text(p.read_text()+p.read_text().splitlines()[-1]+'\n')
    elif mode=='event_truncated':
     p=f.legacy_job/'events.log';p.write_text(p.read_text().rstrip('\n'))
    elif mode=='event_seq':
     p=f.legacy_job/'events.log';p.write_text(p.read_text().replace('seq=4','seq=5'))
    elif mode=='process_view':(f.proc/'self/status').unlink()
    elif mode=='trace_worker':(f.run/'trace-worker.pid').write_text('123\n')
    elif mode=='input_fail':f.set_mode('input_fail')
    elif mode=='disabled':(f.mod/'state/disabled').touch()
    else:(f.run/'session').mkdir();(f.run/'session/video_enabled').touch()
    self.assertBlocked(f)
 def test_postcommit_publication_failure_reholds_state_and_preserves_holds(self):
  f=self.f();mock=f.w/'mockbin';mock.mkdir();exe(mock/'cp','#!/bin/sh\ncase "$2" in */p49-experiment.state) exit 23;;esac\nexec /bin/cp "$@"\n');f.env['PATH']=str(mock)+os.pathsep+os.environ['PATH'];p=f.prepare();self.assertNotEqual(p.returncode,0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertEqual(f.status()['requires_reset'],'1');self.assertFalse(f.native_calls());self.assertFalse(f.released());self.assertIn('FULL_PREPARE_GRANT_COMMIT_FAILED',(f.legacy_job/'RESULT.txt').read_text())
 def test_postcommit_unlock_failure_reholds_state_preserving_foreign_files(self):
  f=self.f();mock=f.w/'mockbin';mock.mkdir();exe(mock/'rm','#!/bin/sh\ncase "$*" in *engine.lock/NX51_OWNER*) exit 23;;esac\nexec /bin/rm "$@"\n');f.env['PATH']=str(mock)+os.pathsep+os.environ['PATH'];p=f.prepare();self.assertNotEqual(p.returncode,0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertEqual(f.status()['requires_reset'],'1');self.assertTrue((f.run/'engine.lock/NX51_OWNER').exists());self.assertTrue((f.run/'trace.lock/NX51_OWNER').exists());self.assertFalse(f.native_calls())
 def test_spoofed_worker_without_supervisor_lease_cannot_prepare(self):
  f=self.f();env=dict(f.env,NX51_PREPARE_FULL_ONLY='1',NX51_SESSION_ROOT=str(f.out),NX51_JOB=str(f.legacy_job),NX51_SESSION_TOKEN='spoofed');p=subprocess.run(['/bin/sh',str(f.runner),'--prepare-full-worker'],env=env,capture_output=True,text=True,timeout=10);self.assertNotEqual(p.returncode,0);self.assertEqual(f.status()['state'],'RESTART_REQUIRED');self.assertFalse(f.native_calls());self.assertFalse(f.released())

if __name__=='__main__':
 suite=unittest.defaultTestLoader.loadTestsFromTestCase(PrepareFull);result=unittest.TextTestRunner(verbosity=2).run(suite)
 (R/'validation/full-prepare-tests.json').write_text(json.dumps(dict(host_model=True,hardware_tested=False,tests_run=result.testsRun,passed=result.wasSuccessful(),failures=len(result.failures),errors=len(result.errors),native_submissions_during_preparation=0,runner_sha256=sha(R/'installer/SOURCE/runner.sh.in'),manager_source_sha256=sha(R/'session/src/session.c'),manager_source_files={str(p.relative_to(R)):sha(p) for p in [R/'session/src/session.c',R/'session/src/journal.inc']},retirement_manifest_sha256=sha(R/'base/nx-lut-lite/data/prior-front-retire.sha256'),source_sha256={str(p.relative_to(R)):sha(p) for p in [R/'installer/SOURCE/runner.sh.in',R/'installer/SOURCE/test_prepare_full_r4.py',R/'installer/SOURCE/test_workflow.py',R/'installer/SOURCE/fileops.c',R/'session/src/session.c',R/'session/src/journal.inc']}),indent=2)+'\n');raise SystemExit(not result.wasSuccessful())
