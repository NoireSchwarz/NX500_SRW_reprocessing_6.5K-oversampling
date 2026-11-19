from context import *
import importlib.util,re,shutil,zipfile,time
needed=['UI-validation.json','auto-init-validation.json','UI-visual-validation.json','installer-validation.json','identity-validation.json','build-validation.json']
for name in needed:assert json.loads((W/name).read_text('utf8'))['passed'],name
spec=importlib.util.spec_from_file_location('rc2ri',W/'runtime_identity.py');ri=importlib.util.module_from_spec(spec);spec.loader.exec_module(ri)
assert ri.audit(S)['passed']
sys.path.insert(0,str(R/'work'));from audit_camera_manifest import audit_package
assert audit_package(S,include_sums=False)['passed']
installer=json.loads((W/'installer-validation.json').read_text('utf8'))
assert installer['production_deploy_sha256']==sha(S/'nx-lut-demo/deploy.sh')
assert installer['module_manifest_sha256']==sha(S/'nx-lut-demo/module.manifest')
changed=[];unchanged=[]
for p in sorted((M/'bin').iterdir()):
 if p.is_file():
  old=B/'nx-lut-demo/nx-lut-lite/bin'/p.name;assert old.is_file()
  (unchanged if sha(p)==sha(old)else changed).append(p.name)
assert changed==['nxl-menu'],changed
for folder in ['native','looks']:
 for p in (M/folder).rglob('*'):
  if p.is_file():assert sha(p)==sha(B/'nx-lut-demo/nx-lut-lite'/p.relative_to(M)),p
body=lambda s:re.sub(r'(?m)^([A-Z0-9_]*SHA(?:256)?)=[0-9a-f]{64}$',r'\1=<bound>',s)
semantic=[]
for p in sorted(M.glob('*.sh')):
 old=B/'nx-lut-demo/nx-lut-lite'/p.name;assert old.is_file()
 if body(p.read_text('utf8'))!=body(old.read_text('utf8')):semantic.append(p.name)
assert semantic==['auto-init.sh'],semantic
for p in [S/'install.sh',S/'nx-lut-demo/deploy.sh',*M.glob('*.sh')]:
 result=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe','-n',str(p)],capture_output=True,text=True);assert result.returncode==0,(p,result.stderr)
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks'];assert len(catalog)==19
for row in catalog:
 assert row['tone']and row['scene'];assert sha(S/'looks'/(row['id']+'.bin'))==row['native_sha256']==sha(M/'looks'/(row['id']+'.bin'))
assert sum(x['input']=='STD'for x in catalog)==12 and sum(x['input']=='VLOG'for x in catalog)==7
assert sha(W/'video-reload-research/di-camera-app')=='55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04'
put(W/'baseline-invariance.json',dict(passed=True,unchanged_native_binaries=unchanged,changed_Linux_UI_binary=changed,
 semantic_shell_changes=semantic,core_library_byte_exact_RC1=True,RTOS_Gamma_SRW_native_pipeline_byte_exact_RC1=True,LUT_data_byte_exact_RC1=True,
 source_mutations_published_separately=True,hardware_tested=False))
source=S/'SOURCE/FORMAL_0_4_RC2';source.mkdir(parents=True,exist_ok=True)
for folder in ['base/nx-lut-lite/src','base/nx-lut-lite/vendor','base/nx-lut-lite/tools','base/nx-lut-lite/data','core/src','core/tools','core/tests','common']:
 shutil.copytree(T/folder,source/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in M.glob('*.sh'):shutil.copyfile(p,source/'base/nx-lut-lite'/p.name)
shutil.copyfile(M/'README_zh.md',source/'base/nx-lut-lite/README_zh.md')
shutil.copytree(A,source/'release-tools',dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
support=source/'release-tools/rc1-test-support';support.mkdir(exist_ok=True)
for name in ['render_ui.py','test_installer.py','deploy_sd.py']:shutil.copyfile(A.parent/'formal-rc1-authoring'/name,support/name)
shutil.copyfile(W/'runtime_identity.py',source/'runtime_identity.py')
(source/'installer').mkdir(exist_ok=True)
shutil.copyfile(S/'nx-lut-demo/deploy.sh',source/'installer/deploy-rc2.sh');shutil.copyfile(S/'install.sh',source/'installer/install-entry.sh')
evidence=S/'EVIDENCE/RC2';evidence.mkdir(parents=True,exist_ok=True)
for name in needed+['baseline-invariance.json','source-before.json','changes.json']:
 shutil.copyfile(W/name,evidence/name)
shutil.copytree(W/'UI-preview',evidence/'UI-preview',dirs_exist_ok=True)
research=evidence/'video-reload-research';research.mkdir(exist_ok=True)
for p in (W/'video-reload-research').iterdir():
 if p.suffix in ['.txt','.json']:shutil.copyfile(p,research/p.name)
put(evidence/'inherited-RC1-evidence.json',dict(passed=True,scope='unchanged M3/native/session/LUT math and data; see EVIDENCE/RC1',
 previous_RC1_module_manifest_sha256=sha(B/'nx-lut-demo/module.manifest'),not_repeated_hardware_acceptance=True))
build=json.loads((S/'BUILD.json').read_text('utf8'));build['module_manifest_sha256']=sha(S/'nx-lut-demo/module.manifest');put(S/'BUILD.json',build)
P=W/'release-payload';P.mkdir(exist_ok='--refresh'in sys.argv)
omitted=[]
for p in sorted(S.rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(S)
 if rel.parts[0]=='NXDEMO51':omitted.append(rel.as_posix());continue
 q=P/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
put(W/'package-omissions.json',dict(passed=True,omitted=omitted,reason='working logs and session pointers never shipped'))
(P/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(P).as_posix()+'\n'for p in sorted(P.rglob('*'))if p.is_file()and p.name!='SHA256SUMS'),'utf8',newline='\n')
assert ri.audit(P)['passed'];audit=audit_package(P);assert audit['passed'];put(W/'camera-byte-audit.json',audit)
archive=W/'NX_LUT_Lite_0.4.0_RC2_full.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
 for p in sorted(P.rglob('*')):
  if not p.is_file():continue
  info=zipfile.ZipInfo(p.relative_to(P).as_posix(),time.gmtime(p.stat().st_mtime)[:6]);info.compress_type=zipfile.ZIP_DEFLATED;info.create_system=3
  executable=p.suffix in ['.sh','.arm']or p.name in ['nxl-menu','nxl-keys','nxl-fileops','patch-fileops'];info.external_attr=((0o100755 if executable else 0o100644)<<16);z.writestr(info,p.read_bytes())
with zipfile.ZipFile(archive)as z:
 assert z.testzip()is None;assert len(z.namelist())==len([p for p in P.rglob('*')if p.is_file()]);assert not any(n.startswith('NXDEMO51/')for n in z.namelist())
record=dict(passed=True,release='0.4.0-RC2',payload=str(P),archive=str(archive),bytes=archive.stat().st_size,sha256=sha(archive),
 files=len([p for p in P.rglob('*')if p.is_file()]),module_manifest_sha256=sha(P/'nx-lut-demo/module.manifest'),checks=needed,
 native_invariance=True,archive_CRC_passed=True,working_logs_excluded=True,video_auto_reload_enabled=False,hardware_tested=False)
put(W/'final-package-validation.json',record)
outputs=W.parent/'outputs';outputs.mkdir(exist_ok=True)
for p in [archive,S/'RC2_TEST_STEPS_zh.txt',S/'RC2_VALIDATION_REPORT_zh.txt',S/'RC2_VIDEO_RELOAD_RESEARCH_zh.txt',W/'final-package-validation.json']:
 shutil.copyfile(p,outputs/p.name);assert sha(outputs/p.name)==sha(p)
print('PASS final RC2 package:',record['files'],'files;',record['bytes'],'bytes;',record['sha256'],flush=True)
