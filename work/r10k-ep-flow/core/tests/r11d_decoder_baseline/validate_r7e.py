"""R7e preflight/menu lifecycle + field resource regressions, no camera IO."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,struct,subprocess,sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True);p.add_argument('--cross-runtime',type=Path,required=True)
p.add_argument('--python-deps',type=Path,required=True);p.add_argument('--parent-source',type=Path)
a=p.parse_args();r=Path(__file__).resolve().parents[1];out=r/'validation/r7e';out.mkdir(parents=True,exist_ok=True)
zig=a.zig.resolve();runtime=a.cross_runtime.resolve();sys.path.insert(0,str(a.python_deps.resolve()))
spec=importlib.util.spec_from_file_location('arm_model',Path(__file__).with_name('arm_model.py'));model=importlib.util.module_from_spec(spec);spec.loader.exec_module(model)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'))
records=[]
def run(cmd):
 p=subprocess.run([str(x) for x in cmd],env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
 assert p.returncode==0,(cmd,p.stdout,p.stderr)
 return p
def build(source,target):
 run([sys.executable,r/'core/tools/build_front.py','--zig',zig,'--cross-runtime',runtime,'--source',source,'--output',target])
def arm(name,source):
 target=out/(name+'.arm');build(source,target);result=model.ArmModel(target).run();assert result['exit']==0,result
 records.append(dict(name=name,source_sha256=sha(source),binary_sha256=sha(target),**result));print(result['stdout'].strip(),flush=True)
def native(name,source,argv=()):
 target=out/(name+'.exe')
 run([zig,'cc','-target','x86_64-windows-gnu','-std=c11','-O2','-UNDEBUG','-Wno-unused-function','-Wno-misleading-indentation',
      '-I',r/'core/src','-I',r/'core/tests',source,'-o',target])
 result=run([target,*argv]);records.append(dict(name='native-'+name,source_sha256=sha(source),binary_sha256=sha(target),stdout=result.stdout,hardware_tested=False))
 print(result.stdout.strip(),flush=True)

menu_source=(r/'base/nx-lut-lite/src/menu.c').read_text(encoding='utf-8')
(r/'core/tests/fixtures/menu_srw_confirm_r7e.inc').write_text(menu_source[menu_source.index('static void srw_confirm(void) {'):menu_source.index('static void draw_srw(void) {')],encoding='utf-8',newline='\n')

# Build current production components. No RTOS/render/JPEG worker changes.
probe=r/'core/bin/nxl-session-rearm.arm';session=r/'session/bin/nxl-session.arm';menu=r/'base/nx-lut-lite/bin/nxl-menu';front=r/'core/bin/nxl-demo-front.arm'
build(r/'core/src/nxl-session-rearm.c',probe)
build(r/'core/src/nxl-native-front.c',front)
run([sys.executable,r/'session/tools/build_session.py','--zig',zig,'--cross-runtime',runtime])
run([sys.executable,r/'base/nx-lut-lite/tools/build_zig.py','--zig',zig,'--menu-only','--elftools-path',a.python_deps.resolve()])
for name,binary in [('probe',probe),('session',session),('front',front)]:
 result=model.ArmModel(binary,['--version']).run();assert result['exit']==0,result
 records.append(dict(name='production-'+name+'-smoke',binary_sha256=sha(binary),**result));print(result['stdout'].strip(),flush=True)

tests=[('front-preflight','core/tests/test_front_preflight_r7e.c'),('menu-inline','core/tests/test_menu_inline_r7e.c'),('legacy-preflight-hold','session/tests/test_preflight_hold_r7e.c'),('retirement','core/tests/test_rearm_retirement_r7d.c'),('status','core/tests/test_reset_status_r7d.c'),
 ('session-action','session/tests/test_restart_action_r7d.c'),('shell-idle','core/tests/test_shell_idle_r7c.c'),
 ('shell-adapter','core/tests/test_shell_adapter_r7c.c'),('resource-contract','core/tests/test_session_rearm_contract.c')]
for name,path in tests:arm(name,r/path)
native('front-preflight',r/'core/tests/test_front_preflight_r7e.c')
native('menu-inline',r/'core/tests/test_menu_inline_r7e.c')
native('retirement',r/'core/tests/test_rearm_retirement_r7d.c')
native('status',r/'core/tests/test_reset_status_r7d.c')

# Keep the old initialized/quiet backend contract and its rejection matrix.
field=(r/'core/tests/fixtures/r7c-reset/backend-sample-00.bin').read_bytes()
quiet=bytearray(field[16:16+0x554]);struct.pack_into('<I',quiet,0x400,3)
quiet+=field[16+0x554:16+0x554+12];fixture=out/'quiet-kernel.bin';fixture.write_bytes(quiet)
native('quiet-backend-regression',r/'core/tests/test_session_rearm_backend.c',[fixture])

# Demonstrate the actual R7c predicate rejects the same 16 field snapshots.
if a.parent_source:
 parent=a.parent_source.resolve()
 src=out/'legacy-field-replay.c'
 src.write_text('#include <assert.h>\n#include <stdio.h>\n#include "'+(parent/'core/src/session_rearm_backend.h').as_posix()+'"\n#include "'+(r/'core/tests/fixtures/field_rearm_r7c.h').as_posix()+'"\n'+'''
int main(void){struct r7_backend_sample first,s;memcpy(&first,field_backend[0],sizeof first);unsigned changed=0;
for(unsigned i=0;i<16;i++){memcpy(&s,field_backend[i],sizeof s);assert(r7_backend_error(&s,0xa1c00000u,0xaf65c000u));if(!r7_backend_stable(&first,&s))changed++;}
printf("PASS actual R7c replay: all 16 same snapshots rejected; %u unrelated preview rotations treated as unstable\\n",changed);return 0;}
''',newline='\n')
 arm('legacy-field-replay',src)
 for path in ['core/src/rtos_front_blob.h','core/src/rtos_front.c','hardware/src/jpeg_engine.h','core/src/front_readiness.h']:
  assert sha(r/path)==sha(parent/path),path

from elftools.elf.elffile import ELFFile
with menu.open('rb') as f:
 elf=ELFFile(f);needed=[t.needed for t in elf.get_section_by_name('.dynamic').iter_tags() if t.entry.d_tag=='DT_NEEDED']
 assert needed==['libc.so.6','libX11.so.6']
sources={}
for prefix in ['core/src','session/src','base/nx-lut-lite/src']:
 for path in sorted((r/prefix).rglob('*')):
  if path.is_file():sources[path.relative_to(r).as_posix()]=sha(path)
result={'format':'NX51_R7E_PREFLIGHT_AND_MENU_VALIDATION1','passed':True,'hardware_tested':False,
 'parent_field_report':'R7d front_1/front: NATIVE_STILL_NOT_IDLE_OR_UNINITIALIZED; no native activity',
 'components':{'rearm':{'sha256':sha(probe),'bytes':probe.stat().st_size},'session':{'sha256':sha(session),'bytes':session.stat().st_size},
               'front':{'sha256':sha(front),'bytes':front.stat().st_size},'menu':{'sha256':sha(menu),'bytes':menu.stat().st_size,'needed':needed}},
 'source_sha256':sources,'checks':records,
 'scope':['Replay exact 16 backend and 3 front field snapshots through actual retirement predicates.',
          'Reject active JPEG, unknown preview IRQ profiles, unreadable state and old-allocation references.',
          'Only four observed preview write-buffer registers may rotate; all remaining stability comparisons stay exact.',
          'Normal render initialization, native JPEG prerequisites, proof parser and RTOS blob are unchanged.',
          'Actual menu status and session next-action helpers tested; X11 display not executed.',
          'ARM instructions execute with a small modeled OS; no live camera or MMIO is executed.']}
shell=run([sys.executable,r/'tools/test_preflight_flow_r7e.py']);print(shell.stdout.strip(),flush=True)
result['shell_flows']=json.loads((out/'shell-flows.json').read_text(encoding='utf-8'))
for path in [r/'base/nx-lut-lite'/n for n in ['memory-test.sh','rearm-session.sh','restore-camera.sh']]:sources[path.relative_to(r).as_posix()]=sha(path)
result['scope'] += ['Read-only uninitialized frontend returns42 only with zero native activity and complete cleanup.', 'Menu check stays open and explicitly reloads session/progress before a complete redraw.', 'Legacy R7d preflight hold recovery requires the exact observed report hashes, workflow markers, unspent grant and full native resource retirement.']
(out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS R7e aggregate',out/'validation.json',flush=True)
