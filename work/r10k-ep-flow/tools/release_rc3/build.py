"""RC2 reproducible menu build, with RC3 inputs and dependency identity binding."""
from pathlib import Path
here=Path(__file__).resolve().parent
s=(here.parent/'formal-rc2-authoring/build.py').read_text('utf8')
s=s.replace("'formal-0.4.0-rc1-work/runtime_identity.py'","'formal-0.4.0-rc2-work/runtime_identity.py'")
s=s.replace('rc1.manifest','rc2.manifest')
s=s.replace("assert 'release-0.4.0-rc1'in s or 'release-0.4.0-rc2'in s","assert 'release-0.4.0-rc2'in s or 'release-0.4.0-rc3'in s")
s=s.replace("s.replace('release-0.4.0-rc1','release-0.4.0-rc2')","s.replace('release-0.4.0-rc2','release-0.4.0-rc3')")
s=s.replace("shutil.copyfile(W.parent/'formal-0.4.0-rc2-work/runtime_identity.py'", "\nfor n in ['gamma-reload.sh','log-lut-policy.sh']:shutil.copyfile(T/'base/nx-lut-lite'/n,M/n)\nshutil.copyfile(W.parent/'formal-0.4.0-rc2-work/runtime_identity.py'")
s=s.replace('PASS RC2 build','PASS RC3 build')
exec(compile(s,str(here/'build.py'),'exec'))
