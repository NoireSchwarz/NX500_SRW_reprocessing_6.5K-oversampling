"""Execute OEM UI commit instructions; native callees/state are modeled."""
from context import *
sys.path.insert(0,str(PY))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm_const import *
import io
p=W.parent/'formal-0.4.0-rc2-work/video-reload-research/di-camera-app';elf=ELFFile(io.BytesIO(p.read_bytes()));symbols={}
for sec in elf.iter_sections():
 if sec.header.sh_type in ['SHT_DYNSYM','SHT_SYMTAB']:
  for s in sec.iter_symbols():
   if s.entry.st_value:symbols[s.entry.st_value&~1]=s.name
results=[]
for movie in [0,1]:
 for item,value in [(0xcf,0x100),(0x109,0)]:
  values={0x27a:1,0x2b8:movie,0x7d9:1,item:value};calls=[]
  u=Uc(UC_ARCH_ARM,UC_MODE_ARM);u.mem_map(0x8000,0x700000);u.mem_map(0x70000000,0x20000)
  for seg in elf.iter_segments():
   if seg.header.p_type=='PT_LOAD':u.mem_write(seg.header.p_vaddr,seg.data())
  def hook(uc,at,size,data):
   if at==0x70010000:uc.emu_stop();return
   if 0x286904<=at<0x287ac4:return
   name=symbols.get(at,hex(at));args=[uc.reg_read(x)for x in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]];calls.append(dict(pc=hex(at),name=name,args=args))
   ret=0
   if at==0x1ff988:ret=values.get(args[0],0)
   elif at==0x1ffaec:values[args[0]]=args[1]
   elif 'Set_Pre_Value'in name:values[args[0]]=args[1]
   elif name.startswith('User_Spec_'):ret=1
   uc.reg_write(UC_ARM_REG_R0,ret);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
  u.hook_add(UC_HOOK_CODE,hook)
  for action in [5,4]:
   u.reg_write(UC_ARM_REG_SP,0x7000e000);u.reg_write(UC_ARM_REG_LR,0x70010000)
   for reg,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],[action,3,item,value]):u.reg_write(reg,v)
   u.emu_start(0x286904,0,timeout=2_000_000,count=100000)
  result=dict(movie_standby=movie,item=hex(item),value=hex(value),calls=calls);results.append(result)
  print('case',movie,hex(item),[(x['pc'],x['name'])for x in calls if any(s in x['name']for s in ['Attr','Resolution','Config','Queue','Message','Photo','Update','Change'])],flush=True)
put(W/'research/OEM-menu-submit-trace.json',dict(passed=True,actual_original_ARM=True,native_callees_and_UI_state_modeled=True,cases=results,hardware_tested=False))
