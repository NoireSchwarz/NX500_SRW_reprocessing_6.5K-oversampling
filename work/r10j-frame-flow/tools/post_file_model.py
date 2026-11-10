"""Disposable POSIX file fixture model. Never maps camera devices or executes native calls."""
from pathlib import Path
import os,stat,struct
from session_file_model import SessionFileModel
from unicorn.arm_const import UC_ARM_REG_R7,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5
class PostFileModel(SessionFileModel):
 def interrupt(self,uc,number,data):
  n=uc.reg_read(UC_ARM_REG_R7);a=[uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]]
  custom=n in [5,10,38,39,180,217] or n in [4,146] and a[0] in self.fds
  if not custom:return super().interrupt(uc,number,data)
  self.calls[n]=self.calls.get(n,0)+1
  def path(p):
   q=self.fixture_path(self.string(p)).resolve();assert q.is_relative_to(self.fixture.resolve());return q
  try:
   if n==5:
    text=self.string(a[0]);q=path(a[0]);flags=a[1];access=flags&3
    if q.is_symlink():self.ret(-40);return
    if access:
     if flags&0x80 and q.exists():self.ret(-17);return
     if not q.exists():
      if not flags&0x40:self.ret(-2);return
      q.write_bytes(b'')
     if flags&0x200:q.write_bytes(b'')
    elif not q.exists():self.ret(-2);return
    self.serial+=1;self.fds[self.serial]=dict(path=text,data=q.read_bytes() if q.is_file() else b'',at=0,flags=flags,host=q)
    self.ret(self.serial)
   elif n in [4,146]:
    f=self.fds[a[0]];q=f['host'];assert q.is_file() and f['flags']&3
    chunks=[bytes(uc.mem_read(a[1],a[2]))] if n==4 else [bytes(uc.mem_read(p,l)) for p,l in [struct.unpack('<II',uc.mem_read(a[1]+i*8,8)) for i in range(a[2])]]
    with q.open('r+b') as out:
     out.seek(f['at'])
     for b in chunks:out.write(b);f['at']+=len(b)
    self.ret(sum(map(len,chunks)))
   elif n==180:
    f=self.fds[a[0]];offset=a[4]|(a[5]<<32)
    with f['host'].open('rb') as source:source.seek(offset);b=source.read(a[2])
    if b:uc.mem_write(a[1],b)
    self.ret(len(b))
   elif n==39:path(a[0]).mkdir();self.ret(0)
   elif n==10:path(a[0]).unlink();self.ret(0)
   elif n==38:os.replace(path(a[0]),path(a[1]));self.ret(0)
   elif n==217:
    f=self.fds[a[0]];q=f['host'];names=['.','..']+sorted(p.name for p in q.iterdir());at=f.get('dir_at',0);out=bytearray()
    while at<len(names):
     name=names[at].encode()+b'\0';length=(19+len(name)+7)&~7
     if len(out)+length>a[2]:break
     row=bytearray(length);kind=4 if names[at] in ['.','..'] or (q/names[at]).is_dir() else 8
     struct.pack_into('<QqHB',row,0,at+1,at+1,length,kind);row[19:19+len(name)]=name;out.extend(row);at+=1
    f['dir_at']=at
    if out:uc.mem_write(a[1],bytes(out))
    self.ret(len(out))
  except FileNotFoundError:self.ret(-2)
  except FileExistsError:self.ret(-17)
 def run(self):
  self.uc.mem_write(0xffff0fc0,struct.pack('<8I',0xe5923000,0xe1530000,0x1a000002,0xe5821000,0xe3a00000,0xe12fff1e,0xe3a00001,0xe12fff1e))
  self.uc.emu_start(self.entry,0,timeout=55_000_000,count=8_000_000_000)
  if self.error or self.exit_code is None:raise RuntimeError((self.error,self.calls,self.output.decode(errors='replace')))
  return dict(exit=self.exit_code,stdout=self.output.decode(errors='replace'),modeled_syscalls=self.calls,hardware_tested=False)
