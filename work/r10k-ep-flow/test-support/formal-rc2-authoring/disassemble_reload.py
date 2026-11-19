from context import *
sys.path.insert(0,str(PY))
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
import bisect,struct
V=W/'video-reload-research';p=V/'di-camera-app';f=p.open('rb');elf=ELFFile(f);symbols={}
for sec in elf.iter_sections():
 if sec.header.sh_type not in ['SHT_DYNSYM','SHT_SYMTAB']:continue
 for sym in sec.iter_symbols():
  if sym.entry.st_value and sym.entry.st_info.type=='STT_FUNC':symbols[int(sym.entry.st_value)&~1]=(sym.name,int(sym.entry.st_size))
starts=sorted(symbols);md=Cs(CS_ARCH_ARM,CS_MODE_ARM);md.skipdata=True
def data(at,n):
 for seg in elf.iter_segments():
  h=seg.header
  if h.p_vaddr<=at and at+n<=h.p_vaddr+h.p_filesz:
   f.seek(h.p_offset+at-h.p_vaddr);return f.read(n)
 raise ValueError(hex(at))
def owner(at):
 k=bisect.bisect_right(starts,at)-1
 if k<0:return '?'
 lo=starts[k];name,size=symbols[lo]
 return name+'+'+hex(at-lo)if at<lo+size else '? after '+name
targets=[0xe4398,0xe6b1c,0x2e8b44,0x1d7090,0x1d7174]
calls={hex(x):[]for x in targets};text=elf.get_section_by_name('.text')
for ins in md.disasm(text.data(),text.header.sh_addr):
 if ins.mnemonic in ['bl','b']and ins.op_str.startswith('#'):
  at=int(ins.op_str[1:],0)
  if at in targets:calls[hex(at)].append(dict(pc=hex(ins.address),owner=owner(ins.address)))
selected=set(targets)
for rows in calls.values():
 for r in rows:
  at=int(r['pc'],0);k=bisect.bisect_right(starts,at)-1
  if k>=0 and at<starts[k]+symbols[starts[k]][1]:selected.add(starts[k])
lines=[]
for at in sorted(selected):
 name,size=symbols.get(at,('UNEXPORTED',128));lines.append('\nFUNCTION '+hex(at)+' '+name+' size='+str(size))
 for ins in md.disasm(data(at,min(size,3072)),at):
  extra=''
  if ins.mnemonic.startswith(('b','bl'))and ins.op_str.startswith('#'):
   dst=int(ins.op_str[1:],0);extra=' ; '+symbols.get(dst,('?',0))[0]
  if ins.mnemonic=='ldr'and 'pc'in ins.op_str:
   import re
   m=re.search(r'\[pc(?:, #(-?(?:0x)?[0-9a-f]+))?\]',ins.op_str)
   if m:
    off=int(m.group(1),0)if m.group(1)else 0
    try:
     val=struct.unpack('<I',data(ins.address+8+off,4))[0];extra+=' ; literal='+hex(val)
    except ValueError:pass
  lines.append(hex(ins.address)+': '+ins.mnemonic+' '+ins.op_str+extra)
(V/'reload-callchain.txt').write_text('\n'.join(lines)+'\n','utf8')
put(V/'reload-callers.json',calls)
blob=p.read_bytes();strings={}
for needle in [b"ERROR: NOT IN LIVEVIEW STATE. Can't change video resolution",b'resolution [show,name]',b'CHANGE VIDEO RESOLUTION',b'SYSEVT_CAMERA_SET_MOVIE_RESOLUTION']:
 off=blob.find(needle);assert off>=0
 for seg in elf.iter_segments():
  h=seg.header
  if h.p_offset<=off<h.p_offset+h.p_filesz:
   at=h.p_vaddr+off-h.p_offset;strings[at]=needle.decode();break
refs=[];prev=None
for ins in md.disasm(text.data(),text.header.sh_addr):
 if prev and prev.mnemonic=='movw'and ins.mnemonic=='movt':
  a=prev.op_str.split(', #');b=ins.op_str.split(', #')
  if len(a)==2 and len(b)==2 and a[0]==b[0]:
   val=int(a[1],0)+(int(b[1],0)<<16)
   for at,s in strings.items():
    if abs(val-at)<=8:refs.append(dict(pc=hex(prev.address),literal=hex(val),string=s,owner=owner(prev.address)))
 prev=ins
focus=[]
for row in refs:
 at=int(row['pc'],0);focus.append('\nSTRING '+row['string']+' '+row['owner'])
 for ins in md.disasm(data(at-64,192),at-64):
  extra=''
  if ins.mnemonic in ['bl','b']and ins.op_str.startswith('#'):extra=' ; '+symbols.get(int(ins.op_str[1:],0),('?',0))[0]
  focus.append(hex(ins.address)+': '+ins.mnemonic+' '+ins.op_str+extra)
(V/'resolution-command.txt').write_text('\n'.join(focus)+'\n','utf8')
put(V/'resolution-command-refs.json',refs)
print('OEM calls:',calls,flush=True)
f.close()
