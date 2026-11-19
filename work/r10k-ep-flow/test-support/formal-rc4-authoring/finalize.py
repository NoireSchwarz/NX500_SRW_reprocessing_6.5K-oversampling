from context import *
import importlib.util,re,shutil,zipfile,time
needed=['helpers-validation.json','UI-validation.json','UI-visual-validation.json','engine-shell-validation.json','installer-validation.json','identity-validation.json','build-validation.json']
for n in needed:assert json.loads((W/n).read_text('utf8'))['passed'],n
spec=importlib.util.spec_from_file_location('ri',W/'runtime_identity.py');ri=importlib.util.module_from_spec(spec);spec.loader.exec_module(ri);assert ri.audit(S)['passed']
sys.path.insert(0,str(R/'work'));from audit_camera_manifest import audit_package
assert audit_package(S,include_sums=False)['passed']
inst=json.loads((W/'installer-validation.json').read_text('utf8'));assert inst['production_deploy_sha256']==sha(S/'nx-lut-demo/deploy.sh')and inst['module_manifest_sha256']==sha(S/'nx-lut-demo/module.manifest')
changed=[];new=[];unchanged=[]
for p in sorted((M/'bin').iterdir()):
 if not p.is_file():continue
 b=B/'nx-lut-demo/nx-lut-lite/bin'/p.name
 (new if not b.exists()else unchanged if sha(b)==sha(p)else changed).append(p.name)
assert changed==['nxl-keys','nxl-menu']and new==['nxl-lutload.arm','nxl-native-refresh.arm'],(changed,new)
for folder in ['native','looks']:
 for p in (M/folder).rglob('*'):
  if p.is_file():assert sha(p)==sha(B/'nx-lut-demo/nx-lut-lite'/p.relative_to(M)),p
body=lambda s:re.sub(r'(?m)^([A-Z0-9_]*SHA(?:256)?)=[0-9a-f]{64}$',r'\1=<bound>',s)
semantic=[]
for p in sorted(M.glob('*.sh')):
 b=B/'nx-lut-demo/nx-lut-lite'/p.name;assert b.is_file()
 if body(p.read_text('utf8'))!=body(b.read_text('utf8')):semantic.append(p.name)
assert semantic==['gamma-reload.sh','log-lut-policy.sh','lut-core.sh','memory-test.sh','video-mode.sh'],semantic
for p in [S/'install.sh',S/'nx-lut-demo/deploy.sh',*M.glob('*.sh')]:
 q=subprocess.run([r'C:\Program Files\Git\usr\bin\sh.exe','-n',str(p)],capture_output=True,text=True);assert q.returncode==0,(p,q.stderr)
engine=json.loads((W/'engine-shell-validation.json').read_text('utf8'));last=Path(engine['commands'][0].get('fixture',''))
# Locate the last tested runtime and verify all tested shell bodies after
# path substitution and final dependency binding. This never mutates a test.
fixtures=sorted((W/'tests').glob('engine-*/runtime'),key=lambda p:p.stat().st_mtime_ns);test=fixtures[-1];root=test.parent
def posix(p):s=p.as_posix();return '/'+s[0].lower()+s[2:]
def canonical(s):return body(s.replace(posix(root/'run'),'/tmp/nx-lut-lite').replace(posix(root/'version.info'),'/etc/version.info'))
for n in ['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh','video-mode.sh']:assert canonical((test/n).read_text('utf8'))==canonical((M/n).read_text('utf8')),n
put(W/'baseline-invariance.json',dict(passed=True,unchanged_native_binaries=unchanged,changed_binaries=changed,new_binaries=new,semantic_shell_changes=semantic,core_RTOS_Gamma_SRW_native_and_all_32_LUTs_byte_exact_RC3=True,tested_shell_body_matches_final=True,hardware_tested=False))
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'))['looks'];assert len(catalog)==32
for x in catalog:assert x['tone']and x['scene']and sha(S/'looks'/(x['id']+'.bin'))==x['native_sha256']==sha(M/'looks'/(x['id']+'.bin'))
# Avoid publishing files that only acquired platform newline normalization.
before=json.loads((W/'source-before.json').read_text('utf8'))
for rel,h in before.items():
 p=T/rel;q=SRC/rel
 if not p.is_file()or not q.is_file()or sha(q)!=h or sha(p)==h:continue
 try:same=p.read_text('utf8')==q.read_text('utf8')
 except UnicodeError:same=False
 if same:shutil.copyfile(q,p)
source=S/'SOURCE/FORMAL_0_4_RC4';source.mkdir(parents=True,exist_ok=True)
for folder in ['base/nx-lut-lite/src','base/nx-lut-lite/vendor','base/nx-lut-lite/tools','base/nx-lut-lite/data','core/src','core/tools','core/tests','common']:
 shutil.copytree(T/folder,source/folder,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for p in M.glob('*.sh'):shutil.copyfile(p,source/'base/nx-lut-lite'/p.name)
shutil.copyfile(M/'README_zh.md',source/'base/nx-lut-lite/README_zh.md')
shutil.copytree(A,source/'release-tools',dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for n in ['formal-rc1-authoring','formal-rc2-authoring','formal-rc3-authoring']:
 shutil.copytree(A.parent/n,source/'test-support'/n,dirs_exist_ok=True,ignore=shutil.ignore_patterns('__pycache__','*.pyc','upstream'))
shutil.copytree(W/'research/Metol',source/'upstream/Metol',dirs_exist_ok=True)
shutil.copyfile(W/'runtime_identity.py',source/'runtime_identity.py')
evidence=S/'EVIDENCE/RC4';evidence.mkdir(parents=True,exist_ok=True)
for n in needed+['baseline-invariance.json','preparation.json','source-before.json','git-before.json','engine-shell-before-queue-order.json']:shutil.copyfile(W/n,evidence/n)
shutil.copytree(W/'UI-preview',evidence/'UI-preview',dirs_exist_ok=True)
for p in(W/'research').iterdir():
 if p.suffix in ['.json','.txt']:shutil.copyfile(p,evidence/p.name)
put(evidence/'inherited-RC3-evidence.json',dict(passed=True,previous_RC3_manifest_sha256=sha(B/'nx-lut-demo/module.manifest'),scope='unchanged LUT math/native SRW/Gamma/RTOS/core; RC3 input lease verified by user',not_repeated_hardware_acceptance=True))
build=json.loads((S/'BUILD.json').read_text('utf8'));build.update(format='NXL_RELEASE_RC4',runtime_revision='release-0.4.0-rc4',video_auto_reload_enabled=True,module_manifest_sha256=sha(S/'nx-lut-demo/module.manifest'));put(S/'BUILD.json',build)
P=W/'release-payload';P.mkdir(exist_ok='--refresh'in sys.argv)
for p in sorted(S.rglob('*')):
 if not p.is_file():continue
 rel=p.relative_to(S)
 if rel.parts[0]=='NXDEMO51':continue
 q=P/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
(P/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.relative_to(P).as_posix()+'\n'for p in sorted(P.rglob('*'))if p.is_file()and p.name!='SHA256SUMS'),'utf8',newline='\n')
assert ri.audit(P)['passed'];audit=audit_package(P);assert audit['passed'];put(W/'camera-byte-audit.json',audit)
archive=W/'NX_LUT_Lite_0.4.0_RC4_full.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as z:
 for p in sorted(P.rglob('*')):
  if not p.is_file():continue
  info=zipfile.ZipInfo(p.relative_to(P).as_posix(),time.gmtime(p.stat().st_mtime)[:6]);info.compress_type=zipfile.ZIP_DEFLATED;info.create_system=3
  exe=p.suffix in ['.sh','.arm']or p.name in ['nxl-menu','nxl-keys','nxl-fileops','patch-fileops'];info.external_attr=((0o100755 if exe else 0o100644)<<16);z.writestr(info,p.read_bytes())
with zipfile.ZipFile(archive)as z:assert z.testzip()is None and not any(n.startswith('NXDEMO51/')for n in z.namelist())
rec=dict(passed=True,release='0.4.0-RC4',payload=str(P),archive=str(archive),bytes=archive.stat().st_size,sha256=sha(archive),files=len([p for p in P.rglob('*')if p.is_file()]),module_manifest_sha256=sha(P/'nx-lut-demo/module.manifest'),checks=needed,archive_CRC_passed=True,hardware_tested=False)
put(W/'final-package-validation.json',rec)
out=W.parent/'outputs';out.mkdir(exist_ok=True)
for p in [archive,*S.glob('RC4_*.txt'),W/'final-package-validation.json']:shutil.copyfile(p,out/p.name);assert sha(out/p.name)==sha(p)
print('PASS final RC4',rec['files'],'files',rec['bytes'],'bytes',rec['sha256'],flush=True)
