#!/usr/bin/env python3
"""Reproduce real-SRW/native14x3 model and transaction tests; no physical DMA."""
import argparse,subprocess,os,sys,json,hashlib,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--firmware',type=Path,required=True);p.add_argument('--zig',type=Path,required=True);p.add_argument('--cross-runtime',type=Path,required=True);p.add_argument('--deps',type=Path,required=True);a=p.parse_args()
s=Path(__file__).resolve().parents[1];b=s/'build';r=s/'results';b.mkdir(exist_ok=True);r.mkdir(exist_ok=True)
env={**os.environ,'TMPDIR':str(b.resolve()),'PYTHONPATH':str(a.deps.resolve())}
def run(args,out=None,extra=None):
 q=subprocess.run([str(x) for x in args],capture_output=True,text=True,env={**env,**(extra or {})})
 if out:(r/out).write_text(q.stdout+q.stderr)
 if q.returncode:print(q.stdout,q.stderr,file=sys.stderr);q.check_returncode()
 print(q.stdout.strip() or q.stderr.strip(),flush=True);return q
run([sys.executable,s/'tools/build_rtos.py','--zig',a.zig,'--elftools-path',a.deps])
run([sys.executable,s/'tools/build_front.py','--zig',a.zig,'--cross-runtime',a.cross_runtime])
for name in ['test_real_source','test_front_layout','test_production_front','test_pause_guardian','test_install_guard','test_collector_ready','test_srw_iq']:
 source=s/'tests'/f'{name}.c'
 if source.exists():run(['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation','-I',s/'src',source,'-o',b/name])
run(['cc','-O2','-shared','-fPIC','-I',s/'src',s/'tests/model_hash.c','-o',b/'model_hash.so'])
if (r/'real-decoder-report').exists():shutil.rmtree(r/'real-decoder-report')
(r/'real-decoder-report').mkdir()
run([b/'test_real_source',s.parent/'nx-lut-alloc-update/p39b_input.SRW',s.parent/'nx-lut-alloc-update/p39b_expected_rows.bin',r/'real-decoder-report',b/'real-stage.bin'],'real-source.txt')
assert hashlib.sha256((b/'real-stage.bin').read_bytes()).hexdigest()=='f58c9f351ebe2ca52e231336da8d320a3c47c57997a90284bbf5781cf3e57e0e'
run([b/'test_front_layout',r/'model-cap-template.bin',b/'real-stage.bin',s.parent/'nx-lut-alloc-update/p39b_input.SRW'],'layout.txt')
run([sys.executable,s/'tests/test_iq_reader.py',s.parent/'nx-lut-alloc-update/p39b_input.SRW'],'reader-tests.txt')
run([sys.executable,s/'replay_wb_contract.py',a.firmware],'original-wb-tests.txt')
run([sys.executable,s/'replay_front.py',a.firmware],'front-contracts.txt')
for d in ['production-model-report','failure-model-report','grant-model-report']:
 if (r/d).exists():shutil.rmtree(r/d)
 (r/d).mkdir()
for d in ['production-model-report','failure-model-report']:shutil.copyfile(r/'real-decoder-report/p39_rows.csv',r/d/'p39_rows.csv')
run([b/'test_production_front',r/'model-cap-template.bin',r/'model-result-words.bin',r/'production-model-report',r/'model-diagnostics.bin',b/'model-output.bin',r/'model-stop.bin',b/'real-stage.bin',r/'failure-model-report',r/'model-wb-audit.bin'],'test_production_front.txt')
run([sys.executable,s/'analyze_front.py',r/'production-model-report'],'production-model-analysis.json')
run([sys.executable,s/'tests/test_front_report.py'],'report-tests.txt',{'P39_MODEL_REPORT':str((r/'production-model-report').resolve())})
run([b/'test_pause_guardian'],'pause-guardian.txt')
with __import__('tempfile').TemporaryDirectory(dir=b) as d:run([b/'test_collector_ready',d],'test_collector_ready.txt')
run([b/'test_install_guard',r/'grant-model-report'],'grant-models.txt')
run([sys.executable,s/'test_update.py'],'updater.txt')
run([sys.executable,s/'tools/build_front.py','--zig',a.zig,'--cross-runtime',a.cross_runtime,'--source',s/'tests/test_front_core_arm.c','--output',s/'bin/test-front-core.arm'])
sys.path[:0]=[str(a.deps.resolve()),str(s)];from arm_startup_emulator import run as arm_run
binary=s/'bin/nxl-allocprobe.arm';cases=[arm_run(binary,['--version'],0),arm_run(binary,['--clock'],0),arm_run(s/'bin/test-front-core.arm',[],0)]
(r/'arm-execution.json').write_text(json.dumps({'final_binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'cases':cases,'hardware_tested':False},indent=2)+'\n')
assert '0.3.4-p41;' in cases[0]['stdout'];print('PASS final ARM executable version/clock/full-frame allocation ABI',flush=True)
shutil.copyfile(binary,s.parent/'nx-lut-alloc-update/nxl-allocprobe.arm')
