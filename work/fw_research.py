"""Read fixed local P7 images; helpers preserve original code/data addresses."""
from pathlib import Path
import struct,sys,re
W=Path(__file__).resolve().parent
sys.path.insert(0,str(W/'tooling/python'))
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM
class Firmware:
 def __init__(self,model='NX500'):
  self.model=model
  self.b=(W/('firmware/P7.bin'if model=='NX500'else'nx1-firmware/P7.bin')).read_bytes()
  self.lma,self.end=(0x79bdc0,0x813ee4c0)if model=='NX500'else(0x772100,0x813c9040)
  self.cs=Cs(CS_ARCH_ARM,CS_MODE_ARM);self.cs.skipdata=True
 def offset(self,a):
  if 0x81006000<=a<self.end:return self.lma+a-0x81006000
  if 0x80000000<=a<0x80000000+len(self.b):return a-0x80000000
  raise ValueError(hex(a))
 def data(self,a,n):o=self.offset(a);return self.b[o:o+n]
 def word(self,a):return struct.unpack('<I',self.data(a,4))[0]
 def string(self,a):
  try:
   o=self.offset(a);e=self.b.index(0,o)
   if e-o>500:return ''
   s=self.b[o:e].decode('ascii')
   return s if all(32<=ord(c)<127 or c in'\n\r\t'for c in s)else''
  except(ValueError,UnicodeDecodeError):return''
 def dump(self,a,n):
  lines=[]
  for i in self.cs.disasm(self.data(a,n),a):
   tail='';m=re.search(r'\[pc(?:, #(-?0x[0-9a-f]+|-?\d+))?\]',i.op_str)
   if i.mnemonic.startswith('ldr')and m:
    at=i.address+8+int(m[1]or'0',0)
    try:v=self.word(at);s=self.string(v);tail=f' ; [{at:08x}]={v:08x}'+(' '+repr(s)if s else'')
    except ValueError:pass
   lines.append(f'{i.address:08x}: {i.mnemonic:8} {i.op_str}'+tail)
  return'\n'.join(lines)
 def references(self,fragment):
  needles={}
  for m in re.finditer(rb'[ -~]{5,}',self.b):
   s=m.group().decode('ascii')
   if fragment in s:needles[m.start()+0x80000000]=s
  pool={o:needles[v]for o in range(0,len(self.b)-3,4)if(v:=struct.unpack_from('<I',self.b,o)[0])in needles}
  rows=[]
  for o in range(0,0x530000,4):
   w=struct.unpack_from('<I',self.b,o)[0]
   if w&0x0f7f0000!=0x051f0000:continue
   a=o+8+((w&4095)if w&0x800000 else -(w&4095))
   if a in pool:rows.append((o+0x80000000,pool[a]))
  return rows
if __name__=='__main__':
 f=Firmware(sys.argv[1])
 if sys.argv[2]=='refs':print(f.references(sys.argv[3]))
 else:print(f.dump(int(sys.argv[2],0),int(sys.argv[3],0)if len(sys.argv)>3 else 256))
