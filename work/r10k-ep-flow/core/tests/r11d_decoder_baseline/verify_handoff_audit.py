"""Run production R7g ARM audit instructions against the OEM ref_sem stub.

Only the read-only RTOS reference syscall is modeled. A write outside the
owned audit block or caller stack fails the run. No camera access.
"""
from pathlib import Path
import hashlib,json,struct,sys
R=Path(__file__).resolve().parents[1];W=R.parent
sys.path.insert(0,str(W/'tooling/python'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_INTR,UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
CAP=0xa1c00000;STACK=0x83000000;END=STACK+0x10000;TASK=0x8153e6d4
A=0x8153cbdc;B=0x8153cbe0;OFFSET=0xbe00;NONCE=0x701d1a36
firmware=(W/'firmware/P7.bin').read_bytes();blob=(R/'core/bin/rtos_front.bin').read_bytes()
with (R/'core/build/rtos_front.elf').open('rb') as f:
 symbols={s.name:s['st_value'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
service=struct.unpack_from('<I',firmware,0x804d9ec0-0x80000000)[0]
def words(u,p,n):return list(struct.unpack('<%dI'%n,u.mem_read(p,n*4)))
def put(u,p,*w):u.mem_write(p,struct.pack('<%dI'%len(w),*w))
def checksum(w):
 h=2166136261
 for b in struct.pack('<17I',*w[1:18]):h=((h^b)*16777619)&0xffffffff
 return h
def valid(w,stage):
 return w[:4]==[0x31414c53,1,NONCE,stage] and w[18]==checksum(w) and w[19]==(NONCE^stage^w[18]^0x31414c53)
cases=[]
for label,ids,counts,error,overrun,race in [
 ('free',[165,166],[1,1],0,False,False),
 ('held',[165,166],[0,0],0,False,False),
 ('mixed',[165,166],[1,0],0,False,False),
 ('overreleased-is-recorded-not-repaired',[165,166],[2,2],0,False,False),
 ('reference-error',[165,166],[1,1],-41,False,False),
 ('invalid-zero-holder',[0,166],[1,1],0,False,False),
 ('invalid-large-holder',[65536,166],[1,1],0,False,False),
 ('native-output-overrun',[165,166],[1,1],0,True,False),
 ('holder-race',[165,166],[1,1],0,False,True)]:
 u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x80000000,(len(firmware)+4095)&~4095);u.mem_write(0x80000000,firmware)
 u.mem_map(0x81000000,0x1000000);u.mem_map(CAP,0x10000);u.mem_write(CAP,blob);u.mem_map(STACK,0x11000)
 put(u,CAP+0x3000,NONCE,CAP);put(u,A,ids[0],ids[1]);put(u,TASK+3364,7);put(u,0x81540f38,0x04000001)
 called=[];writes=[];errors=[]
 def code(u,pc,size,data):
  if pc==END:u.emu_stop()
  if pc in [0x804dccc0,0x804dcbb8,0x804dcdd0,0x801da0ac,0x801da044]:errors.append(('mutating-or-blocking-native-call',hex(pc)));u.emu_stop()
 def write(u,access,address,size,value,data):
  if not (CAP+OFFSET<=address and address+size<=CAP+OFFSET+480 or STACK<=address and address+size<=END):errors.append(('foreign-write',hex(address)));u.emu_stop()
  writes.append(address)
 def interrupt(u,n,data):
  sid=u.reg_read(UC_ARM_REG_R12);sem=u.reg_read(UC_ARM_REG_R0);out=u.reg_read(UC_ARM_REG_R1)
  assert n==2 and sid==service and STACK<=out<END-16,(n,hex(sid),hex(out))
  assert sem in [165,166],sem;index=[165,166].index(sem);called.append(sem)
  if not error:put(u,out,0x700+index,0,counts[index])
  if overrun:put(u,out+12,0)
  if race:put(u,A+index*4,167+index)
  u.reg_write(UC_ARM_REG_R0,error&0xffffffff)
 u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_INTR,interrupt);u.hook_add(UC_HOOK_MEM_WRITE,write)
 before=bytes(u.mem_read(0x81000000,0x1000000))
 for stage in range(1,7):
  if race:put(u,A,*ids)
  u.reg_write(UC_ARM_REG_R0,stage);u.reg_write(UC_ARM_REG_SP,END-16);u.reg_write(UC_ARM_REG_LR,END)
  u.emu_start(CAP+symbols['still_audit'],0,count=100000)
  assert u.reg_read(UC_ARM_REG_PC)==END and u.reg_read(UC_ARM_REG_SP)==END-16
  assert not errors,(label,errors)
  w=words(u,CAP+OFFSET+(stage-1)*80,20);assert valid(w,stage),(label,w)
  for index,k in enumerate([6,11]):
   expected=-75 if overrun else error if 0<ids[index]<=65535 else -22
   assert w[k]==ids[index] and w[k+1]==expected&0xffffffff,(label,index,w)
   if not error and 0<ids[index]<=65535:assert w[k+4]==counts[index]
   assert w[16+index]==(167+index if race else ids[index])
   for field in range(20):
    changed=w.copy();changed[field]^=1;assert not valid(changed,stage),(label,field)
 after=bytes(u.mem_read(0x81000000,0x1000000))
 if not race:assert before==after,label
 expected_calls=6*sum(0<x<=65535 for x in ids);assert len(called)==expected_calls,(label,called)
 if label=='free':(R/'validation/r7g/actual-still-audit-model.bin').write_bytes(bytes(u.mem_read(CAP+OFFSET,480)))
 cases.append(dict(case=label,stages=6,reference_syscalls=len(called),task_or_holder_writes_by_audit=0,transport_mutations_rejected=120))
# Production IRQ code cannot enter the diagnostic service.
s=(R/'core/src/rtos_front.c').read_text();irq=s[s.index('void nxl_irq_body'):s.index('int32_t nxl_entry')]
assert 'still_audit(' not in irq
result=dict(format='NX_R7G_STILL_AUDIT_ARM_TEST1',passed=True,hardware_tested=False,cases=cases,
 actual_production_ARM_executed=True,original_ref_sem_stub_executed=True,reference_service=hex(service),
 no_get_put_init_finalize_calls=True,no_ISR_queries=True,firmware_sha256=hashlib.sha256(firmware).hexdigest(),
 binary_sha256=hashlib.sha256(blob).hexdigest(),limits=['RTOS ref_sem service data was modeled; actual camera counts remain unobserved.'])
out=R/'validation/r7g/arm-audit.json';out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(passed=True,cases=len(cases),stages=sum(x['stages'] for x in cases))))
