from context import *
import ast,importlib.util,re,shutil
U=T/'base/nx-lut-lite'
for p in U.glob('*.sh'):
 if (M/p.name).exists()or p.name in ['lut-core.sh','log-lut-policy.sh','finish-srw.sh','srw-log-policy.sh']:
  shutil.copyfile(p,M/p.name)
shutil.copyfile(U/'bin/nxl-menu',M/'bin/nxl-menu')
for p in (U/'data').glob('*.json'):
 if p.name in ['looks-info.json','ui-glyphs.json','release-lut-catalog.json']:shutil.copyfile(p,M/'data'/p.name)
text=(W.parent/'r11q-vlog-work/runtime_identity.py').read_text('utf8')
text=text.replace("M='nx-lut-demo/nx-lut-lite/'","M='nx-lut-demo/nx-lut-lite/'")
hook='''
LIVE.update({
 'BLEND_SHA':M+'bin/nxl-lutblend.arm','SHARP_SHA':M+'bin/nxl-sharp.arm',
 'LUT_GUARD_SHA':M+'bin/nxl-lutguard.arm','LUT_CORE_SHA':M+'lut-core.sh',
 'LUT_IDLE_SHA':M+'bin/nxl-lutidle.arm','LOG_LUT_POLICY_SHA':M+'log-lut-policy.sh',
 'LUT_CATALOG_SHA':M+'data/release-lut-allowlist.tsv','SRW_LOG_POLICY_SHA':M+'srw-log-policy.sh'})
REQUIRED[M+'lut.sh']=set(['OPS_SHA','LUT_CORE_SHA','LUT_GUARD_SHA'])
REQUIRED[M+'lut-core.sh']=set(['OPS_SHA','BLEND_SHA','SHARP_SHA','LUT_IDLE_SHA','LOG_LUT_POLICY_SHA','LUT_CATALOG_SHA'])
REQUIRED[M+'gamma-reload.sh'].add('LUT_IDLE_SHA')
REQUIRED[M+'finish-srw.sh']=set(['OPS_SHA','RUNNER_SHA','SRW_LOG_POLICY_SHA'])
REQUIRED[M+'srw-log-policy.sh']=set(['OPS_SHA'])
REQUIRED[M+'auto-init.sh'].add('SRW_LOG_POLICY_SHA')
REQUIRED[M+'memory-test.sh'].add('SRW_LOG_POLICY_SHA')
'''
at=text.index('\ndef declarations(');text=text[:at]+hook+text[at:]
old="order=[M+'lut.sh',M+'gamma-reload.sh',M+'auto-init-capture.sh',M+'lens-capture.sh',M+'rearm-session.sh',M+'restore-camera.sh',M+'handoff-watch.sh',M+'memory-test.sh',M+'auto-init.sh']"
new="order=[M+'lut-core.sh',M+'lut.sh',M+'gamma-reload.sh',M+'auto-init-capture.sh',M+'lens-capture.sh',M+'rearm-session.sh',M+'restore-camera.sh',M+'handoff-watch.sh',M+'memory-test.sh',M+'auto-init.sh',M+'finish-srw.sh']"
assert old in text;text=text.replace(old,new);ast.parse(text)
(W/'runtime_identity.py').write_text(text,'utf8',newline='\n')
for n in ['install.sh','nx-lut-demo/deploy.sh']:
 p=S/n;s=p.read_text('utf8');assert 'recovery-r11q-vlog'in s or 'release-0.4.0-rc1'in s
 p.write_text(s.replace('recovery-r11q-vlog','release-0.4.0-rc1'),'utf8',newline='\n')
owner=S/'nx-lut-demo/ownership';shutil.copyfile(B/'nx-lut-demo/module.manifest',owner/'r11q.manifest')
p=owner/'index.sha256';prior=''.join(x+'\n'for x in p.read_text('utf8').splitlines()if not x.endswith(' r11q.manifest'));p.write_text(sha(owner/'r11q.manifest')+' r11q.manifest\n'+prior,'utf8',newline='\n')
spec=importlib.util.spec_from_file_location('rc1_identity',W/'runtime_identity.py');ri=importlib.util.module_from_spec(spec);spec.loader.exec_module(ri)
bound=ri.bind(S)
manifest=S/'nx-lut-demo/module.manifest';manifest.write_text(''.join(sha(p)+' '+p.relative_to(M).as_posix()+'\n'for p in sorted(M.rglob('*'))if p.is_file()),'utf8',newline='\n')
p=S/'nx-lut-demo/deploy.sh';s=p.read_text('utf8')
for key,path in [('MANIFEST_SHA',manifest),('OWNERSHIP_INDEX_SHA',owner/'index.sha256')]:s,n=re.subn('^'+key+'=[0-9a-f]{64}$',key+'='+sha(path),s,flags=re.M);assert n==1
p.write_text(s,'utf8',newline='\n')
audit=ri.audit(S);assert audit['passed'];put(W/'runtime-identity-validation.json',audit)
put(W/'binding.json',dict(passed=True,changed=bound,module_manifest_sha256=sha(manifest),SD_written=False))
print('PASS complete release dependency identities; no package checksum placeholders remain',flush=True)
