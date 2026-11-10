#!/usr/bin/env python3
"""Exercise the actual non-controlling SD progress shell helper, no camera."""
from pathlib import Path
import hashlib,io,json,os,shlex,subprocess,tempfile,threading,unittest

R=Path(__file__).resolve().parents[2]
HELPER=R/'base/nx-lut-lite/progress.sh'
STATUS='status.txt'
LOG='progress.log'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
class Progress(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='nx51-progress-')
        self.addCleanup(self.tmp.cleanup)
        self.w=Path(self.tmp.name);self.sd=self.w/'sd';self.sd.mkdir();self.logs=self.sd/'NXDEMO51/logs';self.logs.mkdir(parents=True)
        self.tools=self.w/'tools';self.tools.mkdir();sync=self.tools/'sync';sync.write_text('#!/bin/sh\nexit 0\n');sync.chmod(0o755)
        self.env=dict(os.environ,NXL_PROGRESS_SD=str(self.sd),PATH=str(self.tools)+':'+os.environ.get('PATH','/bin:/usr/bin'))
    def call(self,*args,env=None,before='',after=''):
        script=before+'\n. '+shlex.quote(str(HELPER))+'\nnxl_progress "$@"\n'+after
        return subprocess.run(['/bin/sh','-eu','-c',script,'test',*args],env=env or self.env,capture_output=True,text=True,timeout=15)
    def snapshot(self):
        text=(self.logs/STATUS).read_text();lines=text.splitlines()
        self.assertEqual(lines[0],'NXDEMO51_PROGRESS1');self.assertEqual(lines[-1],'end=NXDEMO51_PROGRESS_END1');self.assertEqual(len(lines),6)
        self.assertLess(len(text.encode()),2048)
        self.assertEqual([s.split('=',1)[0] for s in lines[1:]],['phase','state','detail','time','end'])
        return dict(s.split('=',1) for s in lines[1:])
    def test_atomic_snapshot_and_append_keep_each_phase(self):
        for p,s,d in [('INSTALL','STARTING','SD install entry'),('DEPLOY','PASS','report=/mnt/mmc/NXDEMO51/logs/install.log'),('BLUETOOTH','FAIL','reason=INPUT_NOT_READY')]:
            self.assertEqual(self.call(p,s,d).returncode,0)
        v=self.snapshot();self.assertEqual((v['phase'],v['state'],v['detail']),('BLUETOOTH','FAIL','reason=INPUT_NOT_READY'))
        lines=(self.logs/LOG).read_text().splitlines();self.assertEqual(len(lines),3);self.assertIn('phase=INSTALL state=STARTING',lines[0]);self.assertIn('state=PASS',lines[1]);self.assertIn('state=FAIL',lines[2])
    def test_progress_never_creates_card_root_diagnostic_files(self):
        self.assertEqual(self.call('RAW_REQUEST','REQUESTED','logs only').returncode,0)
        self.assertEqual({p.name for p in self.sd.iterdir()},{'NXDEMO51'})
        self.assertTrue((self.logs/STATUS).is_file());self.assertTrue((self.logs/LOG).is_file())
    def test_owned_log_directory_symlink_is_refused(self):
        self.logs.rmdir();outside=self.w/'outside';outside.mkdir();self.logs.symlink_to(outside,target_is_directory=True)
        self.assertEqual(self.call('RAW','RUNNING','unsafe log directory').returncode,0)
        self.assertEqual(list(outside.iterdir()),[])
    def test_date_missing_is_optional(self):
        bindir=self.w/'bin';bindir.mkdir();(bindir/'mv').symlink_to('/bin/mv')
        r=self.call('INSTALL','STARTING','no date',env=dict(self.env,PATH=str(bindir)))
        self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(self.snapshot()['time'],'UNAVAILABLE')
    def test_date_failure_is_optional(self):
        bindir=self.w/'bin';bindir.mkdir();(bindir/'mv').symlink_to('/bin/mv');d=bindir/'date';d.write_text('#!/bin/sh\nexit 9\n');d.chmod(0o755)
        self.assertEqual(self.call('INSTALL','PASS','date unavailable',env=dict(self.env,PATH=str(bindir))).returncode,0)
        self.assertEqual(self.snapshot()['time'],'UNAVAILABLE')
    def test_missing_mv_still_appends_without_affecting_caller(self):
        bindir=self.w/'bin';bindir.mkdir()
        r=self.call('RAW','FAIL','original hardware result retained',env=dict(self.env,PATH=str(bindir)),after='exit 37\n')
        self.assertEqual(r.returncode,37);self.assertEqual(self.snapshot()['state'],'FAIL');self.assertIn('STATUS_MV_UNAVAILABLE',(self.logs/LOG).read_text());self.assertIn('INPLACE_VERIFIED',(self.logs/LOG).read_text())
    def test_status_symlink_target_untouched(self):
        outside=self.w/'outside';outside.write_text('DO_NOT_WRITE');(self.logs/STATUS).symlink_to(outside)
        self.assertEqual(self.call('INSTALL','FAIL','blocked link').returncode,0)
        self.assertEqual(outside.read_text(),'DO_NOT_WRITE');self.assertTrue((self.logs/STATUS).is_symlink());self.assertFalse((self.logs/LOG).exists())
    def test_log_symlink_target_untouched(self):
        outside=self.w/'outside';outside.write_text('DO_NOT_APPEND');(self.logs/LOG).symlink_to(outside)
        self.assertEqual(self.call('INSTALL','PASS','blocked link').returncode,0)
        self.assertEqual(outside.read_text(),'DO_NOT_APPEND');self.assertFalse((self.logs/STATUS).exists())
    def test_known_SD_directory_alias_accepted(self):
        link=self.w/'link';link.symlink_to(self.sd,target_is_directory=True)
        self.assertEqual(self.call('INSTALL','STARTING','link',env=dict(self.env,NXL_PROGRESS_SD=str(link))).returncode,0)
        self.assertEqual(self.snapshot()['detail'],'link')
    def test_directory_alias_ancestor_is_resolved(self):
        child=self.sd/'child';child.mkdir();link=self.w/'link';link.symlink_to(self.sd,target_is_directory=True)
        self.assertEqual(self.call('INSTALL','STARTING','link',env=dict(self.env,NXL_PROGRESS_SD=str(link/'child'))).returncode,0)
        self.assertIn('phase=INSTALL',(child/'NXDEMO51/logs'/STATUS).read_text())
    def test_status_directory_refused(self):
        (self.logs/STATUS).mkdir()
        self.assertEqual(self.call('INSTALL','STARTING','dir').returncode,0)
        self.assertEqual(list((self.logs/STATUS).iterdir()),[]);self.assertFalse((self.logs/LOG).exists())
    def test_nonexistent_SD_does_not_create_paths(self):
        absent=self.w/'missing'
        self.assertEqual(self.call('INSTALL','STARTING','missing',env=dict(self.env,NXL_PROGRESS_SD=str(absent))).returncode,0)
        self.assertFalse(absent.exists())
    def test_invalid_keys_or_multiline_detail_never_publish(self):
        for args in [('BAD PHASE','PASS','bad'),('INSTALL','bad/state','bad'),('INSTALL','PASS','line1\nline2'),('INSTALL','PASS','line1\rline2'),('INSTALL','PASS','x'*1025)]:
            with self.subTest(args=args[:2]):self.assertEqual(self.call(*args).returncode,0)
        self.assertEqual(list(self.logs.iterdir()),[])
    def test_temp_collision_does_not_overwrite_foreign_file(self):
        before='printf KEEP > "$NXL_PROGRESS_SD/NXDEMO51/logs/.status.$$.0.tmp"'
        self.assertEqual(self.call('INSTALL','PASS','collision',before=before).returncode,0)
        self.assertEqual(self.snapshot()['state'],'PASS');kept=list(self.logs.glob('.status.*.0.tmp'));self.assertEqual(len(kept),1);self.assertEqual(kept[0].read_text(),'KEEP')
    def test_function_does_not_modify_caller_variables(self):
        r=self.call('INSTALL','PASS','isolation',before='nxlpg_phase=CALLER; task_rc=7',after='[ "$nxlpg_phase" = CALLER ] && [ "$task_rc" = 7 ]\n')
        self.assertEqual(r.returncode,0,r.stderr)
    def test_UTF8_detail_bound_is_bytes_not_characters(self):
        self.assertEqual(self.call('INSTALL','PASS','准备环境').returncode,0)
        self.assertEqual(self.snapshot()['detail'],'准备环境')
        prior=(self.logs/STATUS).read_bytes()
        self.assertEqual(self.call('INSTALL','PASS','准'*342,env=dict(self.env,LC_ALL='C.UTF-8')).returncode,0)
        self.assertEqual((self.logs/STATUS).read_bytes(),prior)
    def failing_mv(self,mode):
        mv=self.tools/'mv'
        if mode=='reject-replacement':body='for last do :;done\n[ ! -e "$last" ] || exit 23\nexec /bin/mv "$@"\n'
        elif mode=='false-success':body='exit 0\n'
        else:raise ValueError(mode)
        mv.write_text('#!/bin/sh\n'+body);mv.chmod(0o755)
    def test_replacement_failure_is_logged_and_inplace_verified(self):
        self.failing_mv('reject-replacement')
        self.assertEqual(self.call('INSTALL_ENTRY','RUNNING','first').returncode,0)
        self.assertEqual(self.call('INSTALL_COMPLETE','SUCCESS','latest',after='exit 37\n').returncode,37)
        self.assertEqual(self.snapshot()['phase'],'INSTALL_COMPLETE')
        log=(self.logs/LOG).read_text();self.assertIn('mv_exit=23',log);self.assertIn('snapshot=INPLACE_VERIFIED',log)
    def test_false_successful_move_stale_snapshot_is_detected(self):
        self.assertEqual(self.call('INSTALL_ENTRY','RUNNING','old').returncode,0)
        self.failing_mv('false-success')
        self.assertEqual(self.call('RAW_REQUEST','REQUESTED','new').returncode,0)
        self.assertEqual(self.snapshot()['phase'],'RAW_REQUEST')
        log=(self.logs/LOG).read_text();self.assertIn('mv_exit=0 readback=MISMATCH',log);self.assertIn('snapshot=INPLACE_VERIFIED',log)
    def test_old_failure_log_and_hardware_holds_are_preserved(self):
        (self.logs/LOG).write_text('OLDER_ERROR retained\n');hold=self.sd/'RESTART_REQUIRED';hold.write_text('UNKNOWN_DMA')
        self.failing_mv('false-success')
        self.assertEqual(self.call('RAW','FAILED','new failure').returncode,0)
        self.assertTrue((self.logs/LOG).read_text().startswith('OLDER_ERROR retained\n'));self.assertEqual(hold.read_text(),'UNKNOWN_DMA')
    def test_install_caller_receives_publish_warning_in_install_log(self):
        self.failing_mv('false-success')
        self.assertEqual(self.call('INSTALL','SUCCESS','model',env=dict(self.env,NXL_PROGRESS_TO_INSTALL_LOG='1')).returncode,0)
        text=(self.logs/'install.log').read_text();self.assertIn('STATUS_ATOMIC_PUBLISH_FAILED',text);self.assertIn('snapshot=INPLACE_VERIFIED',text)
    def test_atomic_reader_never_observes_partial_record(self):
        self.assertEqual(self.call('INSTALL','STARTING','first').returncode,0)
        observed=[];errors=[];stop=threading.Event()
        def reader():
            while not stop.is_set():
                try:
                    text=(self.logs/STATUS).read_text();lines=text.splitlines()
                    if len(lines)!=6 or lines[0]!='NXDEMO51_PROGRESS1' or lines[-1]!='end=NXDEMO51_PROGRESS_END1':errors.append(text)
                    observed.append(1)
                except Exception as e:errors.append(str(e))
        th=threading.Thread(target=reader);th.start()
        try:
            cmd='. '+shlex.quote(str(HELPER))+'; n=0; while [ "$n" -lt 40 ];do nxl_progress RAW RUNNING "attempt=$n report=/mnt/mmc/NXDEMO51/JOB_X"; n=$((n+1));done'
            r=subprocess.run(['/bin/sh','-eu','-c',cmd],env=self.env,capture_output=True,text=True,timeout=15)
            self.assertEqual(r.returncode,0,r.stderr)
        finally:stop.set();th.join()
        self.assertTrue(observed);self.assertEqual(errors,[])
        self.assertEqual(self.snapshot()['detail'],'attempt=39 report=/mnt/mmc/NXDEMO51/JOB_X')
    def test_no_boot_or_hardware_dependencies_in_helper(self):
        text=HELPER.read_text()
        for forbidden in ['/proc/','boot_id','reboot(','/dev/drime','/dev/mem','systemctl','pidof','pgrep']:
            self.assertNotIn(forbidden,text)
        self.assertEqual(self.call('INSTALL','PASS','no boot requirement').returncode,0)

if __name__=='__main__':
    stream=io.StringIO();result=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Progress))
    out=R/'validation';out.mkdir(exist_ok=True);log=stream.getvalue();(out/'progress-tests.txt').write_text(log)
    report={'format':'NX51_PROGRESS_TESTS1','passed':result.wasSuccessful(),'tests_run':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'hardware_tested':False,'boot_id_required':False,'helper_sha256':sha(HELPER),'test_sha256':sha(Path(__file__))}
    (out/'progress-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(log,end='');raise SystemExit(0 if result.wasSuccessful() else 1)
