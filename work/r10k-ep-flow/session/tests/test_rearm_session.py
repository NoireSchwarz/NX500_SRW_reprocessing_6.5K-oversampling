#!/usr/bin/env python3
"""Session proof authorization models only; no resource/hardware proof simulated as field evidence."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,unittest,shlex
sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_session as base
R=base.R;EXE=base.EXE
class RearmTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):base.SessionTests.setUpClass()
 def f(self):
  f=base.Fixture();self.addCleanup(f.close);return f
 def setup_complete(self,freeze=False):
  f=self.f();self.assertEqual(f.call('new').returncode,0)
  if freeze:f.env['NX51_TEST_FREEZE_MIRRORS']='1'
  t=base.FRONT.replace('mkdir "$NX51_JOB/front"','mkdir -p "$NX51_JOB/front_1/front"').replace('front-ok "$NX51_JOB/front"','front-ok "$NX51_JOB/front_1/front"')
  self.assertEqual(f.run(t).returncode,0);self.assertEqual(f.run(base.BACK).returncode,0);previous=Path(f.status()[1]['job']);self.assertEqual(f.call('new').returncode,0);current=Path(f.status()[1]['job']);self.assertEqual(f.status()[1]['reason'],'REAL_KERNEL_RESTART_NOT_VERIFIED');return f,previous,current
 def worker(self,f,alter='',after=''):
  script=f.base/'make-proof.py';script.write_text('''import os,pathlib,hashlib
j=os.environ['NX51_JOB'];previous=os.environ['NX51_PREVIOUS_JOB'];token=os.environ['NX51_SESSION_TOKEN']
assert os.environ['NX51_REARM_ONLY']=='1'
assert 'NX51_PREPARE_FULL_ONLY' not in os.environ
p=pathlib.Path(j)/('rearm_'+token);p.mkdir()
body='NX51REARMPROOF1\\njob='+j+'\\nprevious='+previous+'\\ntoken='+token+'\\nreadonly=1\\nnative_calls=0\\nresource_snapshots=3\\nold_range_free=1\\nbackend_range_free=1\\nbackend_resources_retired=1\\nnative_outcome_resolved=1\\nknown_roots_clear=1\\ncode_original=1\\nshell_parked=1\\nqueue_empty=1\\nworkers_idle=1\\nreason=EMPTY_SESSION_RESOURCE_PROOF_VERIFIED\\n'
'''+alter+'''\np.joinpath('REARM.proof').write_text(body+'sha256='+hashlib.sha256(body.encode()).hexdigest()+'\\nend=NX51REARMEND1\\n')
''')
  return f.script(shlex.quote(sys.executable)+' '+shlex.quote(str(script))+'\n"$MANAGER" "$NX51_SESSION_ROOT" rearm-fresh "$NX51_JOB/rearm_$NX51_SESSION_TOKEN"\n'+after,'rearm.sh')
 def rearm(self,f,script):return f.call('rearm',str(script),'--rearm-worker')
 def test_valid_full_resource_proof_rearms_without_old_pixels(self):
  f,previous,current=self.setup_complete();old=previous.joinpath('STATE.journal').read_bytes();p=self.rearm(f,self.worker(f));self.assertEqual(p.returncode,0,p.stderr);s=f.status()[1];self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertEqual(s['front'],'-');self.assertEqual(s['front_verified'],'0');self.assertEqual(s['front_attempt'],'0');self.assertEqual(s['backend_attempt'],'0');self.assertEqual(s['reason'],'EMPTY_SESSION_RESOURCE_PROOF_VERIFIED');self.assertIn('seq=2\n',(current/'STATE.journal').read_text());self.assertEqual(old,(previous/'STATE.journal').read_bytes());self.assertEqual(f.run(base.FRONT).returncode,0);self.assertEqual(f.status()[1]['front_attempt'],'1')
 def test_stale_previous_canonical_mirror_is_not_authority(self):
  f,previous,current=self.setup_complete(True);self.assertIn('state=NEED_STILL_INIT',(previous/'SESSION.state').read_text());self.assertEqual(self.rearm(f,self.worker(f)).returncode,0);self.assertEqual(f.status()[1]['state'],'NEED_STILL_INIT')
 def test_no_explicit_valid_receipt_keeps_empty_held(self):
  f,previous,current=self.setup_complete();before=(current/'STATE.journal').read_bytes();p=self.rearm(f,f.script('exit 0\n'));self.assertEqual(p.returncode,76);self.assertEqual(before,(current/'STATE.journal').read_bytes());self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_normal_lease_or_environment_cannot_forge_rearm_authority(self):
  f,previous,current=self.setup_complete();self.assertEqual(f.call('rearm-fresh',str(current)).returncode,77)
  g=self.f();p=g.run('export NX51_REARM_ONLY=1\n"$MANAGER" "$NX51_SESSION_ROOT" rearm-fresh "$NX51_JOB"\n');self.assertEqual(p.returncode,77)
 def test_rearm_mode_cannot_call_other_transition_or_native_stage(self):
  for cmd in ['configure raw422 100','begin front','begin backend','front-ok "$NX51_JOB"','backend-ok','fresh-front "$NX51_JOB"','new','import-front "$NX51_JOB"']:
   with self.subTest(cmd=cmd):
    f,previous,current=self.setup_complete();before=(current/'STATE.journal').read_bytes();p=self.rearm(f,f.script('"$MANAGER" "$NX51_SESSION_ROOT" '+cmd+'\n'));self.assertEqual(p.returncode,77);self.assertEqual(before,(current/'STATE.journal').read_bytes())
 def test_required_backend_context_and_unknown_resolution_not_optional(self):
  for key in ['backend_range_free','backend_resources_retired','native_outcome_resolved','old_range_free','known_roots_clear','code_original','queue_empty','workers_idle','readonly']:
   with self.subTest(key=key):
    f,previous,current=self.setup_complete();before=(current/'STATE.journal').read_bytes();p=self.rearm(f,self.worker(f,"body=body.replace("+repr(key+'=1')+","+repr(key+'=0')+")"));self.assertNotEqual(p.returncode,0);self.assertEqual(before,(current/'STATE.journal').read_bytes())
 def test_wrong_previous_or_token_receipt_cannot_rearm(self):
  for expression in ["body=body.replace('token='+token,'token='+'0'*32)","body=body.replace('previous='+previous,'previous='+j)"]:
   f,previous,current=self.setup_complete();self.assertNotEqual(self.rearm(f,self.worker(f,expression)).returncode,0);self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_missing_or_torn_pointer_history_has_no_predecessor_authority(self):
  for damaged in [False,True]:
   f,previous,current=self.setup_complete();p=f.root/'CURRENT.journal'
   if damaged:p.write_bytes(p.read_bytes()+b'NX51PTR4\nseq=3\n')
   else:p.unlink()
   marker=f.base/'worker-ran';q=self.rearm(f,f.script('touch '+shlex.quote(str(marker))+'\n'));self.assertNotEqual(q.returncode,0);self.assertFalse(marker.exists())
 def test_legacy_mirror_without_current_state_journal_is_not_eligible(self):
  f,previous,current=self.setup_complete();(current/'STATE.journal').unlink();marker=f.base/'worker-ran';self.assertNotEqual(self.rearm(f,f.script('touch '+shlex.quote(str(marker))+'\n')).returncode,0);self.assertFalse(marker.exists());self.assertEqual(f.status()[1]['state'],'RESTART_REQUIRED')
 def test_nonempty_or_nonseq1_hold_is_not_eligible(self):
  f,previous,current=self.setup_complete();f.rewrite(lambda b:b.replace('seq=1\n','seq=2\n').replace('front_attempt=0\n','front_attempt=1\n'));marker=f.base/'worker-ran';self.assertNotEqual(self.rearm(f,f.script('touch '+shlex.quote(str(marker))+'\n')).returncode,0);self.assertFalse(marker.exists())
 def test_authoritative_prior_unknown_state_rejected(self):
  f,previous,current=self.setup_complete();p=previous/'STATE.journal';parts=p.read_text().split('end=NX51END1\n');body=parts[-2].split('sha256=',1)[0].replace('state=COMPLETE\n','state=RESTART_REQUIRED\n').replace('outcome=VERIFIED_COMPLETE\n','outcome=UNKNOWN_REQUIRE_RESTART\n').replace('reason=JPEG_SAVED\n','reason=PRIOR_NATIVE_OUTCOME_UNKNOWN\n');p.write_text('end=NX51END1\n'.join(parts[:-2])+'end=NX51END1\n'+body+'sha256='+hashlib.sha256(body.encode()).hexdigest()+'\nend=NX51END1\n');marker=f.base/'worker-ran';self.assertNotEqual(self.rearm(f,f.script('touch '+shlex.quote(str(marker))+'\n')).returncode,0);self.assertFalse(marker.exists())
 def test_failure_after_rearm_commit_reholds_as_unknown(self):
  f,previous,current=self.setup_complete();p=self.rearm(f,self.worker(f,after='exit 9\n'));self.assertNotEqual(p.returncode,0);s=f.status()[1];self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['reason'],'REARM_WORKER_FAILED');self.assertEqual(s['requires_reset'],'1')
 def test_empty_hold_event_must_be_exact_new_session(self):
  f,previous,current=self.setup_complete();(current/'events.log').write_text('seq=1 event=fail state=RESTART_REQUIRED stage=idle outcome=SAFE_RELEASED_COLD reason=REAL_KERNEL_RESTART_NOT_VERIFIED\n');marker=f.base/'worker-ran';self.assertNotEqual(self.rearm(f,f.script('touch '+shlex.quote(str(marker))+'\n')).returncode,0);self.assertFalse(marker.exists())
if __name__=='__main__':
 result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RearmTests));(R/'validation/rearm-session-tests.json').write_text(json.dumps(dict(passed=result.wasSuccessful(),tests_run=result.testsRun,failures=len(result.failures),errors=len(result.errors),hardware_tested=False,host_model=True,scope='Session authorization/protocol only. Positive receipts model a fully proven pinned native probe; no camera resource safety is claimed.',source_sha256={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'src/session.c',R/'src/journal.inc',R/'src/rearm.inc']},test_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()),indent=2)+'\n');raise SystemExit(0 if result.wasSuccessful() else 1)
