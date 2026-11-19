from context import *
sys.path.insert(0,str(PY))
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
import struct,io
P=W.parent/'formal-0.4.0-rc2-work/video-reload-research/di-camera-app';blob=P.read_bytes();elf=ELFFile(io.BytesIO(blob));md=Cs(CS_ARCH_ARM,CS_MODE_ARM);md.skipdata=True
syms={}
for sec in elf.iter_sections():
 if sec.header.sh_type in ['SHT_DYNSYM','SHT_SYMTAB']:
  for x in sec.iter_symbols():
   if x.entry.st_value:syms[x.entry.st_value&~1]=x.name
def data(at,n):
 for seg in elf.iter_segments():
  h=seg.header
  if h.p_vaddr<=at and at+n<=h.p_vaddr+h.p_filesz:return blob[h.p_offset+at-h.p_vaddr:h.p_offset+at-h.p_vaddr+n]
 raise ValueError(hex(at))
def text(at):
 try:
  b=data(at,160).split(b'\0')[0]
  return b.decode('ascii')if len(b)>1 and all(32<=c<127 for c in b)else''
 except(ValueError,UnicodeError):return''
lines=[]
for start,size in [(0x281f80,0x180),(0x1ff988,0x70),(0x2729fc,0x1c),(0x272778,0x268),(0x2779dc,0x380),(0x273e6c,0x58),(0x2740b8,0xc00),(0x2787bc,0x800),(0x2753b4,0x600),(0x286904,0x11c0)]:
 lines.append('\nFUNCTION '+hex(start)+' '+syms.get(start,''));prev=None
 for i in md.disasm(data(start,size),start):
  extra=''
  if i.mnemonic in ['b','bl']and i.op_str.startswith('#'):extra=' ; '+syms.get(int(i.op_str[1:],0),'')
  if prev and prev.mnemonic=='movw'and i.mnemonic=='movt':
   a=prev.op_str.split(', #');b=i.op_str.split(', #')
   if len(a)==2 and len(b)==2 and a[0]==b[0]:
    at=int(a[1],0)|(int(b[1],0)<<16);extra+=' ; '+hex(at)+' '+syms.get(at,'')+' '+text(at)
  lines.append(hex(i.address)+': '+i.mnemonic+' '+i.op_str+extra);prev=i
v=W/'research';v.mkdir(exist_ok=True);(v/'resolution-disassembly.txt').write_text('\n'.join(lines),'utf8')
table=[]
for at in range(0x44be9c,0x44c01c,24):
 vals=struct.unpack('<6I',data(at,24));table.append(dict(address=hex(at),words=[hex(x)for x in vals],name=text(at)))
put(v/'video-resolution-table.json',table)
put(v/'command-symbols.json',[dict(address=hex(at),name=name)for at,name in syms.items()if any(s in name.lower()for s in ['command','cmd','test','system','message','refresh'])])
print('PASS static OEM video command analysis',flush=True)
