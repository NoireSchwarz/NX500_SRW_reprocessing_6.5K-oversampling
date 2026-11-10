"""Execute the actual R10j ARM tracer, native high/low reads and entry ABI.
Peripheral time/latch behaviors are explicit models, not hardware claims.
"""
from pathlib import Path
import sys,struct,json,hashlib
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';sys.path[:0]=[str(W),str(W/'tooling/python')]
from fw_research import Firmware
from unicorn import *
from unicorn.arm_const import *
fw=Firmware();D=R/'core/build-timing';meta=json.loads((R/'validation/current/timing-build.json').read_text());sym=meta['symbols'];A=(D/'timing-a.bin').read_bytes();B=(D/'timing-b.bin').read_bytes()
STATE=0x817dbbe0;OWNER=STATE-4;M=0x82010000;STACK=0x82080000;STOP=0x820ff000
REGS=[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11,UC_ARM_REG_R12]
def branch(at,target):return 0xea000000|(((target-at-8)//4)&0xffffff)
class Model:
 def __init__(self,latch=False):
  self.latch=latch;self.tick=0;self.latched=0;self.reads=[];self.writes=[];self.instruction_count=0;self.critical_count=0;self.max_critical=0;self.critical_run=0;self.cache_ops=0;self.expected_cpsr=0x13
  self.u=u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x80000000,0x1000000);u.mem_write(0x80000000,fw.b);u.mem_map(0x81000000,0x1100000);u.mem_map(0x06002000,4096)
  for block in meta['blocks']:
   if block['name']=='boundary':continue
   at=block['address'];raw=fw.data(at,block['bytes'])
   if block['name']=='timing_a':raw=A
   elif block['name']=='timing_b':raw=B
   elif block['name']=='marker':raw=struct.pack('<I',branch(at,sym['ipc_marker']))+raw[4:]
   elif block['name']=='irq_entry':raw=struct.pack('<I',branch(at,sym['irq_entry']))+raw[4:]
   else:raw=struct.pack('<I',branch(at,sym['hw_commit'])&0x0fffffff)+raw[4:]
   u.mem_write(at,raw)
  u.mem_map(0x20810000,0x10000);self.mmio_starts=[];self.acks=[]
  self.put(0x8146e438,[0x1717]);self.put(0x813ebf60,[0xe005]);self.put(STATE,[0x33544c56,4]+[0]*62);self.put(0x817dbce0,[0x8244a000,1,0])

  u.hook_add(UC_HOOK_MEM_READ,self.read);u.hook_add(UC_HOOK_MEM_WRITE,self.write)
  u.hook_add(UC_HOOK_CODE,self.code,begin=0x804e1d58,end=0x804e22a3)
 def put(self,a,v):self.u.mem_write(a,struct.pack('<%dI'%len(v),*[x&0xffffffff for x in v]))
 def words(self,a,n):return list(struct.unpack('<%dI'%n,self.u.mem_read(a,4*n)))
 def read(self,u,k,a,n,v,d):
  if a>=0x80000000 or 0x20810000<=a<0x20820000:return
  assert a in (0x06002060,0x06002064)and n==4
  self.reads.append(a)
  if a==0x06002060:self.latched=self.tick;u.mem_write(a,struct.pack('<I',self.tick>>32))
  else:u.mem_write(a,struct.pack('<I',(self.latched if self.latch else self.tick)&0xffffffff))
 def write(self,u,k,a,n,v,d):
  if a==0x20810304:
   self.mmio_starts.append((self.tick,v));return
  if a==0x20810440:
   self.acks.append(v)
   if v:self.put(0x20810418,[self.words(0x20810418,1)[0]&~v])
   return
  if OWNER<=a and a+n<=STATE+256:
   assert u.reg_read(UC_ARM_REG_CPSR)&0xc0==0xc0,('non-atomic shared update',hex(a),hex(u.reg_read(UC_ARM_REG_PC)))
  else:assert STACK-256<=a and a+n<=STACK,('unexpected mutation',hex(a),hex(u.reg_read(UC_ARM_REG_PC)))
  self.writes.append((a,n,v))
 def code(self,u,at,n,d):
  self.instruction_count+=1;flags=u.reg_read(UC_ARM_REG_CPSR)
  if flags&0xc0==0xc0:self.critical_count+=1;self.critical_run+=1;self.max_critical=max(self.max_critical,self.critical_run)
  else:self.critical_run=0
  w=self.words(at,1)[0]
  if w&0xfff00fff==0xee000f3a:
   self.cache_ops+=1;assert flags&0xc0==self.expected_cpsr&0xc0,'cache clean inside new masked section'
 def setup(self,tick,args,lr,flags):
  self.tick=tick;u=self.u;u.reg_write(UC_ARM_REG_CPSR,flags);self.expected_cpsr=flags
  values=list(args)+[0x12340000+i for i in range(13-len(args))]
  for reg,v in zip(REGS,values):u.reg_write(reg,v)
  u.reg_write(UC_ARM_REG_SP,STACK);u.reg_write(UC_ARM_REG_LR,lr);return values
 def marker(self,tick,kind=4,caller=None,flags=0xa0000013,controller=1):
  caller=caller or(0x8017d884 if kind==4 else 0x801cdf78);values=self.setup(tick,[kind],caller,flags);u=self.u;values[4]=controller;u.reg_write(UC_ARM_REG_R4,controller);u.emu_start(0x804e1fb8,caller,count=3000)
  assert u.reg_read(UC_ARM_REG_PC)==caller and u.reg_read(UC_ARM_REG_SP)==STACK
  assert [u.reg_read(r)for r in REGS]==values and u.reg_read(UC_ARM_REG_CPSR)==flags
 def start(self,tick,**kw):self.marker(tick,4,**kw)
 def hardware(self,tick,controller=1,flags=0x60000013):
  original=self.words(0x20810304,1)[0];code=fw.data(0x804997a4,60);self.u.mem_write(0x820e0000,code)
  values=self.setup(tick,[controller],STOP,flags);u=self.u;u.emu_start(0x820e0000,STOP,count=100)
  expected=[u.reg_read(r)for r in REGS];psr=u.reg_read(UC_ARM_REG_CPSR);sp=u.reg_read(UC_ARM_REG_SP);wanted=self.words(0x20810304,1)[0];writes=len(self.mmio_starts)
  self.put(0x20810304,[original]);self.setup(tick,[controller],STOP,flags);u.emu_start(0x804997a4,STOP,count=2000)
  assert [u.reg_read(r)for r in REGS]==expected and u.reg_read(UC_ARM_REG_CPSR)==psr and u.reg_read(UC_ARM_REG_SP)==sp
  assert self.words(0x20810304,1)[0]==wanted and len(self.mmio_starts)-writes==(1 if controller<=2 else 0)
 def irq(self,tick,status=4,flags=0x60000092):
  self.put(0x20810418,[status]);values=self.setup(tick,[44],STOP,flags);u=self.u;u.emu_start(0x80036ae8,0x80036aec,count=2000)
  got=[u.reg_read(r)for r in REGS];sp=u.reg_read(UC_ARM_REG_SP);psr=u.reg_read(UC_ARM_REG_CPSR);stack=bytes(u.mem_read(sp,20))
  self.put(0x820e0100,[0xe92d40f0]);self.setup(tick,[44],STOP,flags);u.emu_start(0x820e0100,0x820e0104,count=1)
  assert got==[u.reg_read(r)for r in REGS]and sp==u.reg_read(UC_ARM_REG_SP)and psr==u.reg_read(UC_ARM_REG_CPSR)and stack==bytes(u.mem_read(sp,20))
 def records(self):
  assert self.words(0x817dbce0,3)==[0x8244a000,1,0];assert bytes(self.u.mem_read(0x804e2128,0x17c))==fw.data(0x804e2128,0x17c);a=self.words(STATE,64);n=a[2];records=[]
  for seq in range(max(1,n-7),n+1):
   row=a[8+(seq&7)*7:15+(seq&7)*7];assert row[0]==seq,(seq,row);records.append(row)
  return a,records

rows=[]
for mode,preset in [(m,p) for m in [0x1717,0x1818,0x1919] for p in [0xe004,0xe005]]:
 for latch in [False,True]:
  m=Model(latch);m.put(0x8146e438,[mode]);m.put(0x813ebf60,[preset]);m.start(1000000);m.hardware(1026000);m.irq(1030000,8);m.irq(1040000,0x40);m.irq(2100000,4);m.marker(2102600,5)
  a,r=m.records();assert r[0][1:6]==[1026000,1030000,1040000,2100000,2102600] and r[0][6]&15==15 and (r[0][6]>>26)&3==1 and (r[0][6]>>28)&3==1 and not a[3]
  assert not m.acks and m.reads==[0x06002060,0x06002064]*5
  rows.append(dict(case='DCI-UHD-fullwidth-same-five-boundaries',mode=hex(mode),latch=latch,record=r[0],max_atomic_instructions=m.max_critical))
for latch in [False,True]:
 m=Model(latch);t=0xffff0000
 for i in range(1200):
  m.start(t);m.hardware(t+2600);m.irq(t+3000,8);m.irq(t+4000,0x40);m.irq(t+780000);m.marker(t+782600,5);t+=867534
 a,r=m.records();assert a[2]==1200 and len(r)==8 and all((x[4]-x[1])&0xffffffff==777400 for x in r)
 rows.append(dict(case='1200-native-clock-wrap-frames',latch=latch,completed=a[2],retained=r,max_atomic_instructions=m.max_critical))
for mode in [0x1616,0x1718,0x1817,0x1918,0x1a1a]:
 m=Model();m.put(0x8146e438,[mode]);m.start(100);m.hardware(200);m.irq(500);m.marker(600,5);a,r=m.records();assert not r and not a[3]
rows.append(dict(case='only-three-verified-mode-pairs-qualified',passed=True))
for controller in [0,1,2]:
 for flags in [0x13,0x92,0xa000001f]:
  m=Model();m.start(100,controller=controller);m.hardware(200,controller,flags);m.irq(500);m.marker(600,5);a,r=m.records();assert r[0][1]==200 and (r[0][6]>>24)&3==controller
rows.append(dict(case='original-all-three-conditional-STR-and-register-CPSR-stack-ABI',passed=True))
m=Model();m.start(100);m.hardware(200);m.irq(500,0x4c);m.marker(600,5);a,r=m.records();assert r[0][2:5]==[500]*3 and r[0][6]&128
rows.append(dict(case='coalesced-event-times-are-one-observation-not-event-order',record=r[0]))
m=Model();m.start(100);m.hardware(200)
for i in range(10):m.irq(300+i,0x48)
m.irq(500);m.marker(600,5);a,r=m.records();assert r[0][2:4]==[300,300] and (r[0][6]>>26)&3==3 and (r[0][6]>>28)&3==3
rows.append(dict(case='event-counts-saturate-explicit-at-three-or-more',record=r[0]))
m=Model();m.start(100);m.hardware(200);m.irq(500);m.irq(550);m.marker(600,5);m.marker(700,5);a,r=m.records();assert len(r)==1 and r[0][4]==500 and r[0][5]==600 and (r[0][6]>>8)&255==2
rows.append(dict(case='first-final-observation-and-single-publish-on-callback',record=r[0]))
m=Model();m.start(100);m.hardware(200);m.start(400);a,r=m.records();assert len(r)==1 and r[0][6]&16 and not r[0][6]&8
rows.append(dict(case='cancelled-request-explicit-not-silent-loss',record=r[0]))
m=Model();m.start(100,controller=1);m.hardware(200,0);m.hardware(300,1);m.irq(500);m.marker(600,5);a,r=m.records();assert r[0][1]==300
rows.append(dict(case='other-controller-not-claimed',record=r[0]))
m=Model();m.put(STATE+8,[0xffffffff]);m.start(100);m.hardware(200);m.irq(500);m.marker(600,5);a,r=m.records();assert a[2]==1
rows.append(dict(case='sequence-wrap-skips-zero',record=r[0]))
m=Model();m.start(1000000);m.hardware(1026000);m.put(0x813f09bc,[0x801cdf6c]);m.put(0x20810410,[0,0,4]);m.put(0x20810418,[4]);m.setup(2100000,[44],STOP,0x92)
def callback_clock(u,a,n,d):
 if a==0x801cdf6c:m.tick=2102600
m.u.hook_add(UC_HOOK_CODE,callback_clock,begin=0x801cdf6c,end=0x801cdf6c)
m.u.emu_start(0x80036ae8,0x801cdf78,count=10000);a,r=m.records();assert r[0][1:6]==[1026000,0,0,2100000,2102600] and m.acks==[4,0]
rows.append(dict(case='actual-original-IRQ-dispatch-ACK-and-callback-path',record=r[0],native_ACK=m.acks))
for at in [0x804e1d58,0x804e1f44,0x804e203c,0x804e2050,0x804e2098]:
 m.setup(0,[0],STOP,0x13);m.u.emu_start(at,STOP,count=10);assert m.u.reg_read(UC_ARM_REG_PC)==STOP
out=dict(passed=True,cases=rows,code_hashes=[hashlib.sha256(A).hexdigest(),hashlib.sha256(B).hexdigest()],ring_bytes=256,capacity=8,native_string_logger_preserved=True,hardware_tested=False)
(R/'validation/current/timing-ARM.json').write_text(json.dumps(out,indent=2)+'\n')
print('PASS',len(rows),'ARM cases; 2400 modeled frames; three-mode timing; original trigger and dispatcher preserved')
