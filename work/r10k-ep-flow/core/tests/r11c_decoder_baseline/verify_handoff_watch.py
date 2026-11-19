from pathlib import Path
import json,struct,sys,zlib
R=Path(__file__).resolve().parents[1];W=R.parent;sys.path.insert(0,str(W/'tooling/python'))
from arm_model import ArmModel
from unicorn.arm_const import *
class ObserverModel(ArmModel):
 def __init__(self,binary):
  self.closed=[];self.fds={};self.maps=[];super().__init__(binary)
  # Linux kuser cmpxchg, needed by the real libc memory-stream mutex.
  self.uc.mem_write(0xffff0fc0,struct.pack('<5I',0xe5923000,0xe0533000,0x05821000,0xe2730000,0xe12fff1e))
 def string(self,p):
  b=bytearray()
  for _ in range(4096):
   c=bytes(self.uc.mem_read(p,1));p+=1
   if c==b'\0':return b.decode()
   b.extend(c)
  raise AssertionError('unterminated path')
 def interrupt(self,u,n,data):
  call=u.reg_read(UC_ARM_REG_R7);a=[u.reg_read(x) for x in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]]
  if call==369:assert a[0]==0 and a[1]==7 and not a[2];u.mem_write(a[3],struct.pack('<QQ',32,32));self.ret(0)
  elif call in [76,191]:u.mem_write(a[1],struct.pack('<II',32,32));self.ret(0)
  elif call==6:self.closed.append(a[0]);self.fds.pop(a[0],None);self.ret(0)
  elif call==221:assert a[0]==40 and a[1]==2 and a[2]==1;self.ret(0)
  elif call in [5,322]:
   p=self.string(a[0] if call==5 else a[1]);flags=a[1] if call==5 else a[2];assert flags&3==0,(p,flags)
   if p=='/proc/123/stat':body=('123 (camera app) S '+' '.join(str(i) for i in range(4,23))+'\n').encode()
   elif p=='/proc/123/maps':body=('10000000-10001000 r-xp 00000000 00:00 1 /usr/lib/fixture.so\n'*90+'76000000-76050000 r-xp 00000000 00:00 1 /usr/lib/libudd5.so\n').encode();assert body.find(b'libudd5.so')>4096
   else:self.ret(-2);return
   self.fds[40]=body;self.ret(40)
  elif call==3:
   # /proc seq_file reads may return a short page before EOF. Force short
   # reads even for stat to verify that the production reader drains it.
   b=self.fds[a[0]][:min(a[2],7)];self.fds[a[0]]=self.fds[a[0]][len(b):];u.mem_write(a[1],b);self.ret(len(b))
  elif call==192 and a[4]==99:
   assert a[1]==4096 and a[2]==1 and a[3]==1 and a[5] in [0x8153cbdc//4096,0x80592640//4096],a
   address=0x60000000+len(self.maps)*0x2000;u.mem_map(address,4096)
   if a[5]==0x8153cbdc//4096:u.mem_write(address+0xbdc,struct.pack('<II',165,166))
   else:u.mem_write(address+0x640,struct.pack('<I',0x801daa38))
   u.mem_protect(address,4096,1);self.maps.append(a);self.ret(address)
  elif call==54 and a[0] in [1,2] and a[1] in [0x5401,0x5413]:self.ret(-25)
  elif call in [26,37,54]:raise AssertionError(('unexpected camera mutation syscall',call,a))
  else:super().interrupt(u,n,data)
m=ObserverModel(R/'core/bin/test-handoff-watch.arm');result=m.run();assert result['exit']==0,result
assert all(x in m.closed for x in range(3,32)) and not any(x in m.closed for x in [0,1,2])
lines=result['stdout'].splitlines();frame=bytes.fromhex(json.loads(lines[0])['frame_hex']);header=struct.unpack('<10I',frame[:40])
assert header[9]==zlib.crc32(frame[:36]) and header[8]==zlib.crc32(frame[40:])
assert json.loads(lines[1])=='quote" slash\\ newline\n tab\t'
ram=json.loads(lines[2]);assert ram['atomic'] is False and all(row[1] is None for row in ram['words'])
schema=json.loads(lines[3]);assert schema['format']=='NXINITBROAD1' and len(schema['hot_addresses'])==64 and len(schema['context_pages'])==28
assert schema['linux_blocks']==[[0x55560,36],[0x55d90,16],[0x57080,8],[0x57720,16]]
assert schema['linux_app_addresses']==[0x44b480,0x4aa52c,0x4aa2f0,0x4aa258]
assert schema['repeat_key_addresses']==[0x8153f3f8,0x81540f38,0x8142de78,0x867555c4,0x86755238,0x867554c4]
assert lines[4].startswith('PASS_OBSERVER_')
out=dict(format='NX_R7H_OBSERVER_ARM_SAFETY1',passed=True,hardware_tested=False,actual_observer_C_executed=True,
 inherited_descriptors_closed=list(range(3,32)),workflow_token_removed=True,physical_mappings_read_only=m.maps,
 denied_addresses=['ISP MMIO','ROM','outside DDR','unaligned'],unavailable_values_remain_null=True,
 proc_stat_with_spaces_parsed=True,short_proc_reads_drained_to_EOF=True,library_beyond_first_proc_page_discovered=True,frame_crc32_independently_verified=True,JSON_escaping_verified=True,actual_schema_JSON_verified=True,
 limits=['Camera filesystem, scheduler, dlog utility and actual sampling throughput were not exercised.'])
(R/'validation/r7h/observer-arm.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(dict(passed=True,closed_inherited_fds=29,readonly_physical_maps=len(m.maps),short_reads_drained=True)))
