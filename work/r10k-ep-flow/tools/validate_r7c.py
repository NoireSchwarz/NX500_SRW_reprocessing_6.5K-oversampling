"""Rebuild the three changed ARM components and run focused R7c regressions.

Requires Zig 0.13.0, the original arm-linux-musleabi-cross runtime,
pyelftools 0.31 and Unicorn 2.1.4. No camera access is made.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, platform, re, subprocess, sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig',type=Path,required=True)
parser.add_argument('--cross-runtime',type=Path,required=True)
parser.add_argument('--python-deps',type=Path)
parser.add_argument('--baseline',type=Path)
parser.add_argument('--installer',type=Path)
parser.add_argument('--shell',type=Path)
parser.add_argument('--field-root',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'validation/r7c';out.mkdir(parents=True,exist_ok=True)
zig=args.zig.resolve();runtime=args.cross_runtime.resolve()
if args.python_deps:sys.path.insert(0,str(args.python_deps.resolve()))
spec=importlib.util.spec_from_file_location('arm_model',Path(__file__).with_name('arm_model.py'))
emulator=importlib.util.module_from_spec(spec);spec.loader.exec_module(emulator)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'))
records=[]

def run(command):
 p=subprocess.run([str(v) for v in command],capture_output=True,text=True,encoding='utf-8',errors='replace',env=env,timeout=120)
 if p.returncode:raise RuntimeError(f'{command}\n{p.stdout}\n{p.stderr}')
 return p

def build(source,target,base=root):
 (base/'core/results').mkdir(parents=True,exist_ok=True)
 run([sys.executable,base/'core/tools/build_front.py','--zig',zig,'--cross-runtime',runtime,
      '--source',source,'--output',target])

def arm_case(name,source,base=root,argv=()):
 target=out/(name+'.arm');build(source,target,base)
 result=emulator.ArmModel(target,argv).run();assert result['exit']==0,result
 record=dict(name=name,source_sha256=sha(source),binary_sha256=sha(target),**result)
 records.append(record);print(result['stdout'].strip(),flush=True)

components=[('rearm',root/'core/src/nxl-session-rearm.c',root/'core/bin/nxl-session-rearm.arm'),
 ('front',root/'core/src/nxl-native-front.c',root/'core/bin/nxl-demo-front.arm'),
 ('controller',root/'hardware/src/nxl-allocprobe.c',root/'hardware/bin/nxl-hw-controller.arm')]
for name,source,target in components:
 build(source,target)
 result=emulator.ArmModel(target,['--version']).run();assert result['exit']==0,result
 records.append(dict(name='production-'+name+'-smoke',source_sha256=sha(source),binary_sha256=sha(target),**result))
 print(result['stdout'].strip(),flush=True)

cases=[('shell-idle','core/tests/test_shell_idle_r7c.c'),
 ('shell-adapter','core/tests/test_shell_adapter_r7c.c'),
 ('capacity','core/tests/test_allocator_capacity_r7c.c'),
 ('readiness','core/tests/test_front_readiness.c'),
 ('resource-contract','core/tests/test_session_rearm_contract.c'),
 ('front-contract','core/tests/test_front_core_arm.c'),
 ('snapshot-stability','hardware/tests/test_allocator_stability.c')]
for name,path in cases:arm_case(name,root/path)

if args.baseline:
 baseline=args.baseline.resolve()
 legacy=out/'legacy-shell-adapter.c'
 text=(root/'core/tests/test_shell_adapter_r7c.c').read_text()
 text=text.replace('#define REARM_SOURCE "../src/nxl-session-rearm.c"',
                   '#define REARM_SOURCE "'+(baseline/'core/src/nxl-session-rearm.c').as_posix()+'"')
 legacy.write_text('#define EXPECT_LEGACY_REJECTION 1\n'+text,newline='\n')
 arm_case('baseline-shell-reproduction',legacy,baseline)
 prefix=(root/'core/tests/test_front_readiness.c').read_text().split('int main(void){',1)[0]
 for label,base,expected in [('legacy',baseline,1),('fixed',root,0)]:
  source=out/('clock-reproduction-'+label+'.c')
  body='''int main(void){struct model m;struct fr_wait_result r;
 initialize(&m);m.sleep_mode=1;int stalled=run_wait(&m,&r);assert(stalled==EXPECTED);
 initialize(&m);m.clock_step=UINT32_MAX;int wrapped=run_wait(&m,&r);assert(wrapped==EXPECTED);
 printf("PASS clock differential: stalled_accepted=%d wrapped_accepted=%d elapsed_ms=%u\\n",stalled,wrapped,r.elapsed_ms);return 0;}
'''.replace('EXPECTED',str(expected))
  source.write_text(prefix+body,newline='\n');arm_case('clock-differential-'+label,source,base)

if platform.system()=='Windows':
 for name,path in cases[:1]+cases[2:4]:
  source=root/path;exe=out/(name+'.exe')
  run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
       '-Wno-unused-function','-Wno-misleading-indentation','-I',root/'hardware/src','-I',root/'core/src',source,'-o',exe])
  p=run([exe]);records.append({'name':'native-'+name,'binary_sha256':sha(exe),'stdout':p.stdout,'exit':p.returncode,'hardware_tested':False})
  print(p.stdout.strip(),flush=True)
  if name=='capacity' and args.field_root:
   for relative in ['front_2/front/case_00/allocator_before.bin','jpeg/attempt_1/hardware/allocator_before.bin']:
    snapshot=args.field_root/relative
    if snapshot.is_file():
     p=run([exe,snapshot.resolve()]);records.append({'name':'field-capacity-'+relative,'input_sha256':sha(snapshot),'stdout':p.stdout,'exit':p.returncode,'hardware_tested':False})
     print(p.stdout.strip(),flush=True)

if args.shell and args.installer:
 target=out/'transaction-paths.json'
 p=run([sys.executable,Path(__file__).with_name('test_transaction_paths_r7c.py'),'--source',args.installer.resolve(),
        '--shell',args.shell.resolve(),'--output',target])
 record=json.loads(target.read_text());record['name']='installer-transaction-paths';records.append(record)
 print(p.stdout.strip(),flush=True)

source_hashes={}
for subtree in ['core/src','hardware/src']:
 for p in sorted((root/subtree).rglob('*')):
  if p.is_file():source_hashes[p.relative_to(root).as_posix()]=sha(p)
result={'format':'NX51_R7C_FOCUSED_VALIDATION1','passed':True,'hardware_tested':False,
        'camera_speedup_measured':False,'environment':platform.platform(),'zig_version':run([zig,'version']).stdout.strip(),
        'components':{name:{'binary_sha256':sha(binary),'bytes':binary.stat().st_size} for name,_,binary in components},
        'source_sha256':source_hashes,'checks':records,
        'limits':['ARM instructions run with modeled stdio/memory/time syscalls, not a real ARM Linux kernel.',
                  'No camera, DMA, MMIO or actual ptrace is executed.',
                  'Existing front CAP retention and JPEG cold-reset requirements remain; no unsafe free or forced rearm is introduced.',
                  'Installer case tests exercise the exact path selector, not a complete camera deployment.']}
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS aggregate: '+str(out/'validation.json'),flush=True)
