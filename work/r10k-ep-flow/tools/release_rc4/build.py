from context import *
import shutil,importlib.util,re
for n in ['nxl-native-refresh-main.c']:
 shutil.copyfile(A/n,T/'core/src/nxl-native-refresh.c')
for n in ['nxl-lutload','nxl-native-refresh']:
 host(n,T/'core/src'/(n+'.c'),M/'bin'/(n+'.arm'))
# Reuse font generation and audited package ownership from RC2, with RC4 inputs.
s=(A.parent/'formal-rc2-authoring/build.py').read_text('utf8')
s=s.replace("'--menu-only'","'--keys-menu-only'")
s=s.replace("shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-menu',M/'bin/nxl-menu');", "shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-menu',M/'bin/nxl-menu');shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-keys',M/'bin/nxl-keys');")
s=s.replace("shutil.copyfile(W.parent/'formal-0.4.0-rc1-work/runtime_identity.py',W/'runtime_identity.py')", """shutil.copyfile(W.parent/'formal-0.4.0-rc3-work/runtime_identity.py',W/'runtime_identity.py')
p=W/'runtime_identity.py';identity=p.read_text('utf8');identity=identity.replace("LIVE={", "LIVE={'LUT_LOAD_SHA':M+'bin/nxl-lutload.arm','NATIVE_REFRESH_SHA':M+'bin/nxl-native-refresh.arm',");p.write_text(identity,'utf8',newline='\\n')
for n in ['lut-core.sh','log-lut-policy.sh','gamma-reload.sh','memory-test.sh','video-mode.sh']:shutil.copyfile(T/'base/nx-lut-lite'/n,M/n)
""")
s=s.replace('rc1.manifest','rc3.manifest').replace("assert 'release-0.4.0-rc1'in s or 'release-0.4.0-rc2'in s", "assert 'release-0.4.0-rc3'in s or 'release-0.4.0-rc4'in s").replace("s.replace('release-0.4.0-rc1','release-0.4.0-rc2')", "s.replace('release-0.4.0-rc3','release-0.4.0-rc4')")
exec(compile(s,str(A/'build.py'),'exec'))
