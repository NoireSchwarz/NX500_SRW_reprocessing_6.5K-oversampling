#!/usr/bin/env python3
"""Real diagnostic/trace shell fixtures with sensor/native tools replaced by stubs."""
from pathlib import Path
import hashlib,io,json,os,subprocess,tempfile,unittest
R=Path(__file__).resolve().parents[2];BASE=R/'base/nx-lut-lite'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def executable(p,t):p.write_text(t);p.chmod(0o755)
class DiagnosticPaths(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='nx51-diagnostic-paths-');self.addCleanup(self.tmp.cleanup);self.w=Path(self.tmp.name)
  self.sd=self.w/'mnt/mmc';self.sd.mkdir(parents=True);self.module=self.w/'module';(self.module/'bin').mkdir(parents=True);(self.module/'state').mkdir();self.mock=self.w/'bin';self.mock.mkdir()
  self.run=self.w/'tmp/nx-lut-lite';self.run.mkdir(parents=True)
  self.proc=self.w/'proc';(self.proc/'sys/kernel/random').mkdir(parents=True);(self.proc/'sys/kernel/random/boot_id').write_text('MODEL_ONLY\n');(self.proc/'uptime').write_text('1.0 0.0\n')
  self.version=self.w/'version.info';self.version.write_text('1.12\nNX500\n')
  self.bt=self.w/'bluetoothd';self.bt.write_text('# NX_LUT_LITE_BT_HOOK_V1\n')
  for n in ['diagnose.sh','trace.sh']:
   s=(BASE/n).read_text().replace('/mnt/mmc',str(self.sd)).replace('/tmp/nx-lut-lite',str(self.run)).replace('/etc/version.info',str(self.version)).replace('/proc/',str(self.proc)+'/').replace('/usr/sbin/bluetoothd',str(self.bt))
   executable(self.module/n,s)
  (self.module/'process.sh').write_text('nxl_require_normal_mode(){ return 0; }\n')
  executable(self.module/'lut.sh','#!/bin/sh\n[ "$1" = status ] || exit 97\nprintf "LUT_STATUS_MODEL_ONLY\\n"\n')
  executable(self.module/'bin/nxl-menu','#!/bin/sh\n[ "$1" = --inspect ] || exit 98\nprintf "INPUT_INSPECTION_MODEL_ONLY\\n"\n')
  executable(self.module/'bin/nxl-lutobserve.arm','#!/bin/sh\n[ "$1" = watch ] || exit 99\nprintf "index,value\\n0,1\\n"\n')
  executable(self.mock/'prefman','#!/bin/sh\n[ "$1" = get ] || exit 96\nprintf "PREF_READ_MODEL_ONLY\\n"\n')
  self.env=dict(os.environ,PATH=str(self.mock)+':'+os.environ.get('PATH','/usr/bin:/bin'))
 def run_script(self,name,*args):return subprocess.run(['/bin/sh',str(self.module/name),*args],env=self.env,capture_output=True,text=True,timeout=8)
 def test_default_diagnostic_creates_only_owned_log_folder_on_request(self):
  self.assertEqual(list(self.sd.iterdir()),[])
  r=self.run_script('diagnose.sh');self.assertEqual(r.returncode,0,(r.stdout,r.stderr))
  p=self.sd/'NXDEMO51/logs/diagnostic.txt';self.assertIn('LUT_STATUS_MODEL_ONLY',p.read_text());self.assertEqual({p.name for p in self.sd.iterdir()},{'NXDEMO51'})
 def test_trace_keeps_all_snapshots_and_csv_inside_log_subfolder(self):
  self.assertEqual(list(self.sd.iterdir()),[])
  r=self.run_script('trace.sh');self.assertEqual(r.returncode,0,(r.stdout,r.stderr))
  out=self.sd/'NXDEMO51/logs/af-trace';self.assertEqual({p.name for p in out.iterdir()},{'OFF.before.txt','OFF.after.txt','OFF.csv','OFF.result.txt'})
  self.assertIn('COMPLETE',(out/'OFF.result.txt').read_text());self.assertEqual({p.name for p in self.sd.iterdir()},{'NXDEMO51'});self.assertFalse((self.run/'trace.lock').exists())
 def test_logging_paths_do_not_restore_legacy_root_defaults(self):
  d=(BASE/'diagnose.sh').read_text();t=(BASE/'trace.sh').read_text()
  self.assertIn('/mnt/mmc/NXDEMO51/logs/diagnostic.txt',d);self.assertIn('/mnt/mmc/NXDEMO51/logs/af-trace',t)
  self.assertNotIn('/mnt/mmc/nx-lut-lite-diagnostic.txt',d);self.assertNotIn('/mnt/mmc/nx-lut-lite-af-trace',t)
 def test_diagnostic_leaf_symlink_is_refused(self):
  logs=self.sd/'NXDEMO51/logs';logs.mkdir(parents=True);other=self.w/'userfile';other.write_text('PRESERVE');(logs/'diagnostic.txt').symlink_to(other)
  self.assertEqual(self.run_script('diagnose.sh').returncode,1);self.assertEqual(other.read_text(),'PRESERVE')
if __name__=='__main__':
 stream=io.StringIO();result=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(DiagnosticPaths));v=R/'validation';v.mkdir(exist_ok=True)
 (v/'diagnostic-path-tests.txt').write_text(stream.getvalue())
 (v/'diagnostic-path-tests.json').write_text(json.dumps({'format':'NX51_DIAGNOSTIC_PATH_R4_TESTS1','passed':result.wasSuccessful(),'tests_run':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'hardware_tested':False,'all_sensor_native_tools_stubbed':True,'source_sha256':{str(p.relative_to(R)):sha(p) for p in [BASE/'diagnose.sh',BASE/'trace.sh',Path(__file__)]}},indent=2)+'\n')
 print(stream.getvalue(),end='');raise SystemExit(not result.wasSuccessful())
