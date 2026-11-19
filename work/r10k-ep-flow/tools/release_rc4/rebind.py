from context import *
import importlib.util,re,shutil
p=W/'runtime_identity.py';s=p.read_text('utf8')
if "REQUIRED[M+'lut-core.sh'].add('LUT_LOAD_SHA')"not in s:
 s=s.replace('def declarations(',"REQUIRED[M+'lut-core.sh'].add('LUT_LOAD_SHA')\nREQUIRED[M+'gamma-reload.sh'].add('NATIVE_REFRESH_SHA')\nREQUIRED[M+'video-mode.sh'].add('NATIVE_REFRESH_SHA')\n\ndef declarations(")
 p.write_text(s,'utf8',newline='\n')
spec=importlib.util.spec_from_file_location('ri',p);ri=importlib.util.module_from_spec(spec);spec.loader.exec_module(ri);ri.bind(S)
manifest=S/'nx-lut-demo/module.manifest';manifest.write_text(''.join(sha(p)+' '+p.relative_to(M).as_posix()+'\n'for p in sorted(M.rglob('*'))if p.is_file()),'utf8',newline='\n')
p=S/'nx-lut-demo/deploy.sh';s=p.read_text('utf8');s,n=re.subn('^MANIFEST_SHA=[0-9a-f]{64}$','MANIFEST_SHA='+sha(manifest),s,flags=re.M);assert n==1;p.write_text(s,'utf8',newline='\n')
audit=ri.audit(S);assert audit['passed'];put(W/'identity-validation.json',audit)
put(W/'build-validation.json',dict(passed=True,menu_sha256=sha(M/'bin/nxl-menu'),keys_sha256=sha(M/'bin/nxl-keys'),new_helpers={n:sha(M/'bin'/n)for n in ['nxl-lutload.arm','nxl-native-refresh.arm']},module_manifest_sha256=sha(manifest),hardware_tested=False))
print('PASS final RC4 dependency binding')
