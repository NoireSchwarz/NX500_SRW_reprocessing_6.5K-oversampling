"""Finalize the narrow RC3 changes over the immutable RC2 baseline."""
from context import *
import re
# Confirm the tested production control flow still matches after final hash binding.
test_runtime=W/'tests/engine-1/runtime';rr=W/'tests/engine-1/run';version=W/'tests/engine-1/version.info'
def posix(p):
 s=p.as_posix();return '/'+s[0].lower()+s[2:]
def canonical(text):
 text=text.replace(posix(rr),'/tmp/nx-lut-lite').replace(posix(version),'/etc/version.info')
 return re.sub(r'(?m)^([A-Z0-9_]*SHA(?:256)?)=[0-9a-f]{64}$',r'\1=<bound>',text)
rows=[]
for name in ['lut.sh','lut-core.sh','gamma-reload.sh','log-lut-policy.sh']:
 a=(test_runtime/name).read_text('utf8');b=(M/name).read_text('utf8');assert canonical(a)==canonical(b),name
 rows.append(dict(script=name,tested_runtime_sha256=sha(test_runtime/name),final_production_sha256=sha(M/name),
  body_identical_after_fixture_paths_and_bound_component_hashes=True))
put(W/'log-final-binding-validation.json',dict(passed=True,scripts=rows,final_component_hashes_verified_separately=True))
s=(A.parent/'formal-rc2-authoring/finalize.py').read_text('utf8').replace('RC2','RC3').replace('deploy-rc2.sh','deploy-rc3.sh')
start=s.index('needed=');end=s.index('\nfor name in needed:',start)
s=s[:start]+"needed=['input-validation.json','UI-validation.json','log-transition-validation.json','log-final-binding-validation.json','UI-visual-validation.json','installer-validation.json','identity-validation.json','build-validation.json','LUT-import-validation.json','XH-conversion-validation.json','catalog-validation.json']"+s[end:]
s=s.replace("for folder in ['native','looks']:","for folder in ['native']:")
s=s.replace("assert semantic==['auto-init.sh']","assert semantic==['gamma-reload.sh','log-lut-policy.sh']")
s=s.replace('len(catalog)==19','len(catalog)==32').replace("sum(x['input']=='STD'for x in catalog)==12","sum(x['input']=='STD'for x in catalog)==25")
s='\n'.join(line for line in s.split('\n')if not line.startswith("assert sha(W/'video-reload-research/di-camera-app')"))
s=s.replace('core_library_byte_exact_RC1','core_library_byte_exact_RC2').replace('RTOS_Gamma_SRW_native_pipeline_byte_exact_RC1','RTOS_Gamma_SRW_native_pipeline_byte_exact_RC2').replace('LUT_data_byte_exact_RC1','previous_19_LUTs_byte_exact_RC2')
s=s.replace("shutil.copyfile(M/'README_zh.md',source/'base/nx-lut-lite/README_zh.md')", "shutil.copyfile(M/'README_zh.md',source/'base/nx-lut-lite/README_zh.md')\nshutil.copytree(M/'looks',source/'base/nx-lut-lite/looks',dirs_exist_ok=True)")
s=s.replace("'changes.json'","'preparation.json'")
start=s.index("research=evidence/'video-reload-research'");end=s.index("put(evidence/'inherited-RC1-evidence.json'",start);s=s[:start]+s[end:]
s=s.replace('inherited-RC1-evidence.json','inherited-RC2-evidence.json').replace('previous_RC1_module_manifest_sha256','previous_RC2_module_manifest_sha256')
s=s.replace('RC3_VIDEO_RELOAD_RESEARCH_zh.txt','RC3_LUT_CATALOG_zh.txt')
exec(compile(s,str(A/'finalize.py'),'exec'))
