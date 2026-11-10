from pathlib import Path
import hashlib,json,os,shutil,stat,subprocess,sys,tempfile,zipfile
W=Path(__file__).resolve().parent;R=W/'r10k-ep-flow';B=W/'r10j-frame-flow-full-v1';P=W/'r10k-ep-flow-full-v1';O=W.parent/'outputs';V=R/'validation/current';M='nx-lut-demo/nx-lut-lite/';EV='EVIDENCE/R10K_EP_FLOW/'
sys.path[:0]=[str(W),str(R/'tools')]
from runtime_identity import bind,audit,sha,PATTERN,target
from audit_camera_manifest import audit_package
from validate_package_bootstrap import validate

def cp(src,n):p=P/n;p.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,p)
def put(n,s):p=P/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf8',newline='\n')

def evidence(final=False):
 names=['frozen-baseline.json','video-probe-os.json','path-reader.json','flow-production.json','ep-contract.json','ep-decoder.json']
 if final:names+=['runtime.json','prior-installed-upgrade.json']
 for name in names:
  assert json.loads((V/name).read_text('utf8'))['passed'];cp(V/name,EV+name)
 for name in ['start_r10k.py','build_r10k.py','test_r10k_probe_os.py','test_r10k_path_probe.py','test_r10k_flow_production.py','test_r10k_ep_contract.py','test_r10k_ep_decode.py','test_r10k_runtime.py','validate_r10k_install.py','prepare_r10k_metadata.py','package_r10k.py']:
  cp(W/name,'SOURCE/packaging/'+name)
 cp(O/'R10k_测试步骤与更新说明.txt','R10k_更新说明.txt')
 cp(O/'R10k_测试步骤与更新说明.txt',EV+'TEST_STEPS.txt')
 for name in ['video_ep_readback.py','analyze_video_counters.py']:
  cp(R/'tools'/name,'PC/video-stage-counters/'+name)
 cp(R/'base/nx-lut-lite/data/video-probe-schema.json','PC/video-stage-counters/video-probe-schema.json')

if '--finalize'not in sys.argv:
 old=json.loads((O/'R10j_FRAME_FLOW_PACKAGE_VALIDATION.json').read_text('utf8'));assert sha(O/old['archive'])==old['sha256']and audit_package(B)['passed']
 fresh=not P.exists();assert fresh or '--refresh'in sys.argv
 if fresh:
  shutil.copytree(B,P)
  cp(B/'nx-lut-demo/module.manifest','nx-lut-demo/ownership/r10j.manifest')
  put('nx-lut-demo/ownership/index.sha256',sha(P/'nx-lut-demo/ownership/r10j.manifest')+' r10j.manifest\n'+(B/'nx-lut-demo/ownership/index.sha256').read_text('utf8'))
 names=[line.decode().split(' ',1)[1]for line in(B/'nx-lut-demo/module.manifest').read_bytes().splitlines()]
 for name in names:
  source=R/'base/nx-lut-lite'/name
  if source.is_file()and(not name.startswith('bin/')or name in ['bin/nxl-menu','bin/nxl-video-probe.arm']):cp(source,M+name)
 cp(R/'installer/deploy.sh','nx-lut-demo/deploy.sh');cp(R/'installer/install-entry.sh','install.sh')
 for name in ['README_zh.md','DEMO_SCOPE_zh.md',M+'README_zh.md']:put(name,(O/'R10k_测试步骤与更新说明.txt').read_text('utf8'))
 skip={'bin','build','results','validation','__pycache__','.git','.zig-cache','zig-cache'}
 for source in R.rglob('*'):
  rel=source.relative_to(R)
  if source.is_file()and not any(part in skip or part.startswith('build-')for part in rel.parts):cp(source,'SOURCE/demo-p52-recovery-r7/'+rel.as_posix())
 evidence();bind(P)
 module=sorted(p.relative_to(P/M).as_posix()for p in(P/M).rglob('*')if p.is_file());assert len(module)==64
 put('nx-lut-demo/module.manifest',''.join(sha(P/M/n)+' '+n+'\n'for n in module))
 n='nx-lut-demo/deploy.sh';put(n,PATTERN.sub(lambda m:m[1]+'='+sha(P/target(n,m[1])),(P/n).read_text('utf8')))
 assert audit(P)['passed']and audit_package(P,include_sums=False)['passed']
 print('Staged complete R10k EP+three-mode package; 64 modules; R10j full-width/RTOS/blue/USB/SRW frozen')
 raise SystemExit

evidence(True);(R/'validation/r7h').mkdir(parents=True,exist_ok=True);assert validate(P)['passed'];identity=audit(P);assert identity['passed']and len(identity['checks'])==47
for path in P.rglob('*.sh'):
 result=subprocess.run(['C:/Program Files/Git/usr/bin/sh.exe','-n',str(path.resolve())],capture_output=True);assert result.returncode==0,path
for path in(B/M/'bin').iterdir():
 if path.is_file()and path.name not in ['nxl-menu','nxl-video-probe.arm']:assert sha(path)==sha(P/M/'bin'/path.name)
info=json.loads((B/'BUILD.json').read_text('utf8'))
info.update(format='NXL_R10K_EP_FLOW_TEST1',install_revision='recovery-r10k-ep-flow',sample_schema=7,sample_record_bytes=1280,sampler_RAM_bytes=7696384,
 EP_readback_reinvestigation=True,EP_readback_reopened_by_user=True,EP_readback_cleared=False,
 EP_readback_test='Passive double-read auxiliary RDMA1/WDMA1 bank0/bank1, bracketed by IPC header and mode; matched flow/IRQ/DDR evidence',
 EP_aux_disable_AB_implemented=False,EP_readback_record_word_offset=287,EP_readback_record_words=32,
 EP_new_bounded_reads_per_sample_max=12,RTOS_machine_code_identical_to_R10j=True,new_device_writes=False,
 frame_drop_fixed=False,camera_hardware_tested=False,SD_deployed=False)
put('BUILD.json',json.dumps(info,indent=2)+'\n');put('installer-identities.json',json.dumps(identity,indent=2)+'\n')
files=sorted(p for p in P.rglob('*')if p.is_file()and p.relative_to(P).as_posix()!='SHA256SUMS')
put('SHA256SUMS',''.join(sha(p)+'  '+p.relative_to(P).as_posix()+'\n'for p in files));assert audit_package(P)['passed']
archive=O/'NX500_R10k_EP_Flow_full.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=False)as z:
 for path in sorted(p for p in P.rglob('*')if p.is_file()):
  n=path.relative_to(P).as_posix();i=zipfile.ZipInfo(n,(2026,11,10,1,0,0));i.create_system=0;i.external_attr=0x20;i.compress_type=zipfile.ZIP_DEFLATED;z.writestr(i,path.read_bytes())
with zipfile.ZipFile(archive)as z:
 assert z.testzip()is None;names=z.namelist();assert len(names)==len(set(n.casefold()for n in names))
 checks={n:h for h,n in(line.split('  ',1)for line in z.read('SHA256SUMS').decode().splitlines())}
 assert set(checks)==set(names)-{'SHA256SUMS'}
 for n,h in checks.items():assert hashlib.sha256(z.read(n)).hexdigest()==h
with tempfile.TemporaryDirectory(prefix='nx500_ep_unpack_')as temporary:
 folder=Path(temporary).resolve();temp_root=Path(tempfile.gettempdir()).resolve()
 assert folder.is_relative_to(temp_root)and folder.name.startswith('nx500_ep_unpack_')
 assert max(len(str(folder/n))for n in names)<260
 result=subprocess.run(['C:/Windows/System32/tar.exe','-xf',str(archive.resolve()),'-C',str(folder)],capture_output=True);assert result.returncode==0,result.stderr
 for n,h in checks.items():assert sha(folder/n)==h,n
 assert folder.is_relative_to(temp_root)and folder.name.startswith('nx500_ep_unpack_')
digest=sha(archive);short=Path('C:/Users/qwer0/Documents/Codex')/archive.name
assert not short.exists();shutil.copyfile(archive,short);assert sha(short)==digest
report=dict(passed=True,archive=archive.name,download=str(short),sha256=digest,bytes=archive.stat().st_size,zip_entries=len(names),module_files=64,identity_pins=47,
 Windows_native_extraction_verified=True,RTOS_fullwidth_SRW_USB_blue_unchanged_from_R10j=True,
 sample_schema=7,sample_record_bytes=1280,header_bytes=16384,sampler_RAM_bytes=7696384,
 EP_readback_reopened=True,EP_aux_disable_AB_implemented=False,no_hot_SD_writes=True,no_new_device_writes=True,
 frame_drop_fixed=False,camera_hardware_tested=False,SD_deployed=False)
(O/'R10k_EP_FLOW_PACKAGE_VALIDATION.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
(O/(archive.stem+'.sha256')).write_text(digest+'  '+archive.name+'\n',encoding='ascii')
state=dict(release='R10k',state='LOCAL_PACKAGE_VERIFIED',archive=str(archive.resolve()),download=str(short),archive_sha256=digest,SD_accessed=False,SD_deployed=False,camera_install_executed=False,camera_hardware_tested=False,EP_readback_cleared=False,EP_aux_disable_AB_implemented=False)
for path in [W/'r10k-state.json',O/'R10k_EP_FLOW_RELEASE_STATE.json']:path.write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps(report,ensure_ascii=False,indent=2))
