from context import *
U=T/'base/nx-lut-lite/src';p=U/'menu.c'
s=(A/'prepare.py').read_text('utf8');s=s[s.index("patch(p,'task_child_watch_detach();'"):]
exec(compile(s,str(A/'prepare.py')+'-remaining','exec'))
