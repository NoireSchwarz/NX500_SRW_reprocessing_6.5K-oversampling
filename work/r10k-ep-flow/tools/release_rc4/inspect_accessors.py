from context import *
sys.path.insert(0,str(PY))
from elftools.elf.elffile import ELFFile
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
import io,struct
md=Cs(CS_ARCH_ARM,CS_MODE_ARM);md.skipdata=True
for fname,targets in [('libuser-spec.so',['User_Spec_Get_Value',0x33cc]),('di-camera-app',[0x280884])]:
 p=W.parent/'formal-0.4.0-rc2-work/video-reload-research'/fname;blob=p.read_bytes();elf=ELFFile(io.BytesIO(blob));syms={}
 for sec in elf.iter_sections():
  if sec.header.sh_type in ['SHT_DYNSYM','SHT_SYMTAB']:
   for s in sec.iter_symbols():
    if s.entry.st_value:syms[s.name]=(s.entry.st_value,s.entry.st_size)
 lines=[]
 def get(at,n):
  for seg in elf.iter_segments():
   h=seg.header
   if h.p_vaddr<=at and at+n<=h.p_vaddr+h.p_filesz:return blob[h.p_offset+at-h.p_vaddr:h.p_offset+at-h.p_vaddr+n]
  raise ValueError(hex(at))
 if fname=='libuser-spec.so':
  for sec in elf.iter_sections():
   if sec.header.sh_type in ['SHT_REL','SHT_RELA']:
    tab=elf.get_section(sec.header.sh_link)
    for rel in sec.iter_relocations():
     if rel.entry.r_offset==0x37178:
      symbol=tab.get_symbol(rel.entry.r_info_sym);print('GETTER_PTR',symbol.name,hex(symbol.entry.st_value));targets.append(int(symbol.entry.st_value))
 for name in targets:
  at,size=syms[name]if isinstance(name,str)else(name,1500)
  for seg in elf.iter_segments():
   h=seg.header
   if h.p_vaddr<=at<h.p_vaddr+h.p_filesz:data=blob[h.p_offset+at-h.p_vaddr:h.p_offset+at-h.p_vaddr+min(size or 500,1500)];break
  lines.append('FUNCTION '+str(name)+' '+hex(at))
  for ins in md.disasm(data,at):lines.append(hex(ins.address)+': '+ins.mnemonic+' '+ins.op_str)
 if fname=='di-camera-app':
  for id in [0x27a,0x2b8]:
   at=struct.unpack('<I',get(0x280894+(id-0xcb)*4,4))[0];case=['CASE '+hex(id)+' '+hex(at)]
   for ins in md.disasm(get(at,32),at):case.append(hex(ins.address)+': '+ins.mnemonic+' '+ins.op_str)
   literal_at=at+8+(0x488 if id==0x27a else 0x394);value=struct.unpack('<I',get(literal_at,4))[0];case.append('POINTER '+hex(value))
   lines+=case;print('\n'.join(case))
  photo=[]
  for i in range(13):
   at=0x44b8c8+i*28;b=get(at,28);photo.append(dict(name=b[:8].split(b'\0')[0].decode('ascii'),words=list(struct.unpack('<7I',b))))
  put(W/'research/photo-resolution-table.json',photo)
 else:
  for id in [0xcf,0x109,0x10a]:
   jump=next(md.disasm(get(0x4938+(id-1)*4,4),0x4938+(id-1)*4));at=int(jump.op_str[1:],0);case=['CASE '+hex(id)+' '+hex(at)]
   for ins in md.disasm(get(at,32),at):case.append(hex(ins.address)+': '+ins.mnemonic+' '+ins.op_str)
   lines+=case;print('\n'.join(case))
 (W/'research'/(fname+'-accessors.txt')).write_text('\n'.join(lines),'utf8')
 print(fname,'\n'+'\n'.join(lines[:28]),flush=True)
