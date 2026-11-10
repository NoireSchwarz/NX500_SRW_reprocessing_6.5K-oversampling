#!/usr/bin/env python3
"""Real HOST supervisor/fileops; synthetic proof producer, never camera evidence."""
from pathlib import Path
import fcntl, hashlib, io, json, os, shlex, shutil, signal, subprocess, sys, time, unittest

R = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(R / 'session/tests'))
import test_session as session

BUILD = R / 'validation/rearm-wrapper-build'
EXE = BUILD / 'session.host'
OPS = BUILD / 'fileops.host'
WRAPPER = R / 'base/nx-lut-lite/rearm-session.sh'
session.EXE = EXE

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

PROBE = r'''import fcntl, hashlib, json, os, pathlib, signal, sys, time
args = sys.argv[1:]
j=os.environ['NX51_JOB']; old=os.environ['NX51_PREVIOUS_JOB']; t=os.environ['NX51_SESSION_TOKEN']
assert args == ['--job',j,'--previous',old,'--token',t,'--output',j+'/rearm_'+t]
assert os.environ['NX51_REARM_ONLY']=='1' and 'NX51_PREPARE_FULL_ONLY' not in os.environ
base=pathlib.Path(os.environ['HOST_WRAPPER_BASE'])
base.joinpath('probe-argv.json').write_text(json.dumps(args))
try:
    fields=dict(x.split('=',1) for x in pathlib.Path(os.environ['NX51_SESSION_ROOT'],'active.lease').read_text().splitlines())
    assert fields['token']==t and fields['mode']=='rearm'
    os.kill(int(fields['pid']),0)
    fd=os.open(pathlib.Path(os.environ['NX51_SESSION_ROOT'],'run.lock'),os.O_RDWR)
    try:
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        raise AssertionError('run lock is not held by the supervisor')
    except BlockingIOError: pass
    finally: os.close(fd)
except (OSError,AssertionError,ValueError,KeyError): sys.exit(62)
MODE_BODY
'''

PROOF = r'''p=pathlib.Path(j)/('rearm_'+t);p.mkdir()
body='NX51REARMPROOF1\njob='+j+'\nprevious='+old+'\ntoken='+t+'\nreadonly=1\nnative_calls=0\nresource_snapshots=3\nold_range_free=1\nbackend_range_free=1\nbackend_resources_retired=1\nnative_outcome_resolved=1\nknown_roots_clear=1\ncode_original=1\nshell_parked=1\nqueue_empty=1\nworkers_idle=1\nreason=EMPTY_SESSION_RESOURCE_PROOF_VERIFIED\n'
ALTER_BODY
p.joinpath('REARM.proof').write_text(body+'sha256='+hashlib.sha256(body.encode()).hexdigest()+'\nend=NX51REARMEND1\n')
'''

class Fixture(session.Fixture):
    def __init__(self, probe='success', alter='', manager=''):
        super().__init__()
        self.sd=self.base/'sd';self.sd.mkdir();self.root=self.sd/'NXDEMO51'
        self.d=self.base/'module';(self.d/'bin').mkdir(parents=True);(self.d/'state').mkdir()
        self.proc=self.base/'proc';(self.proc/'self').mkdir(parents=True)
        (self.proc/'self/status').write_text('Name:\tsh\n')
        self.env.update(HOST_WRAPPER_BASE=str(self.base),NXL_PROGRESS_SD=str(self.sd))
        self.path=self.base/'path';self.path.mkdir()
        (self.path/'sync').write_text('#!/bin/sh\n# HOST durability publication model only\nexit 0\n');(self.path/'sync').chmod(0o755)
        self.env['PATH']=str(self.path)+':'+os.environ['PATH']
        shutil.copyfile(OPS,self.d/'bin/nxl-fileops');(self.d/'bin/nxl-fileops').chmod(0o755)
        self.manager=self.d/'bin/nxl-session.arm'
        self.manager.write_text('#!/bin/sh\n'+manager+'\nexec '+shlex.quote(str(EXE))+' "$@"\n');self.manager.chmod(0o755)
        mode={
            'success':PROOF.replace('ALTER_BODY',alter),
            'failure':"sys.exit(5)\n",
            'no-proof':"sys.exit(0)\n",
            'slow':"signal.signal(signal.SIGTERM,lambda *_:(base.joinpath('probe-stopped').write_text('TERM'),sys.exit(143)))\nbase.joinpath('probe-ready').write_text(str(os.getpid()))\nwhile True: time.sleep(.02)\n",
            'popup':"time.sleep(.15)\nsys.exit(5)\n",
        }[probe]
        self.probe=self.d/'bin/nxl-session-rearm.arm'
        self.probe.write_text('#!'+sys.executable+'\n'+PROBE.replace('MODE_BODY',mode));self.probe.chmod(0o755)
        self.menu=self.d/'bin/nxl-menu'
        self.menu.write_text('#!'+sys.executable+'\nimport os,pathlib,signal,time\nb=pathlib.Path(os.environ["HOST_WRAPPER_BASE"])\nsignal.signal(signal.SIGTERM,lambda *_:(b.joinpath("popup-stopped").write_text("TERM"),exit(0)))\nb.joinpath("popup-ready").write_text(str(os.getpid()))\nwhile True: time.sleep(.02)\n');self.menu.chmod(0o755)
        (self.d/'process.sh').write_text((R/'base/nx-lut-lite/process.sh').read_text().replace('/proc/',str(self.proc)+'/'))
        shutil.copyfile(R/'base/nx-lut-lite/progress.sh',self.d/'progress.sh')
        self.script_path=self.d/'rearm-session.sh'
        self.render()
    def render(self):
        text=WRAPPER.read_text().replace('OUT=/mnt/mmc/NXDEMO51','OUT='+shlex.quote(str(self.root)))
        text=text.replace('OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d','OPS_SHA='+sha(self.d/'bin/nxl-fileops'))
        for token,p in [('SESSION',self.manager),('PROBE',self.probe),('MENU',self.menu)]:
            text=text.replace('@DEMO51_'+('REARM' if token=='PROBE' else token)+'_SHA256@',sha(p))
        self.script_path.write_text(text)
    def setup_held(self):
        assert self.call('new').returncode==0
        front=session.FRONT.replace('mkdir "$NX51_JOB/front"','mkdir -p "$NX51_JOB/front_1/front"').replace('front-ok "$NX51_JOB/front"','front-ok "$NX51_JOB/front_1/front"')
        assert self.run(front).returncode==0
        assert self.run(session.BACK).returncode==0
        self.previous=Path(self.status()[1]['job'])
        assert self.call('new').returncode==0
        self.job=Path(self.status()[1]['job'])
        self.before=(self.job/'STATE.journal').read_bytes()
        self.old_before=(self.previous/'STATE.journal').read_bytes()
        self.pointer_before=(self.root/'CURRENT.journal').read_bytes()
    def invoke(self,*args,env=None):
        return subprocess.run(['/bin/sh',str(self.script_path),*(args or ('--rearm',))],env=env or self.env,capture_output=True,text=True,timeout=10)
    def worker_env(self,**extra):
        return dict(self.env,NX51_REARM_ONLY='1',NX51_SESSION_ROOT=str(self.root),NX51_JOB=str(self.job),NX51_PREVIOUS_JOB=str(self.previous),NX51_SESSION_TOKEN='a'*32,**extra)
    def unchanged(self):
        return self.before==(self.job/'STATE.journal').read_bytes() and self.old_before==(self.previous/'STATE.journal').read_bytes() and self.pointer_before==(self.root/'CURRENT.journal').read_bytes()
    def log(self):return (self.root/'logs/rearm.log').read_text()
    def progress(self):return (self.root/'logs/status.txt').read_text()

class WrapperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        BUILD.mkdir(parents=True,exist_ok=True)
        subprocess.run(['cc','-O2','-DHOST_MODEL','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation',str(R/'session/src/session.c'),'-o',str(EXE)],check=True)
        subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function',str(R/'installer/SOURCE/fileops.c'),'-o',str(OPS)],check=True)
    def f(self,**kw):
        f=Fixture(**kw);self.addCleanup(f.close);f.setup_held();return f
    def held(self,f,p):
        self.assertNotEqual(p.returncode,0,p.stdout+p.stderr);self.assertTrue(f.unchanged());self.assertNotIn('REARM_READY',f.log())
    def wait_file(self,path):
        for _ in range(300):
            if path.exists():return
            time.sleep(.01)
        self.fail('Timed out waiting for '+str(path))
    def test_real_supervisor_and_complete_host_proof_commit_same_fresh_job(self):
        f=self.f();p=f.invoke();self.assertEqual(p.returncode,0,p.stdout+p.stderr+f.log());s=f.status()[1]
        self.assertEqual(s['state'],'NEED_STILL_INIT');self.assertEqual(s['reason'],'EMPTY_SESSION_RESOURCE_PROOF_VERIFIED');self.assertEqual(s['front'],'-');self.assertEqual(s['front_attempt'],'0');self.assertEqual(s['backend_attempt'],'0');self.assertIn('seq=2\n',(f.job/'STATE.journal').read_text());self.assertEqual(f.old_before,(f.previous/'STATE.journal').read_bytes());self.assertEqual(f.pointer_before,(f.root/'CURRENT.journal').read_bytes());self.assertIn('state=READY',f.progress());self.assertFalse((f.root/'active.lease').exists())
        args=json.loads((f.base/'probe-argv.json').read_text());self.assertEqual(args[0:4],['--job',str(f.job),'--previous',str(f.previous)]);self.assertEqual(args[-1],str(f.job)+'/rearm_'+args[5]);self.assertEqual(len(args),8)
    def test_probe_failure_preserves_retry_eligible_seq1_and_old_journal(self):
        f=self.f(probe='failure');p=f.invoke();self.held(f,p);self.assertEqual(p.returncode,5);self.assertIn('state=HELD',f.progress());self.assertNotIn('event=fail',(f.job/'events.log').read_text());self.assertFalse((f.root/'active.lease').exists())
    def test_zero_probe_exit_without_receipt_is_not_ready(self):
        f=self.f(probe='no-proof');self.held(f,f.invoke());self.assertIn('REARM_PROOF_COMMIT_REJECTED',f.log())
    def test_partial_or_unresolved_resource_proof_cannot_commit(self):
        for key in ['backend_resources_retired','native_outcome_resolved','old_range_free','workers_idle']:
            with self.subTest(key=key):
                f=self.f(alter="body=body.replace('"+key+"=1','"+key+"=0')");self.held(f,f.invoke())
    def test_proof_for_wrong_token_cannot_commit(self):
        f=self.f(alter="body=body.replace('token='+t,'token='+'0'*32)");self.held(f,f.invoke())
    def test_proof_for_wrong_job_cannot_commit(self):
        f=self.f(alter="body=body.replace('job='+j,'job='+old)");self.held(f,f.invoke())
    def test_proof_corrupt_checksum_cannot_commit(self):
        f=self.f();f.probe.write_text(f.probe.read_text().replace('hashlib.sha256(body.encode()).hexdigest()',"('0'*64)"));f.render();self.held(f,f.invoke());self.assertIn('REARM_PROOF_COMMIT_REJECTED',f.log())
    def test_pinned_binary_identity_tamper_rejects_before_probe(self):
        for name in ['nxl-fileops','nxl-session.arm','nxl-session-rearm.arm','nxl-menu']:
            with self.subTest(name=name):
                f=self.f();path=f.d/'bin'/name;path.write_bytes(path.read_bytes()+b'\n# tampered\n');self.held(f,f.invoke());self.assertFalse((f.base/'probe-argv.json').exists())
    def test_symlinked_pinned_component_rejected(self):
        f=self.f();m=f.menu.rename(f.base/'menu-copy');f.menu.symlink_to(m);self.held(f,f.invoke());self.assertIn('MENU_IDENTITY_FAILED',f.log())
    def test_missing_process_helper_rejected(self):
        f=self.f();(f.d/'process.sh').unlink();self.held(f,f.invoke());self.assertIn('PROCESS_HELPER_MISSING',f.log())
    def test_factory_daemon_blocks_rearm(self):
        f=self.f();(f.proc/'600').mkdir();(f.proc/'600/status').write_text('Name:\tdfmsd\n');self.held(f,f.invoke());self.assertIn('FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE',f.log())
    def test_unavailable_process_view_blocks_rearm(self):
        f=self.f();(f.proc/'self/status').unlink();self.held(f,f.invoke())
    def test_disabled_marker_or_broken_symlink_blocks_rearm(self):
        for link in [False,True]:
            f=self.f();path=f.d/'state/disabled'
            if link:path.symlink_to(f.base/'absent')
            else:path.write_text('disabled')
            self.held(f,f.invoke());self.assertIn('MODULE_DISABLED',f.log())
    def test_worker_without_supervisor_environment_rejected(self):
        f=self.f();self.held(f,f.invoke('--rearm-worker'));self.assertIn('REARM_LEASE_REQUIRED',f.log());self.assertFalse((f.base/'probe-argv.json').exists())
    def test_forged_worker_environment_is_not_live_lease(self):
        f=self.f();self.held(f,f.invoke('--rearm-worker',env=f.worker_env()));self.assertIn('probe_exit=62',f.log());self.assertNotIn('REARM_PROOF_COMMIT_REJECTED',f.log())
    def test_wrong_root_or_mode_rejects_worker(self):
        for key,value in [('NX51_SESSION_ROOT','/different'),('NX51_REARM_ONLY','0')]:
            f=self.f();env=f.worker_env();env[key]=value;self.held(f,f.invoke('--rearm-worker',env=env));self.assertIn('REARM_LEASE_REQUIRED',f.log())
    def test_job_traversal_nested_empty_and_long_names_rejected(self):
        for suffix in ['JOB_../escape','JOB_x/../../escape','JOB_','JOB_'+('x'*161),'JOB_x.other']:
            f=self.f();env=f.worker_env();env['NX51_JOB']=str(f.root)+'/'+suffix;self.held(f,f.invoke('--rearm-worker',env=env));self.assertIn('INVALID_JOB',f.log());self.assertFalse((f.base/'probe-argv.json').exists())
    def test_job_leaf_symlink_rejected(self):
        f=self.f();alias=f.root/'JOB_alias';alias.symlink_to(f.job,target_is_directory=True);env=f.worker_env();env['NX51_JOB']=str(alias);self.held(f,f.invoke('--rearm-worker',env=env));self.assertIn('INVALID_JOB',f.log())
    def test_previous_same_nested_or_symlink_rejected(self):
        for mode in ['same','nested','link']:
            f=self.f();env=f.worker_env()
            if mode=='same':env['NX51_PREVIOUS_JOB']=str(f.job)
            elif mode=='nested':env['NX51_PREVIOUS_JOB']=str(f.previous)+'/..'
            else:
                alias=f.root/'JOB_oldalias';alias.symlink_to(f.previous,target_is_directory=True);env['NX51_PREVIOUS_JOB']=str(alias)
            self.held(f,f.invoke('--rearm-worker',env=env));self.assertIn('INVALID_PREVIOUS_JOB',f.log())
    def test_invalid_token_rejects_before_probe(self):
        for token in ['a'*31,'A'*32,'a'*31+'/','a'*33]:
            f=self.f();env=f.worker_env();env['NX51_SESSION_TOKEN']=token;self.held(f,f.invoke('--rearm-worker',env=env));self.assertIn('INVALID_LEASE_TOKEN',f.log())
    def test_existing_report_regular_directory_or_broken_link_never_overwritten(self):
        for kind in ['file','directory','link']:
            f=self.f();proof=f.job/('rearm_'+'a'*32)
            if kind=='file':proof.write_bytes(b'preserve-report')
            elif kind=='directory':proof.mkdir();(proof/'prior').write_bytes(b'preserve-proof')
            else:proof.symlink_to(f.base/'nonexistent')
            self.held(f,f.invoke('--rearm-worker',env=f.worker_env()));self.assertIn('REARM_REPORT_ALREADY_EXISTS',f.log());self.assertFalse((f.base/'probe-argv.json').exists())
    def test_unknown_and_extra_argv_do_not_invoke_probe(self):
        for args in [('--unknown',),('--rearm','extra'),('--rearm-worker','extra')]:
            f=self.f();self.held(f,f.invoke(*args));self.assertFalse((f.base/'probe-argv.json').exists())
    def test_commit_rejection_never_ready_or_session_fail(self):
        f=self.f(manager='if [ "${2:-}" = rearm-fresh ];then exit 71;fi');self.held(f,f.invoke());self.assertIn('REARM_PROOF_COMMIT_REJECTED',f.log());self.assertNotIn('event=fail',(f.job/'events.log').read_text())
    def test_probe_cancellation_reaped_no_commit_and_same_seq1(self):
        f=self.f(probe='slow');p=subprocess.Popen(['/bin/sh',str(f.script_path),'--rearm'],env=f.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            self.wait_file(f.base/'probe-ready');p.terminate();out,err=p.communicate(timeout=5);self.assertNotEqual(p.returncode,0,out+err);self.assertTrue(f.unchanged());self.assertTrue((f.base/'probe-stopped').exists());self.assertIn('state=HELD',f.progress());self.assertNotIn('REARM_READY',f.log());self.assertFalse((f.root/'active.lease').exists())
        finally:
            if p.poll() is None:p.kill();p.wait(timeout=3)
    def test_cancel_during_atomic_commit_supervisor_reholds_not_ready(self):
        manager='if [ "${2:-}" = rearm-fresh ];then printf ready > "$HOST_WRAPPER_BASE/commit-ready"; sleep .15;fi'
        f=self.f(manager=manager);p=subprocess.Popen(['/bin/sh',str(f.script_path),'--rearm'],env=f.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            self.wait_file(f.base/'commit-ready');p.terminate();p.communicate(timeout=5);self.assertNotEqual(p.returncode,0);s=f.status()[1];self.assertEqual(s['state'],'RESTART_REQUIRED');self.assertEqual(s['reason'],'REARM_WORKER_FAILED');self.assertEqual(s['requires_reset'],'1');self.assertNotIn('REARM_READY',f.log())
        finally:
            if p.poll() is None:p.kill();p.wait(timeout=3)
    def test_popup_reaped_on_probe_failure(self):
        f=self.f(probe='popup');self.held(f,f.invoke());self.assertTrue((f.base/'popup-ready').exists());self.assertTrue((f.base/'popup-stopped').exists())
    def test_hup_cancellation_preserves_proof_failure_retry(self):
        f=self.f(probe='slow');p=subprocess.Popen(['/bin/sh',str(f.script_path),'--rearm'],env=f.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            self.wait_file(f.base/'probe-ready');p.send_signal(signal.SIGHUP);p.communicate(timeout=5);self.assertNotEqual(p.returncode,0);self.assertTrue(f.unchanged());self.assertTrue((f.base/'probe-stopped').exists());self.assertNotIn('REARM_READY',f.log())
        finally:
            if p.poll() is None:p.kill();p.wait(timeout=3)
    def test_wrapper_preserves_old_holds_and_cap_proof_bytes(self):
        f=self.f(probe='failure');paths=[]
        for name in ['engine.lock','trace.lock']:
            lock=f.d/'state'/name;lock.mkdir();p=lock/'RESTART_REQUIRED';p.write_bytes(b'UNKNOWN_NATIVE_OWNER\x00\n');paths.append(p)
        cap=f.previous/'retained-cap.bin';cap.write_bytes(b'CAP_PROOF_NOT_AN_INPUT\x00\xff');paths.append(cap);saved={p:p.read_bytes() for p in paths}
        self.held(f,f.invoke());self.assertEqual(saved,{p:p.read_bytes() for p in paths})
    def test_missing_progress_helper_does_not_change_commit_authority(self):
        f=self.f();(f.d/'progress.sh').unlink();p=f.invoke();self.assertEqual(p.returncode,0,p.stdout+p.stderr+f.log());self.assertEqual(f.status()[1]['reason'],'EMPTY_SESSION_RESOURCE_PROOF_VERIFIED');self.assertIn('REARM_READY',f.log())
    def test_status_or_log_symlink_cannot_write_outside_logs(self):
        f=self.f();(f.root/'logs').mkdir();outside=f.base/'outside';outside.write_bytes(b'preserve');(f.root/'logs/rearm.log').symlink_to(outside);p=f.invoke();self.assertNotEqual(p.returncode,0);self.assertEqual(outside.read_bytes(),b'preserve');self.assertTrue(f.unchanged());self.assertFalse((f.base/'probe-argv.json').exists())

if __name__=='__main__':
    buf=io.StringIO();result=unittest.TextTestRunner(stream=buf,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(WrapperTests))
    text=buf.getvalue();print(text,end='');out=R/'validation';out.mkdir(exist_ok=True)
    evidence=out/'recovery-r7-wrapper-tests.txt';evidence.write_text(text)
    sources=[WRAPPER,R/'base/nx-lut-lite/process.sh',R/'base/nx-lut-lite/progress.sh',R/'installer/SOURCE/fileops.c',R/'installer/SOURCE/src/sha256.h',R/'session/src/session.c',R/'session/src/journal.inc',R/'session/src/rearm.inc',Path(__file__)]
    record=dict(passed=result.wasSuccessful(),tests_run=result.testsRun,failures=len(result.failures),errors=len(result.errors),hardware_tested=False,host_model=True,
                scope='Shell dispatch/cancellation and actual HOST C lease/journal authorization only. Positive RESOURCE proof is synthetic HOST_FIXTURE, not the production native probe or camera resource safety. backend_resources_retired authorizes fresh native initialization; it does not assert complete quant/context reset. No physical rearm success is claimed.',
                trust_boundaries=['Pinned fileops validates session/probe/menu identities before dispatch.','External entry delegates to real C rearm supervisor/run.lock/live rearm lease.','Probe receives exact flat job/previous/token/new proof path; only its exit zero reaches real C rearm-fresh.','C independently validates complete resource proof and commits same-job fresh SRW state.','Probe failures preserve original seq1 held journal byte-for-byte; cancellation during completed commit is conservatively reheld by supervisor.','No wrapper native hardware calls, old hold deletion, old YUV adoption, or SESSION fail.'],
                source_sha256={str(p.relative_to(R)):sha(p) for p in sources},host_binary_sha256={str(p.relative_to(R)):sha(p) for p in [EXE,OPS]},component_evidence_sha256={str(evidence.relative_to(R)):sha(evidence)})
    (out/'recovery-r7-wrapper-tests.json').write_text(json.dumps(record,indent=2)+'\n')
    raise SystemExit(0 if result.wasSuccessful() else 1)
