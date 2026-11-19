from pathlib import Path
import sys
here=Path(__file__).resolve().parent
support=here/'rc1-test-support/test_installer.py'
if not support.exists():support=here.parent/'formal-rc1-authoring/test_installer.py'
s=support.read_text('utf8')
s=s.replace("'rc1_identity'","'rc2_identity'").replace("'R11q-current-patched'","'RC1-current-patched'")
if '--current-only'in sys.argv:
 s=s.replace("[(W.parent/'r11p-clean-log-work/stage','R11p-original',R/'work/firmware/libmmfcore.so'),(B,'RC1-current-patched',M/'native/libmmfcore.r11f.so')]", "[(B,'RC1-current-patched',M/'native/libmmfcore.r11f.so')]")
exec(compile(s,str(Path(__file__).resolve()),'exec'))
