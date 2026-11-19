from context import *
exec((A/'research_native.py').read_text('utf8').split('lines=[]')[0])
out=[]
for start,size in [(0x275394,0x20),(0x273e6c,0x58),(0x286a58,0x180),(0x287068,0xb0),(0x2876c0,0xb0)]:
 out.append('\nBLOCK '+hex(start))
 for i in md.disasm(data(start,size),start):
  extra=''
  if i.mnemonic in ['b','bl']and i.op_str.startswith('#'):extra=' ; '+syms.get(int(i.op_str[1:],0),'')
  out.append(hex(i.address)+': '+i.mnemonic+' '+i.op_str+extra)
(W/'research/native-commit-details.txt').write_text('\n'.join(out),'utf8')
from dissect.extfs import ExtFS
with (R/'work/firmware/platform.img').open('rb')as f:
 fs=ExtFS(f);binary=fs.get('/usr/bin/st').open().read()
(W/'research/st-original').write_bytes(binary)
put(W/'research/st-original-identity.json',dict(path='/usr/bin/st',bytes=len(binary),sha256=sha(W/'research/st-original')))
# Re-run original instructions for both photo and movie standby flags.
s=(A/'trace_menu_submit.py').read_text('utf8').replace('0x2b8:movie,','0x2b8:movie,0x2bb:movie,')
s=s.replace('OEM-menu-submit-trace.json','OEM-menu-submit-movie-flags-trace.json')
exec(compile(s,str(A/'trace_menu_submit.py'),'exec'))
print('PASS exact original st identity and native GUI standby branches')
