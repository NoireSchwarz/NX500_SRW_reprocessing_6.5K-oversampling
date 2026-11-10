#!/usr/bin/env python3
"""Bind current menu tests, legacy libc imports and an actual ARM version probe."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,tempfile
R=Path(__file__).resolve().parents[1];P=R.parents[1];W=P.parent
sys.path.insert(0,str(W/'wb-load-p42/deps'))
from elftools.elf.elffile import ELFFile
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rel=lambda p:str(p.relative_to(P))
def main():
 binary=R/'bin/nxl-menu';libc=W/'demo-p49/research/backend-reuse/camera-root/lib/libc-2.13.so';loader=W/'demo-p49/research/backend-reuse/camera-root/lib/ld-2.13.so';qemu=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';stub=R/'build/libX11.so.6'
 assert sha(libc)=='198a7b52e68df955de2573557644720e76580aae489e3785bb9afd80ad6f2e9c'
 with libc.open('rb') as f:exports={s.name for s in ELFFile(f).get_section_by_name('.dynsym').iter_symbols() if s['st_shndx']!='SHN_UNDEF'}
 with binary.open('rb') as f:
  e=ELFFile(f);imports={s.name for s in e.get_section_by_name('.dynsym').iter_symbols() if s['st_shndx']=='SHN_UNDEF' and s.name};needed=[t.needed for t in e.get_section_by_name('.dynamic').iter_tags() if t.entry.d_tag=='DT_NEEDED'];assert e['e_machine']=='EM_ARM' and e.elfclass==32 and e.little_endian and not e['e_flags']&0x400
 missing=sorted(s for s in imports if not s.startswith('X') and s not in exports);assert not missing
 assert sorted(needed)==['libX11.so.6','libc.so.6']
 with tempfile.TemporaryDirectory(prefix='r7-menu-ARM-',dir=W) as td:
  x=Path(td);(x/'libX11.so.6').symlink_to(stub.resolve());env={**os.environ,'LD_LIBRARY_PATH':str(x)}
  p=subprocess.run([str(qemu),'-L',str(libc.parent.parent),str(binary),'--version'],env=env,check=True,capture_output=True,text=True,timeout=10)
  assert 'MENU_SESSION_REARM=RESOURCE_REARM_R7' in p.stdout and 'MENU_CAMERA_RECOVERY=CAMERA_RECOVERY_R5' in p.stdout
 widths={};adv={int(cp):int(a) for cp,a in re.findall(r'^\{(\d+),(\d+),',(R/'src/ui_font.h').read_text(),re.M)}
 for text in ['CHECK RAW RESET','CHECKING RAW RESOURCES - PLEASE WAIT','RAW SESSION READY - TAKE JPEG THEN PREPARE RAW','RESET NOT PROVEN - CHECK REARM LOG']:
  widths[text]=sum(adv.get(ord(c),adv[63]) for c in text);assert widths[text]<=680
 version={'passed':True,'hardware_tested':False,'X11_tested':False,'tests_run':2,'binary_sha256':sha(binary),'binary_bytes':binary.stat().st_size,'NX500_libc_import_audit':{'missing':missing,'libc_sha256':sha(libc),'imports':sorted(imports),'loader_sha256':sha(loader),'needed':needed,'soft_float_ARM_EABI':True},'actual_ARM_version_probe':{'output':p.stdout.splitlines(),'no_hardware_or_UI_calls':True,'runtime':'Original NX500 glibc2.13 and loader; link-only X11 stub resolves imports but version branch calls no X11 function.'},'bitmap_text_widths':widths,'rows':9,'source_sha256':{rel(p):sha(p) for p in [R/'src/menu.c',R/'src/ui_state.h',R/'src/ui_font.h']}}
 (P/'validation/recovery-r7-menu-ARM.json').write_text(json.dumps(version,indent=2)+'\n')
 # Refresh the conventional menu build record consumed by the release gate.
 (P/'validation/menu-build.json').write_text(json.dumps(version,indent=2)+'\n')
 refs=[('menu-rearm-tests.json',25),('menu-camera-recovery-tests.json',13),('menu-full-prepare-tests.json',14),('menu-progress-tests.json',18),('menu-launch-tests.json',8),('status-query-tests.json',10),('recovery-r7-menu-ARM.json',2)]
 sources={};artifacts=[]
 for name,count in refs:
  path=P/'validation'/name;record=json.loads(path.read_text());assert record['passed'] and record['tests_run']==count
  for key,want in record.get('source_sha256',{}).items():assert sha(P/key)==want,(name,key);sources[key]=want
  artifacts.append({'path':rel(path),'sha256':sha(path),'tests_run':count})
 for name in ['build_zig.py','test_menu_rearm.py','test_menu_camera_recovery.py','test_menu_full_prepare.py','test_menu_progress.py','test_menu_launch.py','test_status_query.py','collect_recovery_r7_menu.py']:
  path=R/'tools'/name;sources[rel(path)]=sha(path)
 result={'format':'NX51_RECOVERY_R7_MENU_TESTS1','passed':True,'hardware_tested':False,'X11_tested':False,'tests_run':sum(x['tests_run'] for x in artifacts),'source_sha256':sources,'binary':{'path':rel(binary),'sha256':sha(binary),'bytes':binary.stat().st_size},'binary_sha256':sha(binary),'component_evidence_sha256':{x['path']:x['sha256'] for x in artifacts},'test_artifacts':artifacts,'menu_contract':{'rows':9,'rearm_row':8,'eligible_reason':'REAL_KERNEL_RESTART_NOT_VERIFIED','requires_valid_held_unverified_front_status':True,'action':11,'background_mode':4,'shell_mode':'rearm-session','runner_flag':'--rearm','hardware_authority':False,'native_command_calls':0,'UI_forced_unlocks':0,'state_mutation_in_UI':False,'backend_independently_must_verify_resources':True,'popup_ready_ms':12000,'popup_unproven_ms':12000},'bitmap_text_widths':widths,'limits':['Host UI/router tests and a version-only ARM probe do not exercise X11 on the camera or prove resource rearm success.']}
 out=P/'validation/recovery-r7-menu-tests.json';out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'aggregate':str(out),'sha256':sha(out),'tests_run':result['tests_run'],'binary':result['binary']},indent=2))
if __name__=='__main__':main()
