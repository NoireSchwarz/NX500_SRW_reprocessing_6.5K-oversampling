"""Execute shipped LUT utilities, modeling Linux files and physical RAM only.
The real ARM code performs all guards, comparisons, writes and rollback.
No ISP, DMA, cache coherence or physical registers are emulated.
"""
from pathlib import Path
import hashlib,json,os,struct,sys
REPO=Path(os.environ.get('NXLOG_REPO',r'D:\NX500模组开发\repository\nx500'))
sys.path.insert(0,str(REPO/'work/tooling/python'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_INTR,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
REG=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]

def host_path(s):
 if len(s)>3 and s[0]=='/' and s[2]=='/' and s[1].isalpha():s=s[1]+':'+s[2:]
 return Path(s)

class Model:
 def __init__(self,binary,args,state,root):
  self.state=Path(state).resolve();self.root=Path(root).resolve();self.binary=Path(binary)
  self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM);self.fds={};self.nextfd=10;self.maps={};self.writes=[];self.out=bytearray();self.exit=None
  with self.binary.open('rb') as f:
   elf=ELFFile(f);segs=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD'];lo=min(s['p_vaddr'] for s in segs)&~4095;hi=max(s['p_vaddr']+s['p_memsz'] for s in segs);self.u.mem_map(lo,(hi-lo+4095)&~4095)
   for seg in segs:self.u.mem_write(seg['p_vaddr'],seg.data())
   self.entry=elf.header['e_entry']
  self.u.mem_map(0x70000000,0x200000);ptr=0x70100000;pointers=[]
  for s in [str(binary),*args]:
   b=s.encode()+b'\0';self.u.mem_write(ptr,b);pointers.append(ptr);ptr+=len(b)
  stack=0x700fe000;vals=[len(pointers),*pointers,0,0,0,0];self.u.mem_write(stack,struct.pack('<'+'I'*len(vals),*vals));self.u.reg_write(UC_ARM_REG_SP,stack)
  self.u.hook_add(UC_HOOK_INTR,self.syscall);self.u.hook_add(UC_HOOK_MEM_WRITE,self.write)
 def string(self,ptr):
  out=bytearray()
  for i in range(4096):
   b=self.u.mem_read(ptr+i,1)
   if b==b'\0':return out.decode()
   out.extend(b)
  raise AssertionError('unterminated string')
 def path(self,s):
  if s.startswith('/tmp/nx-lut-lite/'):p=self.root/'run'/s.removeprefix('/tmp/nx-lut-lite/')
  else:p=host_path(s).resolve()
  assert p==self.root or self.root in p.parents,(s,p,self.root)
  return p
 def write(self,u,access,at,n,value,_):
  for base,(physical,span,writable,file) in self.maps.items():
   if base<=at and at+n<=base+span:
    assert writable and physical!=0x2082b000
    self.writes.append((physical+at-base,n))
 def ret(self,v):self.u.reg_write(UC_ARM_REG_R0,v&0xffffffff)
 def syscall(self,u,num,_):
  n=u.reg_read(UC_ARM_REG_R7);a=[u.reg_read(r)for r in REG]
  if n in [1,248]:self.exit=a[0];u.emu_stop();return
  if n==5:
   name=self.string(a[0]);flags=a[1]
   if name=='/etc/version.info':value=['bytes',b'1.12\nNX500\n',0]
   elif name=='/dev/mem':value=['mem',None,0]
   else:
    p=self.path(name)
    if flags&3:
     p.parent.mkdir(parents=True,exist_ok=True);f=p.open('wb' if flags&512 else 'ab')
    else:
     if not p.is_file():self.ret(-2);return
     f=p.open('rb')
    value=['file',f,0]
   fd=self.nextfd;self.nextfd+=1;self.fds[fd]=value;self.ret(fd);return
  if n==6:
   kind,obj,at=self.fds.pop(a[0]);
   if kind=='file':obj.close()
   self.ret(0);return
  if n==3:
   kind,obj,at=self.fds[a[0]]
   if kind=='bytes':b=obj[at:at+a[2]];self.fds[a[0]][2]+=len(b)
   else:assert kind=='file';b=obj.read(a[2])
   if b:u.mem_write(a[1],b)
   self.ret(len(b));return
  if n==4:
   b=bytes(u.mem_read(a[1],a[2]))
   if a[0] in [1,2]:self.out.extend(b)
   else:kind,obj,at=self.fds[a[0]];assert kind=='file';obj.write(b);obj.flush()
   self.ret(len(b));return
  if n==192:
   physical=a[5]<<12;size=a[1];writable=bool(a[2]&2)
   if physical==0x80380000:assert size==0x4000;name='table.bin'
   elif physical==0x81119000:assert size==0x6000;name='slot.bin'
   elif physical==0x2082b000:assert size==0x1000 and not writable;name=None
   else:raise AssertionError(('Unexpected mmap',hex(physical),size))
   base=0x60000000+len(self.maps)*0x100000;u.mem_map(base,size)
   b=(self.state/name).read_bytes() if name else bytes(size);assert len(b)==size;u.mem_write(base,b)
   self.maps[base]=(physical,size,writable,name);self.ret(base);return
  if n==91:
   physical,size,writable,name=self.maps.pop(a[0]);assert size==a[1]
   if writable:(self.state/name).write_bytes(bytes(u.mem_read(a[0],size)))
   u.mem_unmap(a[0],size);self.ret(0);return
  raise AssertionError(('unmodeled syscall',n,a))
 def run(self):
  self.u.emu_start(self.entry,0,count=12000000)
  assert self.exit is not None
  for kind,obj,at in self.fds.values():
   if kind=='file':obj.close()
  return {'exit':self.exit,'stdout':self.out.decode('utf8','replace'),'physical_writes':self.writes,'binary_sha256':hashlib.sha256(self.binary.read_bytes()).hexdigest()}

def cli():
 binary=Path(os.environ['NXLOG_BINARY_DIR'])/(sys.argv[1]+'.arm')
 m=Model(binary,sys.argv[2:],os.environ['NXLOG_MODEL_STATE'],os.environ['NXLOG_MODEL_ROOT']);r=m.run()
 with (m.state/'calls.jsonl').open('a',encoding='utf8') as f:f.write(json.dumps({'tool':sys.argv[1],'args':sys.argv[2:],**r})+'\n')
 sys.stdout.write(r['stdout']);sys.exit(r['exit'])
if __name__=='__main__':cli()
