"""Execute the real relocatable block and original NX500 P7 consumers in ARM.
Only OS copy/mutex and cache/message transport boundaries are modeled.
"""
from pathlib import Path
import json,struct,sys,hashlib,collections
W=Path(__file__).resolve().parent;R=W/'r10j-frame-flow';V=R/'validation/current';sys.path.insert(0,str(W/'tooling/python'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE,UC_HOOK_MEM_INVALID
from unicorn.arm_const import *
P=(W/'firmware/P7.bin').read_bytes();B=(R/'core/bin/rtos_video_fullwidth.bin').read_bytes()
MAGIC=0x3146574e;FRONT=0x82020000;VECTOR=0x82030000;FULL=0x82040000;CROP=0x82050000;PACKET=0x82060000;STOP=0x824ff000;STACK=0x824e0000
OFF=[0x14,0x18,0x1c,0x20,0x3c,0x40];FULL_VALUES=[6608,3680,6480,3644,92,36];WIDE_VALUES=[6608,3680,6496,3644,92,36];CROP_VALUES=[4096,2436,3856,2176,232,260]
def fnv(b):
 h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 return h
class Replay:
 def __init__(self,base=0x96000000):
  self.base=base;self.nonce=0x64319ad7;self.u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u=self.u;u.mem_map(0x80000000,(len(P)+4095)&~4095);u.mem_write(0x80000000,P);u.mem_map(0x81000000,0x1500000);u.mem_write(0x81006000,P[0x79bdc0:0x79bdc0+0x813ee4c0-0x81006000]);u.mem_map(base,0x200000);self.cache=[];self.writes=[];self.calls=collections.Counter();self.recipe_records=[];self.auto='go';self.cache_fail=0;self.ready=False;self.raw=False;self.heap=0x82120000;self.inject_at=0;self.config_seen=0;self.pending_corrupt=None
  self.put(0x8146d3e4,[1,0x80586aa8]);self.put(0x81541808,[FRONT]);self.put(FRONT,[0x80587498]);self.put(FRONT+0x26c,[0x80592d08,VECTOR,22]);self.put(VECTOR+6*4,[FULL]);self.put(VECTOR+19*4,[CROP]);self.put(0x8146e430,[0x80587f08]);self.put(0x8146e438,[13|(13<<8)])
  u.hook_add(UC_HOOK_CODE,self.code,begin=0x80000000,end=0x80530000);u.hook_add(UC_HOOK_MEM_WRITE,self.write);u.hook_add(UC_HOOK_MEM_INVALID,self.invalid)
  self.run(0x8014cd30);self.run(0x8021c7a8,FULL,1);self.run(0x8021ca48,CROP,1)
  self.run(0x8016cd54,0x820b0000)
  # Run all three original state constructors and manager pointer binding.
  # Stop immediately before OnEnter reaches the hardware clock query.
  self.u.reg_write(UC_ARM_REG_R0,0x820b0000);self.u.reg_write(UC_ARM_REG_SP,STACK);self.u.reg_write(UC_ARM_REG_LR,STOP);self.u.emu_start(0x8016d830,0x8016d894,count=200000);assert self.u.reg_read(UC_ARM_REG_PC)==0x8016d894
  self.put(0x8146e730,[0x820b0000])
  self.power_manager=0x820b0000;self.power_video=self.words(self.power_manager+12,1)[0];self.power_ops=self.words(self.power_video+24,1)[0];self.power_entries=self.words(self.power_ops+8,1)[0];self.power_word=self.power_entries+16
  assert self.words(self.power_word,1)==[9]
  self.put(0x8153cbec,[0x80592110]);self.put(0x8153d958,[0x82300000]);self.put(0x82300000,[0x80592328])
  # Match the real photo-preview state which R9n falsely refused.
  self.put(self.power_manager+20,[self.power_video])
 def put(self,at,v):self.u.mem_write(at,struct.pack('<%dI'%len(v),*v))
 def words(self,at,n):return list(struct.unpack('<%dI'%n,self.u.mem_read(at,n*4)))
 def invalid(self,u,access,at,n,val,data):raise AssertionError(('unmapped',hex(at),n,hex(u.reg_read(UC_ARM_REG_PC))))
 def write(self,u,access,at,n,val,data):
  if self.raw:
   self.writes.append((at,n,val))
   if any(a<=at<a+n for a,n in [(2148584844, 164), (2152602968, 608), (2152603708, 236), (2152603576, 8), (2152306616, 8), (2152306632, 8), (2152306648, 8), (2147707624, 8), (2172500936, 280)]) or at in [0x80592294,0x80592298,0x805922bc,0x801cd9bc,0x8010daa4,0x801cf0e4,0x801cf0fc,self.power_word,0x8014e7ec,*[CROP+x for x in OFF]]:
    self.config_seen+=1
    if self.inject_at==self.config_seen:
     pc=u.reg_read(UC_ARM_REG_PC);self.inject_at=0
     holder=[]
     def corrupt_after(u,pc,n,d):
      self.put(at,[val^1]);u.hook_del(holder[0])
     holder.append(u.hook_add(UC_HOOK_CODE,corrupt_after,begin=pc+4,end=pc+4))
   if at==self.base+0x4100+252 and val and self.words(self.base+0x4100,64)[4]==9 and self.words(self.base+0x4100,64)[33]==1:
    self.ready=True
    if self.auto=='go':self.put(self.base+0x4200,[self.nonce^0x91f47ca3])
    elif self.auto=='abort':self.put(self.base+0x4204,[(~self.nonce)&0xffffffff])
 def ret(self,value=0):self.u.reg_write(UC_ARM_REG_R0,value&0xffffffff);self.u.reg_write(UC_ARM_REG_PC,self.u.reg_read(UC_ARM_REG_LR))
 def code(self,u,at,n,data):
  r=[u.reg_read(x)for x in[UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]]
  if self.pending_corrupt:
   changed_at,value=self.pending_corrupt;self.put(changed_at,[value]);self.pending_corrupt=None
  if at in[0x805249bc,0x80524560]:
   assert 0<r[0]<4096;p=self.heap;self.heap+=(r[0]+63)&~63;u.mem_write(p,b'\xcc'*r[0]);self.ret(p);return
  if at in [0x8009c51c,0x8009c818]:self.ret(0);return
  if at==0x8050c0d4:
   b=bytearray()
   while True:
    ch=bytes(u.mem_read(r[1]+len(b),1));b+=ch
    if ch==b'\0':break
   u.mem_write(r[0],bytes(b));self.ret(r[0]);return
  if at==0x804dc95c:self.put(r[3],[0x1001]);self.ret();return
  if at in[0x800465bc,0x80046484,0x800a3320,0x804dcbb8,0x804dcdd0,0x80045fb8]:self.ret();return
  if at in[0x80509eec,0x8050c424]:assert r[2]<1000000;u.mem_write(r[0],bytes(u.mem_read(r[1],r[2])));self.ret(r[0]);self.calls[hex(at)]+=1
  elif at==0x8050a114:assert r[2]<1000000;u.mem_write(r[0],bytes([r[1]&255])*r[2]);self.ret(r[0]);self.calls[hex(at)]+=1
  elif at==0x80096970:self.put(r[0],[0]);self.ret();self.calls[hex(at)]+=1
  elif at==0x80096a10:self.put(r[0],self.words(r[1],1));self.ret();self.calls[hex(at)]+=1
  elif at in[0x800969a0,0x800969e4]:self.ret();self.calls[hex(at)]+=1
  elif at==0x8008fd40:
   assert r[0]==0x81dfc800 and 0x80000000<=r[1]<0x94000000 and r[2]in[64,192,256,320,640,704,4096];self.cache.append([hex(r[1]),r[2]]);self.ret(-1 if self.cache_fail==len(self.cache)else 0)
  elif at==0x8004d2f8:self.calls['original_message_transport']+=1;self.ret()
  elif at in[0x800f6474,0x800f0828,0x800f1868,0x800f0b40]:self.ret();self.calls[hex(at)]+=1
  elif at==0x801c27e4:
   p=r[2];rec=self.words(p,4);im=self.words(p+16,44);self.recipe_records.append(dict(id=hex(rec[0]),input=hex(rec[3]),global_size=im[1:3],active_size=im[3:5],image_words=im));self.ret(0x82080000+len(self.recipe_records)*0x400)
  elif at in[0x801c4eec,0x80175120]:self.ret();self.calls[hex(at)]+=1
  if self.raw and not self.ready and self.words(self.base+0x4100,64)[63]:
   v=self.words(self.base+0x4100,64)
   if v[4]==9 and v[33]==1:
    self.ready=True
    if self.auto=='go':self.put(self.base+0x4200,[self.nonce^0x91f47ca3])
    elif self.auto=='abort':self.put(self.base+0x3204,[(~self.nonce)&0xffffffff])
 def run(self,at,r0=0,r1=0,r2=0,r3=0,count=2000000):
  u=self.u
  for reg,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],[r0,r1,r2,r3]):u.reg_write(reg,v)
  u.reg_write(UC_ARM_REG_SP,STACK);u.reg_write(UC_ARM_REG_LR,STOP);
  try:u.emu_start(at,STOP,count=count)
  except Exception:
   print("Native replay exception PC",hex(u.reg_read(UC_ARM_REG_PC)),"LR",hex(u.reg_read(UC_ARM_REG_LR)),"call",hex(at));raise
  assert u.reg_read(UC_ARM_REG_PC)==STOP,hex(u.reg_read(UC_ARM_REG_PC));return u.reg_read(UC_ARM_REG_R0)
 def transaction(self,operation=1,cookie=True):
  self.ready=False;self.cache=[];self.writes=[];self.config_seen=0;self.u.mem_write(self.base,B);self.u.mem_write(self.base+0x4100,b'\0'*0x200);self.put(self.base+0x4000,[MAGIC,4,self.nonce,self.base,0x200000,operation,FRONT,FULL,CROP,self.power_manager,self.power_video,self.power_ops,self.power_entries,self.power_word]+[0]*2);self.put(PACKET,[0,12,0,0,self.base+0x4800]);self.put(self.base+0x4800,[self.nonce if cookie else self.nonce^1,self.base+0x4000,operation]);self.put(0x8100f7b0,[self.base,0]);self.raw=True;ret=self.run(self.base,PACKET);self.raw=False;reply=self.words(self.base+0x4100,64)
  if cookie:assert reply[:3]==[MAGIC,4,self.nonce]and reply[62]==fnv(bytes(self.u.mem_read(self.base+0x4100,248)))and reply[63]==(0xc74f918b^reply[62]^self.nonce)
  self.nonce=(self.nonce+0x1001)&0xffffffff;return ret,reply
 def legacy_blocks(self):
  for a,n in [(2148584844, 164), (2152602968, 608), (2152603708, 236), (2152603576, 8), (2152306616, 8), (2152306632, 8), (2152306648, 8), (2147707624, 8)]:self.u.mem_write(a,P[a-0x80000000:a-0x80000000+n])
  self.u.mem_write(0x817dbbc8,bytes(280))
 def values(self):return[self.words(CROP+o,1)[0]for o in OFF]
 def proof(self):
  self.run(0x801751dc,0x82070000);self.run(0x80502d50,CROP,0x82070000);assert self.words(0x8207000c,2)==[6496,3644]
  self.recipe_records=[];self.put(0x82090000+0x54,[0,2,3]);self.run(0x8010cf34,0x82090000,0x820a0000);self.run(0x8010cd8c,0x82090000,0x820a0000,0x82080400)
  pp,ipc=self.recipe_records;assert pp['active_size']==[6496,3644]and ipc['active_size']==[3840,2160]
  return self.recipe_records
 def scaler(self):
  if not hasattr(self,'scaler_mapped'):self.u.mem_map(0x20800000,0x40000);self.scaler_mapped=True
  G=0x820c0000;PARAM=0x820d0000;OUT=0x820e0000;DEST=0x820e1000;OBJ=0x820f0000;PRESET=0x82100000
  self.put(0x81543d34,[G]);self.put(DEST,self.recipe_records[1]['image_words']);self.put(PARAM,[0x01000023,0,CROP+16,DEST,DEST,0]);self.u.mem_write(G,b'\0'*64);self.u.mem_write(OUT,b'\0'*32)
  fn=self.words(0x8059237c if self.words(0x801cd9bc,1)[0]==0x801cda10 else 0x805922bc,1)[0];self.run(fn,OBJ,PARAM,OUT);key=self.words(OUT+4,1)[0];self.u.mem_write(PRESET,b'\0'*88);assert self.run(0x804a33a0,OUT,PRESET)==0
  return dict(native_function=hex(fn),preset=hex(key),input=self.words(CROP+28,2),output=self.words(DEST+12,2),decoded_control_hex=bytes(self.u.mem_read(PRESET,88)).hex())

rows=[]
meta=json.loads((V/'timing-build.json').read_text());blocks=meta['blocks']
for base in [0x96000000,0x98200000]:
 r=Replay(base);before_profile=bytes(r.u.mem_read(CROP,192));before_full=bytes(r.u.mem_read(FULL,192));clock_before=bytes(r.u.mem_read(0x8100e400,26*40))
 ret,reply=r.transaction(3);assert ret==0 and reply[4]==0 and reply[5:7]==[1,1] and reply[12]==25 and reply[56]==2
 allowed=[(x['address'],x['bytes'])for x in blocks[1:]]+[(0x817dbbc8,280),(0x8100f7b0,4)]
 bad=[(hex(a),n,v)for a,n,v in r.writes if not(base<=a<base+0x200000 or STACK-10000<=a<STACK or any(lo<=a and a+n<=lo+length for lo,length in allowed))]
 assert not bad,bad[:5]
 assert bytes(r.u.mem_read(CROP,192))==before_profile and bytes(r.u.mem_read(FULL,192))==before_full and bytes(r.u.mem_read(0x8100e400,26*40))==clock_before
 assert r.words(0x817dbbe0,2)==[0x33544c56,4]
 ret,idempotent=r.transaction(3);assert ret==0 and idempotent[7]==0
 ret,applied=r.transaction(1);assert ret==0 and applied[5:7]==[1,2] and r.values()==WIDE_VALUES
 proof=r.proof();scaler=r.scaler();assert scaler['preset']=='0xe005'
 ret,trace=r.transaction(3);assert ret==0 and trace[7]==0 and trace[5:7]==[2,2]
 ret,restored=r.transaction(2);assert ret==0 and r.values()==CROP_VALUES and r.scaler()['preset']=='0xe004'
 rows.append(dict(case='trace-only-no-geometry-clock-or-codec-store-and-repeated-full-native-switch',base=hex(base),trace=reply,apply=applied,restore=restored,proof=proof,scaler=scaler))
# A fresh full-width apply still installs the tracer atomically.
r=Replay();ret,rep=r.transaction(1);assert ret==0 and rep[7]==meta['first_apply_stores'];total=rep[7]
rows.append(dict(case='fresh-apply',receipt=rep))
# Failure injection covers every first-arm target store; publication failures
# retain known fixed bodies and restore all selected video configuration.
r=Replay();ret,rep=r.transaction(3);assert ret==0;trace_stores=rep[7]
for op,stores in [(3,trace_stores),(1,total)]:
 for store in range(1,stores+1):
  r=Replay();before=bytes(r.u.mem_read(CROP,192));r.inject_at=store;ret,rep=r.transaction(op)
  assert ret!=0 and rep[4]==6 and rep[8]==1 and bytes(r.u.mem_read(CROP,192))==before and r.words(0x8014e7ec,1)==[0x8014e8a0],(op,store,rep)
 rows.append(dict(case='each-target-store-readback-fault-restores-known-state',operation=op,stores=stores))
for op in [1,3]:
 for fail in range(2,20):
  r=Replay();r.cache_fail=fail;ret,rep=r.transaction(op)
  if ret==0:continue
  assert rep[4]==7 and rep[8]==1 and r.values()==CROP_VALUES,(op,fail,rep)
 rows.append(dict(case='cache-failures-retain-published-code-and-recover-configuration',operation=op))
for mode in [23,24,25]:
 r=Replay();r.put(0x8146e438,[mode|(mode<<8)]);ret,rep=r.transaction(3);assert ret!=0 and rep[4]==3 and not rep[7]
rows.append(dict(case='all-three-active-movie-modes-refuse-arming',passed=True))
r=Replay();r.put(0x817dbbcc,[0x82280000]);ret,rep=r.transaction(3);assert ret!=0 and rep[4]==12 and not rep[7]
rows.append(dict(case='owned-native-TMC-refused',passed=True))
r=Replay();r.auto='abort';ret,rep=r.transaction(3);assert ret!=0 and rep[4]==10 and not rep[7]
rows.append(dict(case='cancelled-arm-no-target-change',passed=True))
for at in [0x804e1d58,0x804e1fb8,0x80036ae8]:
 r=Replay();r.put(at,[r.words(at,1)[0]^1]);ret,rep=r.transaction(3);assert ret!=0 and not rep[7]
rows.append(dict(case='foreign-code-rejected-before-arm',passed=True))
report=dict(passed=True,cases=rows,trace_stores=trace_stores,first_apply_stores=total,RTOS_block_sha256=hashlib.sha256(B).hexdigest(),physical_camera_tested=False,scope='Actual ARM transaction, original consumers and failure recovery; modeled OS/cache boundaries, not hardware throughput.')
(V/'video-fullwidth-replay.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS',len(rows),'transaction groups,',trace_stores+total,'per-store failure cases, unchanged native geometry and blue-boundary consumer')

