"""Run the actual static ARM session verifier with a bounded read-only FS model.

This models Linux file/lock syscalls over disposable fixture bytes. It does not
model the camera, native calls, ptrace, MMIO or firmware resource retirement.
"""
from pathlib import Path,PurePosixPath
import os,stat,struct
from arm_model import ArmModel
from unicorn.arm_const import UC_ARM_REG_R7,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5
class SessionFileModel(ArmModel):
 def __init__(self,binary,args,fixture,busy=False):
  self.fixture=Path(fixture);self.fds={};self.serial=10;self.busy=busy;self.writes=[]
  super().__init__(binary,args)
 def string(self,p):
  b=bytearray()
  for i in range(4096):
   x=bytes(self.uc.mem_read(p+i,1))
   if x==b'\0':return b.decode()
   b.extend(x)
  raise AssertionError('unterminated model path')
 def fixture_path(self,p):
  assert p.startswith('/') and '..' not in PurePosixPath(p).parts,p
  if p=='/mnt/mmc' or p.startswith('/mnt/mmc/'):return self.fixture/'mnt/mmc'/p[len('/mnt/mmc/'): ] if p!='/mnt/mmc' else self.fixture/'mnt/mmc'
  return self.fixture/p.lstrip('/')
 def info(self,p):
  if p in ['/','/mnt','/opt','/opt/usr','/proc','/proc/sys','/proc/sys/kernel','/proc/sys/kernel/random']:return (stat.S_IFDIR|0o700,0)
  q=self.fixture_path(p)
  try:s=q.lstat()
  except FileNotFoundError:return None
  return s.st_mode,s.st_size
 def packed_stat(self,p):
  info=self.info(p)
  if info is None:return None
  mode,size=info;b=bytearray(104)
  struct.pack_into('<Q',b,0,1);struct.pack_into('<I',b,12,1)
  struct.pack_into('<IIII',b,16,mode,1,0,0);struct.pack_into('<q',b,48,size)
  struct.pack_into('<I',b,56,4096);struct.pack_into('<Q',b,64,(size+511)//512);struct.pack_into('<Q',b,96,1)
  return bytes(b)
 def read(self,fd,n):
  f=self.fds[fd];data=f['data'][f['at']:f['at']+n];f['at']+=len(data);return data
 def interrupt(self,uc,number,data):
  n=uc.reg_read(UC_ARM_REG_R7);a=[uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]]
  known=n in [3,5,6,19,55,118,140,143,145,195,196,197,221,397] or n in [4,146] and a[0] in self.fds
  if not known:return super().interrupt(uc,number,data)
  self.calls[n]=self.calls.get(n,0)+1
  if n==397:self.ret(-38) # statx unavailable on the camera's old kernel
  elif n==5:
   p=self.string(a[0]);flags=a[1];q=self.fixture_path(p);access=flags&3
   if access:
    assert p=='/mnt/mmc/NXDEMO51/logs/session.log',('unexpected write-open',p,flags)
    self.writes.append(p);content=b''
   else:
    if self.info(p) is None:self.ret(-2);return
    if q.is_symlink():self.ret(-40);return
    content=q.read_bytes() if q.is_file() else b''
   self.serial+=1;self.fds[self.serial]=dict(path=p,data=content,at=0,flags=flags);self.ret(self.serial)
  elif n==3:
   b=self.read(a[0],a[2]);uc.mem_write(a[1],b);self.ret(len(b))
  elif n==145:
   total=0
   for i in range(a[2]):
    p,size=struct.unpack('<II',uc.mem_read(a[1]+i*8,8));b=self.read(a[0],size)
    if b:uc.mem_write(p,b)
    total+=len(b)
    if len(b)<size:break
   self.ret(total)
  elif n in [4,146]:
   assert self.fds[a[0]]['path'].endswith('/logs/session.log')
   size=a[2] if n==4 else sum(struct.unpack('<II',uc.mem_read(a[1]+i*8,8))[1] for i in range(a[2]));self.ret(size)
  elif n==6:self.fds.pop(a[0]);self.ret(0)
  elif n in [195,196,197]:
   p=self.fds[a[0]]['path'] if n==197 else self.string(a[0]);b=self.packed_stat(p)
   if b is None:self.ret(-2)
   else:uc.mem_write(a[1],b);self.ret(0)
  elif n==143:
   assert a[1] in [1,5],a
   self.ret(-11 if self.busy and self.fds[a[0]]['path'].endswith('/run.lock') else 0)
  elif n in [55,221]:self.ret(self.fds[a[0]]['flags'] if a[1]==3 else 0)
  elif n==118:self.ret(0)
  elif n in [19,140]:
   f=self.fds[a[0]];off=a[1] if n==19 else (a[1]<<32)|a[2];whence=a[2] if n==19 else a[4]
   bits=32 if n==19 else 64
   if off>=1<<(bits-1):off-=1<<bits
   f['at']=(0 if whence==0 else f['at'] if whence==1 else len(f['data']))+off
   if n==140:uc.mem_write(a[3],struct.pack('<Q',f['at']));self.ret(0)
   else:self.ret(f['at'])
