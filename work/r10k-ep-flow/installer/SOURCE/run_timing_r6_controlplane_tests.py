#!/usr/bin/env python3
"""Fresh host-only records for changed R6 installation and orchestration paths."""
from pathlib import Path
import hashlib,importlib.util,io,json,sys,unittest
R=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
GROUPS={
 'deploy':('test_deploy.py',['installer/deploy.sh.in','installer/install.sh','installer/SOURCE/fileops.c']),
 'renderer':('test_render.py',['installer/render_deploy.py','installer/SOURCE/runner.sh.in','base/nx-lut-lite/restore-camera.sh']),
 'workflow':('test_workflow.py',['installer/SOURCE/runner.sh.in','base/nx-lut-lite/restore-camera.sh','session/src/session.c','session/src/journal.inc','installer/SOURCE/fileops.c']),
 'entry':('test_kernel_entry.py',['installer/install.sh']),
}
def main():
 chosen=sys.argv[1:] or list(GROUPS);passed=True;records={};v=R/'validation';v.mkdir(exist_ok=True)
 for name in chosen:
  file,sources=GROUPS[name];test=Path(__file__).with_name(file)
  spec=importlib.util.spec_from_file_location('r6_'+name,test);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  stream=io.StringIO();result=unittest.TextTestRunner(stream=stream,verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(m));log=stream.getvalue();sys.stdout.write(log);passed=passed and result.wasSuccessful()
  logpath=v/('timing-r6-'+name+'-tests.txt');logpath.write_text(log)
  record={'format':'NX51_TIMING_R6_HOST_COMPONENT_TESTS1','component':name,'tests_run':result.testsRun,'passed':result.wasSuccessful(),'failures':len(result.failures),'errors':len(result.errors),'hardware_tested':False,'log_sha256':sha(logpath),'source_sha256':{p:sha(R/p) for p in [*sources,str(test.relative_to(R)),str(Path(__file__).relative_to(R))]}}
  recordpath=v/('timing-r6-'+name+'-tests.json');recordpath.write_text(json.dumps(record,indent=2)+'\n');records[name]={'path':str(recordpath.relative_to(R)),'sha256':sha(recordpath),'tests_run':result.testsRun,'passed':result.wasSuccessful()}
 print(json.dumps(records,indent=2));return 0 if passed else 1
if __name__=='__main__':raise SystemExit(main())
