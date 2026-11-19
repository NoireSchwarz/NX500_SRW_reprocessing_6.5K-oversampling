from context import *
import shutil
before=json.loads((W/'source-before.json').read_text('utf8'))
for n in ['memory-test.sh','lut-core.sh']:
 rel='base/nx-lut-lite/'+n;assert sha(SRC/rel)==before[rel];shutil.copyfile(SRC/rel,T/rel)
exec(compile((A/'implement_shell.py').read_text('utf8'),str(A/'implement_shell.py'),'exec'))
