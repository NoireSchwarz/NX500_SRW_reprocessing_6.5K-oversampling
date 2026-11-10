#!/usr/bin/env python3
"""Execute the production shell path guard on a real OEM-alias topology."""
from pathlib import Path
import hashlib,io,json,os,shlex,subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[2]
SRC=R/'installer/SOURCE/runner.sh.in'
def function(name):
 s=SRC.read_text();a=s.index(name+'(){');b=s.index('\n}\n',a)+3;return s[a:b]
class PathGuard(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='nx54-front-path-');self.w=Path(self.tmp.name).resolve();self.alias=self.w/'mnt/mmc';self.target=self.w/'opt/storage/sdcard';self.alias.parent.mkdir();self.target.mkdir(parents=True);self.alias.symlink_to(self.target,target_is_directory=True)
  self.out=self.alias/'NXDEMO51';self.old=self.alias/'NXDEMO50';self.report=self.out/'JOB_1793869199_415_10dc3936/front_1/front';(self.report/'case_00').mkdir(parents=True)
  self.prefix='\n'.join([f'OUT={shlex.quote(str(self.out))}',f'OLD_OUT={shlex.quote(str(self.old))}',f'OEM_SD_ALIAS={shlex.quote(str(self.alias))}',f'OEM_SD_TARGET={shlex.quote(str(self.target))}',function('identifier'),function('absent'),function('valid_front_path')])+'\n'
 def tearDown(self):self.tmp.cleanup()
 def call(self,path=None):return subprocess.run(['/bin/sh','-c',self.prefix+'valid_front_path '+shlex.quote(str(path or self.report))],capture_output=True,text=True).returncode
 def test_actual_OEM_alias_accepts_new_front(self):self.assertEqual(self.call(),0)
 def test_regular_directory_without_alias_accepted(self):
  a=self.alias;a.unlink();a.mkdir();self.report=a/'NXDEMO51/JOB_regular/front_2/front';(self.report/'case_00').mkdir(parents=True);self.assertEqual(self.call(),0)
 def test_documented_legacy_path_guard_accepts_but_no_autoimport_exists(self):
  p=self.old/'DEMO_20261105_141315_391/front';(p/'case_00').mkdir(parents=True);self.assertEqual(self.call(p),0);s=SRC.read_text();self.assertNotIn('cache_candidate()',s);self.assertNotIn('cache_save()',s);self.assertIn('NEED_STILL_INIT) run_front;;',s)
 def test_wrong_OEM_target_refused(self):
  target=self.w/'wrong';(target/'NXDEMO51/JOB_1793869199_415_10dc3936/front_1/front/case_00').mkdir(parents=True);self.alias.unlink();self.alias.symlink_to(target,target_is_directory=True);self.assertNotEqual(self.call(),0)
 def test_other_ancestor_link_refused(self):
  job=self.out/'JOB_1793869199_415_10dc3936';actual=self.w/'actual-job';job.rename(actual);job.symlink_to(actual,target_is_directory=True);self.assertNotEqual(self.call(),0)
 def test_report_link_refused(self):
  actual=self.w/'actual-front';self.report.rename(actual);self.report.symlink_to(actual,target_is_directory=True);self.assertNotEqual(self.call(),0)
 def test_case_directory_link_refused(self):
  p=self.report/'case_00';p.rmdir();p.symlink_to(self.w,target_is_directory=True);self.assertNotEqual(self.call(),0)
 def test_extra_path_components_refused(self):
  p=self.report/'extra/front_2/front';(p/'case_00').mkdir(parents=True);self.assertNotEqual(self.call(p),0)
 def test_invalid_front_attempt_refused(self):
  for value in ['front_0','front_01','front_1bad','front_a']:
   p=self.report.parent.parent/value/'front';(p/'case_00').mkdir(parents=True);self.assertNotEqual(self.call(p),0,value)
 def test_escape_and_unexpected_root_refused(self):
  for value in [str(self.report)+'/../front',str(self.w/'elsewhere/front'),str(self.out/'JOB_bad/another/front')]:self.assertNotEqual(self.call(value),0,value)
 def test_actual_restart_markers_refused(self):
  for name in ['P48_RESTART_REQUIRED','P49_RESTART_REQUIRED']:
   p=self.report/name;p.touch();self.assertNotEqual(self.call(),0,name);p.unlink()
 def test_missing_case_directory_refused(self):(self.report/'case_00').rmdir();self.assertNotEqual(self.call(),0)
if __name__=='__main__':
 stream=io.StringIO();result=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PathGuard));v=R/'validation';v.mkdir(exist_ok=True);(v/'front-path-tests.txt').write_text(stream.getvalue());j={'passed':result.wasSuccessful(),'tests_run':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'hardware_tested':False,'real_filesystem_alias_model':True,'source_sha256':{'installer/SOURCE/runner.sh.in':hashlib.sha256(SRC.read_bytes()).hexdigest(),'installer/SOURCE/test_front_path_r4.py':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}};(v/'front-path-tests.json').write_text(json.dumps(j,indent=2)+'\n');print(stream.getvalue(),end='');raise SystemExit(not result.wasSuccessful())
