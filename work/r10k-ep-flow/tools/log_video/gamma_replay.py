"""Real NX500 1.12 Gamma instructions with modeled RAM/libc, never the ISP."""
from pathlib import Path
import struct,sys,hashlib
class GammaModel:
 def __init__(self,p7,packages):
  sys.path.insert(0,str(packages))
  from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
  from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
  self.reg=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3];self.sp=UC_ARM_REG_SP;self.lr=UC_ARM_REG_LR;self.pc=UC_ARM_REG_PC
  rom=Path(p7).read_bytes();assert hashlib.sha256(rom).hexdigest()=='f8e417cb5b55768851ba7e2b8017d3a98dc0b0c8444cc0a5061265cc08dbbf7f'
  self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM);self.block=None;self.writes=[]
  for a,n in [(0x80000000,0x1000000),(0x81000000,0x3000000),(0xbb710000,0x10000)]:self.u.mem_map(a,n)
  self.u.mem_write(0x80000000,rom);self.u.hook_add(UC_HOOK_CODE,self.code);self.u.hook_add(UC_HOOK_MEM_WRITE,self.write)
 def put(self,a,v):self.u.mem_write(a,struct.pack('<'+'I'*len(v),*[x&0xffffffff for x in v]))
 def code(self,u,pc,n,_):
  if pc in [0x80509eec,0x8050a114]:
   a,b,n=[u.reg_read(r)for r in self.reg[:3]];assert n<=65536
   u.mem_write(a,bytes(u.mem_read(b,n))if pc==0x80509eec else bytes([b&255])*n)
   u.reg_write(self.reg[0],a);u.reg_write(self.pc,u.reg_read(self.lr))
  elif pc==0x8017cf88:self.block=bytes(u.mem_read(u.reg_read(self.reg[1]),432));u.emu_stop()
 def write(self,u,access,a,n,v,_):
  if 0xbb710000<=a<0xbb720000:self.writes.append((a,n))
 def call(self,pc,*args):
  self.u.reg_write(self.sp,0x83ff0000);self.u.reg_write(self.lr,0x83fffff0)
  for r,n in zip(self.reg,args):self.u.reg_write(r,n)
  self.u.emu_start(pc,0x83fffff0,count=2000000)
 def calculate(self,bank,x,y):
  self.put(0x82000000,[0x8062f6b0,0,0x82002000,0x82003000,0,0,0x82004000,5]);self.put(0x82004000,[3,5,4,13,2]);self.put(0x8200300c,[bank<<8]);self.put(0x82002000,[2]);self.put(0x813ebf50,[0xbb71b300,0xbb71d300])
  self.put(0x82000edc,x);self.put(0x82000f28,y);self.call(0x8038f90c,0x82000000,(5<<29)|(10<<24))
  assert self.block is not None;self.effective=list(struct.unpack('<39I',bytes(self.u.mem_read(0x82000e3c,156))));return self.block
 def submit(self,bank):
  assert self.block is not None;self.u.mem_write(0x82005000,self.block);self.writes=[]
  self.call(0x8049e4d4,bank,0x82005000);target=[0xbb71d228,0xbb71f228][bank]
  assert self.u.reg_read(self.pc)==0x83fffff0 and self.u.reg_read(self.reg[0])==0
  assert bytes(self.u.mem_read(target,432))==self.block and self.writes==[(v,4)for v in range(target,target+432,4)]
