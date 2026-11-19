from context import *
run('localize',[A/'localize.py'])
run('menu-final',[T/'base/nx-lut-lite/tools/build_zig.py','--zig',ZIG,'--menu-only','--elftools-path',PY])
run('binding',[A/'bind.py'])
