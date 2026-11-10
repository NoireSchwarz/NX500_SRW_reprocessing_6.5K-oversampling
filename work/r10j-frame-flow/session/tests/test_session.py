#!/usr/bin/env python3
"""PC orchestration model: no camera devices, firmware calls, or image jobs."""
from pathlib import Path
import hashlib,json,os,signal,subprocess,tempfile,time,unittest,fcntl

R=Path(__file__).resolve().parents[1]
EXE=R/'build/session.host'
FRONT='''"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 100
"$MANAGER" "$NX51_SESSION_ROOT" begin front
mkdir "$NX51_JOB/front"
"$MANAGER" "$NX51_SESSION_ROOT" front-ok "$NX51_JOB/front"
'''
BACK='''"$MANAGER" "$NX51_SESSION_ROOT" begin backend
"$MANAGER" "$NX51_SESSION_ROOT" backend-ok
'''

class Fixture:
 def __init__(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='nx51-session-test-');self.base=Path(self.tmp.name).resolve();self.root=self.base/'store';self.env=dict(os.environ,MANAGER=str(EXE),NX51_TEST_BOOT_ID='boot-A')
 def close(self):self.tmp.cleanup()
 def call(self,*args,boot=None):
  env=dict(self.env)
  if boot:env['NX51_TEST_BOOT_ID']=boot
  return subprocess.run([str(EXE),str(self.root),*args],env=env,capture_output=True,text=True,timeout=10)
 def script(self,text,name='worker.sh'):
  p=self.base/name;p.write_text('#!/bin/sh\nset -e\n'+text);return p
 def run(self,text,boot=None):return self.call('run',str(self.script(text)),boot=boot)
 def status(self,boot=None):
  p=self.call('status',boot=boot)
  if p.returncode:return p,{}
  lines=p.stdout.splitlines();assert lines[0]=='NX51STATUS1' and lines[-1]=='end=NX51END1'
  return p,dict(s.split('=',1) for s in lines[1:] if '=' in s)
 def state_file(self):return Path(self.status()[1]['job'])/'STATE.journal'
 def front(self):
  p=self.run(FRONT);assert p.returncode==0,(p.stdout,p.stderr);return self.status()[1]
 def rewrite(self,func):
  p=self.state_file();records=p.read_text().split('end=NX51END1\n');last=func(records[-2]);body=last.split('sha256=',1)[0];p.write_text('end=NX51END1\n'.join(records[:-2])+('end=NX51END1\n' if len(records)>2 else '')+body+'sha256='+hashlib.sha256(body.encode()).hexdigest()+'\nend=NX51END1\n')


class SessionTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  subprocess.run(['cc','-O2','-DHOST_MODEL','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation',str(R/'src/session.c'),'-o',str(EXE)],check=True)
 def f(self):
  f=Fixture();self.addCleanup(f.close);return f
 def assertOK(self,p):self.assertEqual(p.returncode,0,p.stdout+p.stderr)
 def alias(self):
  f=self.f();alias=f.base/'mnt/mmc';target=f.base/'opt/storage/sdcard';alias.parent.mkdir();target.mkdir(parents=True);alias.symlink_to(target,target_is_directory=True)
  f.env.update(NX51_TEST_SD_ALIAS=str(alias),NX51_TEST_SD_TARGET=str(target));f.root=alias/'NXDEMO51';return f,alias,target
 def test_real_filesystem_oem_alias_accepts_logical_root_and_two_stages(self):
  f,alias,target=self.alias();self.assertOK(f.call('status'));self.assertFalse(f.root.exists());first=f.front();self.assertTrue((target/'NXDEMO51').is_dir());self.assertTrue(first['job'].startswith(str(alias)));self.assertOK(f.run(BACK));self.assertEqual(f.status()[1]['state'],'COMPLETE')
  (target/'NXDEMO51/logs').mkdir();self.assertOK(f.call('status'));self.assertIn('phase=ENTRY',(target/'NXDEMO51/logs/session.log').read_text());self.assertFalse((target/'NX_LUT_DEMO51_SESSION.log').exists())
 def test_oem_alias_wrong_target_refused_with_rootless_diagnostic(self):
  f,alias,target=self.alias();alias.unlink();other=f.base/'other';other.mkdir();alias.symlink_to(other,target_is_directory=True);p=f.call('new');self.assertNotEqual(p.returncode,0);self.assertIn('phase=ROOT_PARENTS result=FAILED',p.stderr);self.assertFalse((other/'NXDEMO51').exists())
 def test_oem_alias_target_symlink_refused(self):
  f,alias,target=self.alias();target.rmdir();other=f.base/'other';other.mkdir();target.symlink_to(other,target_is_directory=True);p=f.call('new');self.assertNotEqual(p.returncode,0);self.assertIn('phase=ROOT_PARENTS result=FAILED',p.stderr);self.assertFalse((other/'NXDEMO51').exists())
 def test_oem_anchor_does_not_allow_descendant_state_symlink(self):
  f,alias,target=self.alias();f.front();record=f.state_file();copy=f.base/'state-copy';record.rename(copy);record.symlink_to(copy);self.assertNotEqual(f.call('status').returncode,0)
 def test_unknown_boot_allows_fresh_front_and_backend(self):
  f=self.f();f.env['NX51_TEST_BOOT_ID']='UNKNOWN';self.assertEqual(f.front()['boot'],'UNKNOWN');self.assertOK(f.run(BACK));self.assertEqual(f.status()[1]['state'],'COMPLETE')
 def test_known_to_unknown_boot_cannot_clear_hold(self):
  f=self.f();f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART UNVERIFIED\n'));p,s=f.status(boot='UNKNOWN');self.assertOK(p);self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['kernel_boot_changed'],'0');self.assertEqual(f.run(BACK,boot='UNKNOWN').returncode,76)
 def test_unknown_to_known_boot_cannot_clear_hold(self):
  f=self.f();f.env['NX51_TEST_BOOT_ID']='UNKNOWN';f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART UNVERIFIED\n'));self.assertEqual(f.status(boot='boot-B')[1]['state'],'RESTART_REQUIRED');self.assertEqual(f.run(BACK,boot='boot-B').returncode,76)
 def test_new_unknown_boot_completion_keeps_reset_requirement(self):
  f=self.f();f.env['NX51_TEST_BOOT_ID']='UNKNOWN';f.front();self.assertOK(f.run(BACK));self.assertOK(f.call('new',boot='boot-B'));s=f.status(boot='boot-B')[1];self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['requires_reset'],'1');self.assertEqual(f.run(BACK,boot='boot-B').returncode,76)
 def legacy(self,backend=False):
  f=self.f();self.assertOK(f.call('new'));job=Path(f.status()[1]['job']);(job/'STATE.journal').unlink();(f.root/'CURRENT.journal').unlink()
  tail='seq=2 event=configure state=NEED_STILL_INIT stage=idle outcome=NONE reason=NOT_STARTED\nseq=3 event=begin state='+('BACKEND_RUNNING stage=backend' if backend else 'FRONT_RUNNING stage=front')+' outcome=RUNNING reason=TASK_STARTED\nseq=4 event=fail state=RESTART_REQUIRED stage='+('backend' if backend else 'front')+' outcome=UNKNOWN_REQUIRE_RESTART reason=FRONT_RESTART_OR_PATH_INVALID\n'
  with (job/'events.log').open('a') as out:out.write(tail)
  (job/'front_1/front').mkdir(parents=True);return f,job
 def prepare(self,f,text):
  p=f.script(text,'prepare.sh');return f.call('prepare-full',str(p),'--prepare-full-worker')
 def test_state_journal_wins_over_frozen_initial_mirrors(self):
  f=self.f();self.assertOK(f.call('new'));job=Path(f.status()[1]['job']);old=(job/'SESSION.state').read_bytes();f.env['NX51_TEST_FREEZE_MIRRORS']='1';self.assertEqual(f.front()['state'],'NEED_JPEG_INIT');self.assertEqual((job/'SESSION.state').read_bytes(),old);self.assertIn('state=NEED_STILL_INIT',(job/'SUMMARY.txt').read_text());self.assertEqual(f.status()[1]['front_attempt'],'1')
 def test_current_ledger_wins_over_frozen_current_pointer(self):
  f=self.f();self.assertOK(f.call('new'));first=f.status()[1]['job'];old=(f.root/'current.job').read_bytes();f.env['NX51_TEST_FREEZE_MIRRORS']='1';self.assertOK(f.call('new'));self.assertNotEqual(f.status()[1]['job'],first);self.assertEqual((f.root/'current.job').read_bytes(),old)
 def test_torn_state_journal_never_falls_back_to_old_NEED(self):
  f=self.f();f.front();p=f.state_file();p.write_bytes(p.read_bytes()+b'NX51STATE1\nseq=99\n');self.assertNotEqual(f.call('status').returncode,0);self.assertNotEqual(f.run('touch '+str(f.base/'bad')+'\n').returncode,0);self.assertFalse((f.base/'bad').exists())
 def test_duplicate_state_journal_sequence_rejected(self):
  f=self.f();f.front();p=f.state_file();blocks=p.read_text().split('end=NX51END1\n');p.write_text(p.read_text()+blocks[-2]+'end=NX51END1\n');self.assertNotEqual(f.call('status').returncode,0)
 def test_torn_pointer_ledger_never_falls_back_to_current_mirror(self):
  f=self.f();self.assertOK(f.call('new'));p=f.root/'CURRENT.journal';p.write_bytes(p.read_bytes()+b'NX51PTR4\nseq=2\n');self.assertNotEqual(f.call('status').returncode,0);self.assertNotEqual(f.call('new').returncode,0)
 def test_legacy_state_event_divergence_exposes_conservative_hold(self):
  f,job=self.legacy();p,s=f.status();self.assertOK(p);self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['reason'],'LEGACY_STATE_DIVERGENCE');self.assertEqual(s['front_attempt'],'1');self.assertEqual(f.run(BACK).returncode,76);self.assertNotEqual(f.call('new').returncode,0);self.assertIn('seq=1\n',(job/'SESSION.state').read_text())
 def test_legacy_backend_divergence_cannot_prepare_full(self):
  f,job=self.legacy(backend=True);self.assertEqual(f.status()[1]['backend_attempt'],'1');self.assertNotEqual(self.prepare(f,'exit 0\n').returncode,0)
 def test_prepare_full_pinned_worker_handoff_makes_NEXT_front_fresh(self):
  f,job=self.legacy();self.assertOK(self.prepare(f,'[ "$NX51_PREPARE_FULL_ONLY" = 1 ]\n"$MANAGER" "$NX51_SESSION_ROOT" fresh-front "$NX51_JOB/front_1/front"\n'));s=f.status()[1];self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0');self.assertEqual(s['front_attempt'],'1');self.assertEqual(s['reason'],'FULL_TEST_PREPARED_NO_REUSE');self.assertIn('seq=5\n',(job/'STATE.journal').read_text());self.assertTrue((job/'front_1/front').is_dir());self.assertOK(f.run(FRONT.replace('"$NX51_JOB/front"','"$NX51_JOB/front_2/front"').replace('mkdir "$NX51_JOB/front_2/front"','mkdir -p "$NX51_JOB/front_2/front"')));self.assertEqual(f.status()[1]['front_attempt'],'2')
 def test_fresh_front_mutator_needs_full_mode_lease_not_env(self):
  f,job=self.legacy();self.assertEqual(f.call('fresh-front',str(job/'front_1/front')).returncode,77)
  g=self.f();p=g.run('export NX51_PREPARE_FULL_ONLY=1\n"$MANAGER" "$NX51_SESSION_ROOT" fresh-front "$NX51_JOB/front_1/front"\n');self.assertNotEqual(p.returncode,0)
 def test_prepare_full_lease_blocks_native_related_mutators(self):
  for cmd in ['configure raw422 100','begin front','begin backend','front-ok "$NX51_JOB/front_1/front"','new','import-front "$NX51_JOB/front_1/front"']:
   with self.subTest(cmd=cmd):
    f,job=self.legacy();self.assertNotEqual(self.prepare(f,'"$MANAGER" "$NX51_SESSION_ROOT" '+cmd+'\n').returncode,0);self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_prepare_full_unknown_other_reason_stays_held(self):
  f=self.f();f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART REAL_UNKNOWN\n'));self.assertNotEqual(self.prepare(f,'exit 0\n').returncode,0);self.assertEqual(f.status()[1]['reason'],'REAL_UNKNOWN')
 def test_prepare_full_worker_exit_after_clear_reholds_failure(self):
  f,job=self.legacy();self.assertNotEqual(self.prepare(f,'"$MANAGER" "$NX51_SESSION_ROOT" fresh-front "$NX51_JOB/front_1/front"\nexit 9\n').returncode,0);self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED');self.assertEqual(f.status()[1]['reason'],'FULL_PREPARE_WORKER_FAILED')
 def test_prepare_full_no_explicit_clear_is_not_success(self):
  f,job=self.legacy();self.assertNotEqual(self.prepare(f,'exit 0\n').returncode,0);self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_status_does_not_create_missing_lock_or_state_files(self):
  f=self.f();f.root.mkdir();self.assertOK(f.call('status'));self.assertEqual(list(f.root.iterdir()),[])
 def test_status_waits_shared_lock_until_append_record_complete(self):
  f=self.f();s=f.front();job=Path(s['job']);journal=job/'STATE.journal';text=journal.read_text();last=text.split('end=NX51END1\n')[-2];body=last.split('sha256=',1)[0];oldseq=int(next(x[4:] for x in body.splitlines() if x.startswith('seq=')));body=body.replace('seq='+str(oldseq)+'\n','seq='+str(oldseq+1)+'\n',1).replace('reason=FRONT_VERIFIED_PREPARE_JPEG','reason=STATUS_AFTER_APPEND');record=(body+'sha256='+hashlib.sha256(body.encode()).hexdigest()+'\nend=NX51END1\n').encode()
  fd=os.open(f.root/'state.lock',os.O_RDWR);fcntl.flock(fd,fcntl.LOCK_EX);writer=journal.open('ab');half=len(record)//2;writer.write(record[:half]);writer.flush();os.fsync(writer.fileno())
  proc=subprocess.Popen([str(EXE),str(f.root),'status'],env=f.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   time.sleep(.08);self.assertIsNone(proc.poll(),'status read an incomplete append without locking');writer.write(record[half:]);writer.flush();os.fsync(writer.fileno());writer.close();fcntl.flock(fd,fcntl.LOCK_UN);os.close(fd);fd=-1
   out,err=proc.communicate(timeout=3);self.assertEqual(proc.returncode,0,out+err);self.assertIn('reason=STATUS_AFTER_APPEND',out);self.assertNotIn('INVALID_SESSION_RECORD',err)
  finally:
   if not writer.closed:writer.close()
   if fd>=0:fcntl.flock(fd,fcntl.LOCK_UN);os.close(fd)
   if proc.poll() is None:proc.kill();proc.wait(timeout=3)
 def test_legacy_valid_front_cannot_submit_backend_or_bypass_new(self):
  for keep_pointer_ledger in [False,True]:
   with self.subTest(pointer_ledger=keep_pointer_ledger):
    f=self.f();old=f.front();job=Path(old['job']);mirror=(job/'SESSION.state').read_bytes();(job/'STATE.journal').unlink()
    if not keep_pointer_ledger:(f.root/'CURRENT.journal').unlink()
    p,s=f.status();self.assertOK(p);self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['reason'],'LEGACY_FRONT_REQUIRES_FRESH');self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0');self.assertEqual(f.run(BACK).returncode,76);self.assertNotEqual(f.call('new').returncode,0);self.assertEqual((job/'SESSION.state').read_bytes(),mirror);self.assertTrue(Path(old['front']).is_dir())
 def test_new_after_proven_reset_of_legacy_front_still_requires_new_SRW(self):
  f=self.f();old=f.front();job=Path(old['job']);(job/'STATE.journal').unlink();(f.root/'CURRENT.journal').unlink();self.assertOK(f.call('new',boot='boot-B'));s=f.status(boot='boot-B')[1];self.assertNotEqual(s['job'],old['job']);self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0');self.assertEqual(s['front_attempt'],'0');self.assertTrue(Path(old['front']).is_dir());self.assertNotEqual(f.run(BACK,boot='boot-B').returncode,0)
 def test_readonly_status_does_not_create_root(self):
  f=self.f();p,s=f.status();self.assertOK(p);self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertFalse(f.root.exists())
 def test_two_stages_share_one_job_and_durable_summary(self):
  f=self.f();first=f.front();self.assertEqual(first['state'],'NEED_JPEG_INIT');self.assertOK(f.run(BACK));last=f.status()[1];self.assertEqual(last['state'],'COMPLETE');self.assertEqual(first['job'],last['job']);self.assertEqual(last['backend_attempt'],'1');self.assertEqual(len(list(f.root.glob('JOB_*'))),1);self.assertIn('state=COMPLETE',Path(last['job'],'SUMMARY.txt').read_text())
 def test_import_front_disabled_for_fresh_only_pipeline(self):
  f=self.f();old=f.base/'old/DEMO_older/front';old.mkdir(parents=True)
  self.assertNotEqual(f.run('"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 100\n"$MANAGER" "$NX51_SESSION_ROOT" import-front '+str(old)+'\n').returncode,0);s=f.status()[1];self.assertEqual(s['front'],'-');self.assertEqual(s['front_attempt'],'0')
 def test_safe_zero_submit_failure_is_retryable_and_does_not_increment_task(self):
  f=self.f();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" fail SAFE_NO_SUBMIT INPUT_INVALID\n'));s=f.status()[1];self.assertEqual(s['state'],'RETRYABLE');self.assertEqual(s['front_attempt'],'0');self.assertEqual(s['requires_reset'],'0')
 def test_safe_released_backend_failure_can_explicitly_retry(self):
  f=self.f();f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail SAFE_RELEASED CLEAN_FAILURE\n'));self.assertEqual(f.status()[1]['state'],'RETRYABLE');self.assertOK(f.run(BACK));self.assertEqual(f.status()[1]['backend_attempt'],'2')
 def test_unknown_outcome_blocks_same_boot(self):
  f=self.f();f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART UNVERIFIED\n'));s=f.status()[1];self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['next_action'],'REAL_KERNEL_RESTART_NOT_VERIFIED');self.assertEqual(f.run(BACK).returncode,76)
 def test_safe_released_cold_is_still_restart_required(self):
  f=self.f();f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail SAFE_RELEASED_COLD NEED_RESET\n'));self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED');self.assertEqual(f.status()[1]['outcome'] if 'outcome' in f.status()[1] else 'SAFE_RELEASED_COLD','SAFE_RELEASED_COLD');self.assertNotEqual(f.call('new').returncode,0)
 def test_duplicate_safe_cleanup_cannot_downgrade_unknown_outcome(self):
  f=self.f();f.front();p=f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART UNVERIFIED\n"$MANAGER" "$NX51_SESSION_ROOT" fail SAFE_NO_SUBMIT DUPLICATE_CLEANUP\n');self.assertNotEqual(p.returncode,0);s=f.status()[1];self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['requires_reset'],'1');self.assertEqual(s['outcome'],'UNKNOWN_REQUIRE_RESTART')
 def test_real_changed_kernel_epoch_reconciles_verified_cache(self):
  f=self.f();f.front();self.assertOK(f.run('"$MANAGER" "$NX51_SESSION_ROOT" begin backend\n"$MANAGER" "$NX51_SESSION_ROOT" fail UNKNOWN_REQUIRE_RESTART UNVERIFIED\n'));s=f.status(boot='boot-B')[1];self.assertEqual(s['state'],'NEED_JPEG_INIT');self.assertEqual(s['requires_reset'],'0');self.assertOK(f.run(BACK,boot='boot-B'));self.assertEqual(f.status(boot='boot-B')[1]['state'],'COMPLETE')
 def test_new_after_complete_is_fresh_but_cannot_bypass_reset(self):
  f=self.f();old=f.front();self.assertOK(f.run(BACK));self.assertOK(f.call('new'));s=f.status()[1];self.assertNotEqual(s['job'],old['job']);self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0');self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(f.run(BACK).returncode,76)
 def test_new_after_true_restart_requires_fresh_SRW_and_resets_metadata(self):
  f=self.f();old=f.front();self.assertOK(f.run(BACK));self.assertOK(f.call('new',boot='boot-B'));s=f.status(boot='boot-B')[1];self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertEqual(s['look_id'],'-');self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0')
 def test_complete_run_is_status_only_no_automatic_task(self):
  f=self.f();f.front();self.assertOK(f.run(BACK));p=f.run('touch '+str(f.base/'unexpected')+'\n');self.assertOK(p);self.assertFalse((f.base/'unexpected').exists())
 def test_worker_exit_after_begin_gets_terminal_unknown_immediately(self):
  f=self.f();p=f.run('"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 100\n"$MANAGER" "$NX51_SESSION_ROOT" begin front\nexit 7\n');self.assertEqual(p.returncode,7);s=f.status()[1];self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertIn('WORKER_EXIT_WITHOUT_TERMINAL_COMMIT',Path(s['job'],'SUMMARY.txt').read_text())
 def test_zero_exit_without_terminal_commit_is_not_success(self):
  f=self.f();p=f.run('"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 100\n"$MANAGER" "$NX51_SESSION_ROOT" begin front\nexit 0\n');self.assertEqual(p.returncode,76);self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_supervisor_signal_is_durable_unknown(self):
  f=self.f();script=f.script('trap "exit 143" TERM\n"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 100\n"$MANAGER" "$NX51_SESSION_ROOT" begin front\nprintf ready > '+str(f.base/'ready')+'\nwhile :;do sleep .05;done\n')
  proc=subprocess.Popen([str(EXE),str(f.root),'run',str(script)],env=f.env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  self.addCleanup(lambda:proc.poll() is None and proc.kill())
  for _ in range(100):
   if (f.base/'ready').exists():break
   time.sleep(.01)
  self.assertTrue((f.base/'ready').exists());proc.terminate();self.assertNotEqual(proc.wait(timeout=3),0);self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_concurrent_run_is_busy(self):
  f=self.f();script=f.script('printf ready > '+str(f.base/'ready')+'\nsleep .4\n')
  proc=subprocess.Popen([str(EXE),str(f.root),'run',str(script)],env=f.env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  for _ in range(100):
   if (f.base/'ready').exists():break
   time.sleep(.01)
  self.assertEqual(f.call('new').returncode,75);self.assertEqual(f.run('exit 0\n').returncode,75);self.assertEqual(proc.wait(timeout=3),0)
 def test_killed_supervisor_zombie_cannot_authorize_new_worker(self):
  f=self.f();script=f.script('trap "exit 143" TERM\n"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 100\n"$MANAGER" "$NX51_SESSION_ROOT" begin front\nprintf "%s\\n" "$$" > '+str(f.base/'childpid')+'\nprintf ready > '+str(f.base/'ready')+'\nwhile :;do sleep .05;done\n')
  proc=subprocess.Popen([str(EXE),str(f.root),'run',str(script)],env=f.env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  child=None
  try:
   for _ in range(100):
    if (f.base/'ready').exists():break
    time.sleep(.01)
   self.assertTrue((f.base/'ready').exists());child=int((f.base/'childpid').read_text())
   os.kill(proc.pid,signal.SIGKILL);time.sleep(.05) # Deliberately do not reap it yet.
   p=f.run('touch '+str(f.base/'must_not_run')+'\n');self.assertEqual(p.returncode,76);self.assertFalse((f.base/'must_not_run').exists());self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED');self.assertIn('ORPHANED_RUNNING_STATE',f.state_file().read_text())
  finally:
   if child and self.alive(child):os.kill(child,signal.SIGTERM)
   if proc.poll() is None:proc.kill()
   proc.wait(timeout=3)
 def test_exec_descendant_does_not_inherit_run_flock(self):
  f=self.f();p=f.run('sleep 5 >/dev/null 2>&1 </dev/null &\nprintf "%s\\n" "$!" > '+str(f.base/'orphan')+'\n');self.assertOK(p);pid=int((f.base/'orphan').read_text());self.addCleanup(lambda:os.kill(pid,signal.SIGTERM) if self.alive(pid) else None);self.assertOK(f.call('new'))
 @staticmethod
 def alive(pid):
  try:os.kill(pid,0);return True
  except ProcessLookupError:return False
 def test_unauthorized_transition_rejected(self):
  f=self.f();self.assertOK(f.call('new'));before=f.state_file().read_bytes();self.assertEqual(f.call('begin','front').returncode,77);self.assertEqual(f.state_file().read_bytes(),before)
 def test_wrong_fixed_pipeline_metadata_rejected(self):
  f=self.f();self.assertNotEqual(f.run('"$MANAGER" "$NX51_SESSION_ROOT" configure kodak_portra400 100\n').returncode,0);self.assertNotEqual(f.run('"$MANAGER" "$NX51_SESSION_ROOT" configure raw422 50\n').returncode,0)
 def test_truncated_record_refused(self):
  f=self.f();f.front();p=f.state_file();p.write_bytes(p.read_bytes()[:-12]);self.assertNotEqual(f.call('status').returncode,0);self.assertNotEqual(f.run('exit 0\n').returncode,0)
 def test_duplicate_field_with_recomputed_hash_refused(self):
  f=self.f();f.front();f.rewrite(lambda s:s.replace('state=NEED_JPEG_INIT\n','state=NEED_JPEG_INIT\nstate=COMPLETE\n'));self.assertNotEqual(f.call('status').returncode,0)
 def test_invalid_state_with_recomputed_hash_refused(self):
  f=self.f();f.front();f.rewrite(lambda s:s.replace('state=NEED_JPEG_INIT','state=UNSAFE_SKIP'));self.assertNotEqual(f.call('status').returncode,0)
 def test_uint32_truncation_with_recomputed_hash_refused(self):
  f=self.f();f.front();f.rewrite(lambda s:s.replace('front_verified=1','front_verified=4294967297'));self.assertNotEqual(f.call('status').returncode,0)
 def test_symlinked_state_refused(self):
  f=self.f();f.front();p=f.state_file();copy=f.base/'copy';p.rename(copy);p.symlink_to(copy);self.assertNotEqual(f.call('status').returncode,0)
 def test_checksum_damage_refused(self):
  f=self.f();f.front();p=f.state_file();p.write_text(p.read_text().replace('FRONT_VERIFIED_PREPARE_JPEG','FRONT_UNVERIFIED_PREPARE_JPEG'));self.assertNotEqual(f.call('status').returncode,0)
 def test_stale_atomic_temp_does_not_block_next_commit(self):
  f=self.f();s=f.front();Path(s['job'],'.SESSION.state.999.tmp').write_text('stale');self.assertOK(f.run(BACK))
 def test_status_does_not_mutate_record(self):
  f=self.f();f.front();p=f.state_file();before=p.read_bytes();self.assertOK(f.call('status'));self.assertEqual(p.read_bytes(),before)

if __name__=='__main__':
 suite=unittest.defaultTestLoader.loadTestsFromTestCase(SessionTests);result=unittest.TextTestRunner(verbosity=2).run(suite)
 out=R/'validation';out.mkdir(exist_ok=True)
 (out/'session-tests.json').write_text(json.dumps(dict(hardware_tested=False,host_model=True,process_start_ticks_model='kill0 liveness only because sandbox has no /proc; production reads pid start_ticks',tests_run=result.testsRun,passed=result.wasSuccessful(),failures=len(result.failures),errors=len(result.errors),source_sha256=hashlib.sha256((R/'src/session.c').read_bytes()).hexdigest(),source_files={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'src/session.c',R/'src/journal.inc',R/'src/rearm.inc']}),indent=2)+'\n')
 raise SystemExit(0 if result.wasSuccessful() else 1)
