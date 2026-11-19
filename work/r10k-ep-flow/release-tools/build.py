from context import *
s=(A.parent/'formal-rc2-authoring/build.py').read_text('utf8')
s=s.replace("'--menu-only'","'--keys-menu-only'")
s=s.replace("shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-menu',M/'bin/nxl-menu');", "shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-menu',M/'bin/nxl-menu');shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-keys',M/'bin/nxl-keys');")
s=s.replace("shutil.copyfile(W.parent/'formal-0.4.0-rc1-work/runtime_identity.py',W/'runtime_identity.py')", """shutil.copyfile(W.parent/'formal-0.4.0-rc4-work/runtime_identity.py',W/'runtime_identity.py')
for n in ['lut-core.sh','gamma-reload.sh']:shutil.copyfile(T/'base/nx-lut-lite'/n,M/n)
""")
s=s.replace('rc1.manifest','rc4.manifest').replace("assert 'release-0.4.0-rc1'in s or 'release-0.4.0-rc2'in s","assert 'release-0.4.0-rc4'in s or 'release-0.4.0-rc5'in s").replace("s.replace('release-0.4.0-rc1','release-0.4.0-rc2')","s.replace('release-0.4.0-rc4','release-0.4.0-rc5')")
exec(compile(s,str(A/'build.py'),'exec'))
