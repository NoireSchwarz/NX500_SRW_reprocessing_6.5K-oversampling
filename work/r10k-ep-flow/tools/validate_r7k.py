"""Field replay and production RESET/recorder tests with explicit RO services.
No camera IO, native task, hardware unlock or live allocation is executed.
"""
from pathlib import Path
import hashlib, importlib.util, json, re, struct, subprocess, sys

R=Path(__file__).resolve().parents[1]; W=R.parent; V=R/'validation/r7k'
V.mkdir(parents=True,exist_ok=True)
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from arm_model import ArmModel
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R7
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
rows=[]
def build(source,target):
 p=subprocess.run([sys.executable,str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),
  '--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),'--source',str(source),'--output',str(target)],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120)
 assert p.returncode==0,(p.stdout,p.stderr)
def arr(name,data):return 'static const unsigned char '+name+'[]={'+','.join(str(x) for x in data)+'};\n'

source=R/'core/src/nxl-session-rearm.c'; probe=R/'core/bin/nxl-session-rearm.arm'
build(source,probe); result=ArmModel(probe,['--version']).run()
assert result['exit']==0 and 'READONLY_REARM_R7K' in result['stdout'];print(result['stdout'].strip(),flush=True)
field=W/'field-r7i-reset-regression'
names=['rearm_1bf692f2ece475e72562b67e54018017','rearm_6c7db8f3853008743eb874da95035af3','rearm_048d85c806462ae9d09b99e9ed5aee76']
folders=[field/n for n in names]+[W/'field-r7j-occupied'/n for n in ['rearm_249094b2db5760778097e4b61bd5b649','rearm_309004c1109ac03235f77a290d73cc80','rearm_49c401977b63e1a91989db395b4f3df9']]

header=''
for k,name in enumerate(names):
 data=b''.join((field/name/f'backend-sample-{i:02}.bin').read_bytes() for i in range(16))
 header+=arr('field_'+str(k),data)
fixture=V/'field.h';fixture.write_text(header,encoding='utf8')
pure=V/'scope-test.c'
pure.write_text('#include <assert.h>\n#include <stdio.h>\n#include "'+(R/'core/src/session_rearm_backend.h').as_posix()+'"\n#include "'+fixture.as_posix()+'"\n'+r'''
static unsigned checks;static struct r7_backend_sample first,s,next;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void put(unsigned o,uint32_t v){memcpy(s.kernel+o,&v,4);}
int main(void){const uint32_t f=0xa1c00000u,b=0xaf65c000u;const unsigned char *sets[]={field_0,field_1};
for(unsigned k=0;k<2;k++){memcpy(&first,sets[k],sizeof first);for(unsigned i=0;i<16;i++){memcpy(&s,sets[k]+i*sizeof s,sizeof s);CHECK(!r7_backend_error(&s,f,b));CHECK(r7_backend_stable(&first,&s));}}
for(unsigned i=0;i<16;i++){memcpy(&s,field_2+i*sizeof s,sizeof s);CHECK(r7_backend_error(&s,f,b));CHECK(r7_backend_dma_refs(&s,f,b));}
/* No selector, IRQ mask, OTF shape or DMA-port/address whitelist. */
for(unsigned trial=0;trial<256;trial++){s=first;put(0x400,trial);for(unsigned i=0;i<5;i++){s.rtos[i]=trial*(i+11);s.mask[i]=trial*31+i;s.raw[i]=trial*79+i;}
s.otf[0]=trial;s.otf[1]=trial^0x987;for(unsigned i=0;i<32;i++)s.dma[i]=0x94000000u+trial*4096u+i*64u;
s.jpeg[8]=f;s.jpeg[9]=b;s.jpeg[10]=b+1024;CHECK(!r7_backend_error(&s,f,b));CHECK(r7_backend_stable(&first,&s));}
for(unsigned i=0;i<32;i++){s=first;s.dma[i]=f;CHECK(r7_backend_error(&s,f,b));s=first;s.dma[i]=b+201326592u-1;CHECK(r7_backend_error(&s,f,b));}
for(unsigned test=0;test<16;test++){s=first;switch(test){case 0:s.kernel_readable=0;break;case 1:s.top_readable=0;break;case 2:s.rtos_readable=0;break;case 3:s.jpeg_readable=0;break;
case 4:s.mutex[0]=0;break;case 5:put(0x74,0x20);break;case 6:put(0x31c,1);break;case 7:put(0x340,1);break;case 8:put(0x300,1);break;case 9:s.owner=1;break;
case 10:s.jpeg[0]=1;break;case 11:s.jpeg[4]=2;break;case 12:s.clock[0]|=0x80;break;case 13:s.clock[1]|=0x1e0000;break;case 14:put(4,0);break;case 15:s.mutex[1]=0;break;}CHECK(r7_backend_error(&s,f,b));}
next=first;next.mutex[0]=0;CHECK(!r7_backend_stable(&first,&next));next=first;next.owner=1;CHECK(!r7_backend_stable(&first,&next));
next=first;next.kernel[0x31c]=1;CHECK(!r7_backend_stable(&first,&next));next=first;next.jpeg[0]=1;CHECK(!r7_backend_stable(&first,&next));
printf("PASS scoped retirement: 32 exact free field samples, 16 overlapping samples held, 256 unlisted background contexts, active/queue/IRQ-driver/read/DMA failures; checks=%u\n",checks);return 0;}
''',encoding='utf8')
target=V/'scope-test.arm';build(pure,target);result=ArmModel(target).run();assert result['exit']==0,result
rows.append(dict(name='scoped-predicates',**result));print(result['stdout'].strip(),flush=True)

legacy=V/'legacy-replay.c'
legacy.write_text('#include <assert.h>\n#include <stdio.h>\n#include "'+(W/'r7i/core/src/session_rearm_backend.h').as_posix()+'"\n#include "'+fixture.as_posix()+'"\n'+r'''
int main(void){const unsigned char*sets[]={field_0,field_1};for(unsigned k=0;k<2;k++)for(unsigned i=0;i<16;i++){struct r7_backend_sample s;memcpy(&s,sets[k]+i*sizeof s,sizeof s);const char*e=r7_backend_error(&s,0xa1c00000u,0xaf65c000u);assert(e&&!strcmp(e,"BACKEND_IRQ_NOT_QUIET"));}puts("PASS R7i exact failure replay: both 16-sample free-range checks rejected by global profile");return 0;}
''',encoding='utf8')
target=V/'legacy-replay.arm';build(legacy,target);result=ArmModel(target).run();assert result['exit']==0,result
rows.append(dict(name='legacy-replay',**result));print(result['stdout'].strip(),flush=True)
target=V/'allocation-contract.arm';build(R/'core/tests/test_session_rearm_contract.c',target)
result=ArmModel(target).run();assert result['exit']==0,result;rows.append(dict(name='allocation-contract',**result));print(result['stdout'].strip(),flush=True)

def span(text,name):
 m=re.search(r'\b(?:static\s+)?(?:int|void)\s+'+name+r'\s*\([^)]*\)\s*\{',text);assert m,name
 i=m.end();depth=1;state='code'
 while depth:
  c=text[i];n=text[i:i+2]
  if state=='code':
   if n=='/*':state='comment';i+=2;continue
   if n=='//':state='line';i+=2;continue
   if c=='"':state='string'
   elif c=="'":state='char'
   elif c=='{':depth+=1
   elif c=='}':depth-=1
  elif state=='comment':
   if n=='*/':state='code';i+=2;continue
  elif state=='line':
   if c=='\n':state='code'
  elif c=='\\':i+=2;continue
  elif state=='string' and c=='"':state='code'
  elif state=='char' and c=="'":state='code'
  i+=1
 return m.start(),i

class RecorderModel(ArmModel):
 def __init__(self,path,args,fail=None):
  self.fds={};self.files={};self.serial=10;self.fail=fail;self.dirs=set();super().__init__(path,args)
  # Linux ARM kuser_cmpxchg for musl stdio locks. The CPU model has one
  # executing thread; this implements its compare/store result, not concurrency.
  self.uc.mem_write(0xffff0fc0,struct.pack('<8I',0xe5923000,0xe1530000,0x1a000002,0xe5821000,0xe3a00000,0xe12fff1e,0xe3a00001,0xe12fff1e))
 def run(self):
  # Production scans the complete 45056-entry table repeatedly and formats
  # durable records; use a larger bounded CPU allowance than pure predicates.
  self.uc.emu_start(self.entry,0,timeout=30_000_000,count=1_200_000_000)
  if self.error or self.exit_code is None:
   raise RuntimeError(str((self.error,self.calls,self.output.decode(errors='replace'),[(p,len(b)) for p,b in self.files.items()])))
  return dict(exit=self.exit_code,stdout=self.output.decode(errors='replace'),modeled_syscalls=self.calls,hardware_tested=False)
 def string(self,p):
  out=bytearray()
  for i in range(2048):
   c=bytes(self.uc.mem_read(p+i,1))
   if c==b'\0':return out.decode()
   out.extend(c)
  raise AssertionError('model path bound')
 def interrupt(self,uc,number,data):
  nr=uc.reg_read(UC_ARM_REG_R7);a=[uc.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_R4,UC_ARM_REG_R5]]
  handled=nr in [5,6,39,55,118,140,197,221] or nr in [4,146] and a[0] in self.fds
  if not handled:return super().interrupt(uc,number,data)
  self.calls[nr]=self.calls.get(nr,0)+1
  if nr==5:
   path=self.string(a[0]);assert path.startswith('/mnt/mmc/NXDEMO51/JOB_TEST/rearm_') or path=='/dev/mem',path
   if self.fail=='open' and path.endswith('RESET_TIMELINE.jsonl'):self.ret(-28);return
   if a[1]&0x80 and path in self.files:self.ret(-17);return
   self.serial+=1;self.fds[self.serial]={'path':path,'flags':a[1],'at':0};self.files.setdefault(path,bytearray());self.ret(self.serial)
  elif nr==39:self.dirs.add(self.string(a[0]));self.ret(0)
  elif nr==6:self.fds.pop(a[0]);self.ret(0)
  elif nr in [55,221]:self.ret(self.fds[a[0]]['flags'] if a[1]==3 else 0)
  elif nr==118:self.ret(0)
  elif nr==197:
   b=bytearray(104);struct.pack_into('<IIII',b,16,0o100600,1,0,0);uc.mem_write(a[1],bytes(b));self.ret(0)
  elif nr==140:
   f=self.fds[a[0]];off=(a[1]<<32)|a[2]
   if off>=1<<63:off-=1<<64
   f['at']=(0 if a[4]==0 else f['at'] if a[4]==1 else len(self.files[f['path']]))+off
   uc.mem_write(a[3],struct.pack('<Q',f['at']));self.ret(0)
  elif nr in [4,146]:
   f=self.fds[a[0]]
   if self.fail=='write' and f['path'].endswith('RESET_TIMELINE.jsonl'):self.ret(-28);return
   if nr==4:b=bytes(uc.mem_read(a[1],a[2]))
   else:
    chunks=[]
    for i in range(a[2]):
     at,n=struct.unpack('<II',uc.mem_read(a[1]+i*8,8));chunks.append(bytes(uc.mem_read(at,n)))
    b=b''.join(chunks)
   self.files[f['path']].extend(b);self.ret(len(b))

def variant(name,which,memory_busy=False,driver_busy=False,final_change=False):
 folder=folders[which]
 tables=[(folder/f'allocator-{i}.bin').read_bytes() for i in range(3)]
 if memory_busy:
  table=bytearray(tables[0]);start=(0xa1c00000-0x94000000)//16384
  for i in range(8192):struct.pack_into('<4I',table,(start+i)*16,35,8192-i,123 if i==0 else 0,456 if i==0 else 0)
  tables=[bytes(table)]*3
 words=struct.unpack('<180224I',tables[0]);cursor=0;free_blocks=0
 while cursor<45056:
  if words[4*cursor]&255:cursor+=words[4*cursor+1]
  else:free_blocks+=1;cursor+=1

 assert all(x==tables[0] for x in tables)
 fr=b''.join((folder/f'front-readiness-{i}.bin').read_bytes() for i in range(3))
 roots={}
 for line in (folder/'reads.log').read_text(encoding='utf8').splitlines():
  m=re.match(r'ROOT_WORD address=0x([a-f0-9]+) value=0x([a-f0-9]+)',line)
  if m:roots.setdefault(int(m[1],16),int(m[2],16))
 fixture_text=arr('model_table',tables[0])+arr('model_front',fr)+arr('model_backend',b''.join((folder/f'backend-sample-{i:02}.bin').read_bytes() for i in range(16)))
 fixture_text+='static const uint32_t root_words[][2]={'+','.join('{%uu,%uu}'%pair for pair in sorted(roots.items()))+'};\nstatic unsigned model_pass;\n'
 mock_ram=r'''static int ram(uint32_t a,void*b,unsigned n){
 memset(b,0,n);const struct r7_backend_sample*s=(const void*)(model_backend+(backend_samples<16?backend_samples:15)*sizeof *s);
 if(a==EP_KERNEL_STATE_VA-EP_KERNEL_DELTA&&n==sizeof s->kernel){memcpy(b,s->kernel,n);DRIVER_BUSY return 1;}
 if(a==EP_KERNEL_MUTEX_VA-EP_KERNEL_DELTA&&n==12){memcpy(b,s->mutex,n);return 1;}
 if(a==0x817d8aacu&&n==20){memcpy(b,s->rtos,n);return 1;}if(a==0x817d8c98u&&n==4){memcpy(b,&s->owner,n);return 1;}
 for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof *ep_kernel_guards;i++)if(a==ep_kernel_guards[i].va-EP_KERNEL_DELTA&&n==32){memcpy(b,ep_kernel_guards[i].words,n);return 1;}
 if(a==0x81dfc908u&&n==R7_BLOCKS*16u){memcpy(b,model_table,n);FINAL_CHANGE model_pass++;return 1;}
 const uint32_t head[][2]={{0x817dc498,0x80574230},{0x817dc49c,0x805742ac},{0x817dc4bc,0x81dfc800},{0x81dfc800,0x80574b60},{0x81dfc904,0x80574c48},{0x81eac908,0x94000000},{0x81eac90c,45056},{0x81eac910,32071}};
 if(n==4)for(unsigned i=0;i<sizeof head/sizeof *head;i++)if(a==head[i][0]){memcpy(b,head[i]+1,4);return 1;}
 const struct fr_snapshot *f=(const void*)(model_front+(model_pass?model_pass-1:0)*sizeof *f);
 for(unsigned j=0;j<n;j+=4){unsigned found=0;for(unsigned i=0;i<FR_FIELDS;i++)if(a+j==f->field[i].address){memcpy((char*)b+j,&f->field[i].value,4);found=1;break;}
 if(!found)for(unsigned i=0;i<sizeof root_words/sizeof *root_words;i++)if(a+j==root_words[i][0]){memcpy((char*)b+j,root_words[i]+1,4);found=1;break;}if(!found)return 0;}return 1;}
'''.replace('DRIVER_BUSY','{uint32_t busy=1;memcpy((char*)b+0x31c,&busy,4);}' if driver_busy else '').replace('{0x81eac910,32071}', '{0x81eac910,'+str(free_blocks)+'}').replace('FINAL_CHANGE','if(model_pass==3)((unsigned char*)b)[8]^=1;' if final_change else '')
 mock_mmio=r'''static int mmio(uint32_t a,uint32_t*out){if(!mmio_allowed(a))return 0;mmio_reads++;const struct r7_backend_sample*s=(const void*)(model_backend+(backend_samples<16?backend_samples:15)*sizeof *s);
 if(a>=0x20821020u&&a<=0x20821024u){*out=s->clock[(a-0x20821020u)/4];return 1;}
 for(unsigned i=0;i<5;i++){if(a==0x20821070u+i*4){*out=s->mask[i];return 1;}if(a==0x208210c8u+i*4){*out=s->raw[i];return 1;}}
 if(a==0x20821118u||a==0x20821218u){*out=s->otf[a==0x20821218u];return 1;}
 for(unsigned i=0;i<32;i++)if(a==0x20820000u+r7_dma_offsets[i]){*out=s->dma[i];return 1;}
 for(unsigned i=0;i<12;i++)if(a==0x2082a000u+r7_jpeg_offsets[i]){*out=s->jpeg[i];return 1;}return 0;}
'''
 mocks={'ram':mock_ram,'mmio':mock_mmio,
  'auth':'static int auth(void){return 1;}',
  'metadata':'static int metadata(void){old_pa=0xa1c00000u;old_va=0x90ba8000u;old_nonce=3153779565u;return 1;}',
  'backend_info':'static int backend_info(void){backend_pa=0xaf65c000u;backend_va=0x9e604000u;return 1;}',
  'no_native_locks':'static int no_native_locks(void){return 1;}',
  'firmware':'static int firmware(void){return 1;}',
  'process_setup':'static int process_setup(void){workers_idle=1;return 1;}',
  'physical_guards':'static int physical_guards(void){return 1;}',
  'shell_check':'static int shell_check(unsigned wait){(void)wait;parked=queue_empty=1;return 1;}',
  'no_links':'static int no_links(const char *s){(void)s;return 1;}'}
 text=source.read_text(encoding='utf8')
 for function,body in mocks.items():
  start,end=span(text,function);text=text[:start]+body+text[end:]
 text=text.replace('#include "reset_decision_recorder.h"',fixture_text+'\n#include "reset_decision_recorder.h"')
 text=re.sub(r'#include "([^"]+)"',lambda m:'#include '+json.dumps((R/'core/src'/m[1]).resolve().as_posix()),text)
 text='#pragma GCC diagnostic ignored "-Wunused-variable"\n#pragma GCC diagnostic ignored "-Wunused-const-variable"\n'+text
 generated=V/(name+'.c');generated.write_text(text,encoding='utf8');target=V/(name+'.arm');build(generated,target)
 return target,list(mocks)

job='/mnt/mmc/NXDEMO51/JOB_TEST';out=job+'/rearm_0123456789abcdef0123456789abcdef'
args=['--job',job,'--previous','/mnt/mmc/NXDEMO51/JOB_OLD','--token','0123456789abcdef0123456789abcdef','--output',out]
positive=None;modeled=[]
for name,which,memory_busy,driver_busy,expected in [('field-free-1',0,False,False,0),('field-free-2',1,False,False,0),
 ('same-original-shape-held',0,True,False,1),('active-driver-queue',0,False,True,1),('reused-field-1',3,False,False,0),('reused-field-2',4,False,False,0),('reused-field-3',5,False,False,0),('final-allocator-changed',0,False,False,1)]:
 target,modeled=variant(name,which,memory_busy,driver_busy,final_change=name=='final-allocator-changed');m=RecorderModel(target,args);result=m.run();assert result['exit']==expected,(name,result)
 report=json.loads(bytes(m.files[out+'/result.json']));timeline=[json.loads(line) for line in bytes(m.files[out+'/RESET_TIMELINE.jsonl']).splitlines()]
 has_proof=out+'/REARM.proof' in m.files;assert has_proof==(expected==0),(name,report)
 assert sum(row.get('event')=='allocator_snapshot' for row in timeline)==4
 assert sum(row.get('event')=='transaction_observation' for row in timeline)==16
 assert timeline[-1]['new_native_job_authorized'] is False
 if expected==0:
  assert report['backend_resources_retired']==1 and report['native_outcome_resolved']==1
  assert report['front_allocation_retired']==report['backend_allocation_retired']==report['allocator_layout_verified']==report['allocator_final_revalidated']==1
  if which>=3:assert report['old_range_free']==0 and report['front_range_state']=='REUSED'
  proof=bytes(m.files[out+'/REARM.proof']);body,tail=proof.split(b'sha256=');assert hashlib.sha256(body).hexdigest().encode()==tail.splitlines()[0]
  assert timeline[-1]['verdict']=='RETIRED_NEEDS_INIT';positive=positive or target
 else:assert report['category_failure_mask']&(1 if name=='final-allocator-changed' else 2 if memory_busy else 64)
 for leaf in ['result.json','RESET_TIMELINE.jsonl','RESET_DECISION.txt']+(['REARM.proof'] if has_proof else []):
  (V/(name+'-'+leaf)).write_bytes(bytes(m.files[out+'/'+leaf]))
 rows.append(dict(name=name,exit=result['exit'],reason=report['reason'],proof=has_proof,decision_records=len(timeline),passed=True))
 print('PASS production RESET + recorder:',name,report['reason'],flush=True)
for failure in ['open','write']:
 m=RecorderModel(positive,args,fail=failure);result=m.run();assert result['exit']!=0,result
 assert out+'/REARM.proof' not in m.files
 rows.append(dict(name='recorder-'+failure+'-failure',exit=result['exit'],proof=False,passed=True))
 print('PASS recorder',failure,'failure holds; no receipt',flush=True)
report=dict(format='NX51_R7K_ALLOCATION_LIFETIME_VALIDATION2',passed=True,hardware_tested=False,
 production_source_sha256=sha(source),backend_contract_sha256=sha(R/'core/src/session_rearm_backend.h'),
 recorder_source_sha256=sha(R/'core/src/reset_decision_recorder.h'),probe_sha256=sha(probe),probe_bytes=probe.stat().st_size,
 checks=rows,modeled_services=modeled,production_paths_executed=['main','resources','pool','rootwindow','fr_take','backend_guards','backend_take','backend_proof','result','decision_begin','decision_allocator','decision_backend','decision_finish','proof encoding'],
 scope='Compiled actual ARM production paths with explicit auth/firmware/process/RAM/MMIO/shell/path and file/clock service models. Two field-backed positive runs, allocated-range/active-driver negatives, logger open/write failures. Models establish control reachability and policy behavior, not camera ownership or live hardware safety.',camera_test_pending=True)
(V/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print(json.dumps(dict(passed=True,checks=len(rows),probe_bytes=report['probe_bytes'])),flush=True)
