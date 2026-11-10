#!/usr/bin/env python3
"""Compile actual production main/resources/collector with explicit RO service models.
The fixture does not execute camera IO, ISP, allocator API, or a native job.
"""
from pathlib import Path
import tempfile,subprocess,hashlib,json,re,os,shutil
C=Path(__file__).resolve().parents[1];W=C.parents[1];S=C/'src/nxl-session-rearm.c';raw=S.read_text();K=W/'demo51-field-success/JOB_1793869199_415_10dc3936/jpeg/attempt_1/hardware/p11_kernel_before.bin';rows=[]
def span(s,name):
 m=re.search(r'\b(?:static\s+)?(?:int|void)\s+'+name+r'\s*\([^)]*\)\s*\{',s);assert m,name;i=m.end();depth=1;state='code'
 while depth:
  c=s[i];n=s[i:i+2]
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
with tempfile.TemporaryDirectory(prefix='rearm-collector-',dir=C/'build') as td:
 T=Path(td);root=T/'sd/NXDEMO51';root.mkdir(parents=True)
 s=raw
 mocks={
 'auth':'static int auth(void){authenticated=1;return 1;}',
 'metadata':'static int metadata(void){old_pa=0xa1c00000u;old_va=0x90ba8000u;old_nonce=123;return 1;}',
 'backend_info':'static int backend_info(void){backend_pa=0xaf65c000u;backend_va=0x9e608000u;return 1;}',
 'no_native_locks':'static int no_native_locks(void){return 1;}',
 'firmware':'static int firmware(void){return 1;}',
 'process_setup':'static int process_setup(void){workers_idle=1;return 1;}',
 'shellproof':'static int shellproof(void){parked=queue_empty=1;return 1;}',
 'physical_guards':'static int physical_guards(void){return 1;}',
 'ram':r'''static int ram(uint32_t a,void*b,unsigned n){memset(b,0,n);if(a==EP_KERNEL_STATE_VA-EP_KERNEL_DELTA){FILE*f=fopen(getenv("KERNEL_FIXTURE"),"rb");int ok=f&&fread(b,1,n,f)==n;if(f)fclose(f);return ok;}if(a==EP_KERNEL_MUTEX_VA-EP_KERNEL_DELTA){FILE*f=fopen(getenv("KERNEL_FIXTURE"),"rb");int ok=f&&!fseek(f,EP_KERNEL_STATE_BYTES,SEEK_SET)&&fread(b,1,n,f)==n;if(f)fclose(f);return ok;}for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof*ep_kernel_guards;i++)if(a==ep_kernel_guards[i].va-EP_KERNEL_DELTA&&n==32){memcpy(b,ep_kernel_guards[i].words,32);return 1;}const uint32_t cells[][2]={{0x817dc498,0x80574230},{0x817dc49c,0x805742ac},{0x817dc4bc,0x81dfc800},{0x81dfc800,0x80574b60},{0x81dfc904,0x80574c48},{0x81eac908,0x94000000},{0x81eac90c,45056},{0x81eac910,45056},{FR_TASK,0x80592640},{FR_TASK+3364,7},{FR_TASK+3428,0x819f1000},{0x819f1000,0x805926c8},{0x81543d40,0x819f0000},{0x819f0000,1},{0x8142d154,0x80582820},{0x8142d154+3444,0x8142dee0},{0x8100f7b0,0x800e454c}};for(unsigned i=0;i<sizeof cells/sizeof*cells;i++)if(cells[i][0]>=a&&(uint64_t)cells[i][0]+4<=(uint64_t)a+n)memcpy((char*)b+cells[i][0]-a,cells[i]+1,4);if(a<=0x81dfc908&&a+n>0x81dfc908&&!strcmp(getenv("MOCK_CASE"),"front_busy")){uint32_t kind=0x23,span=8192;unsigned off=((old_pa-0x94000000u)>>14)*16u;memcpy((char*)b+0x81dfc908-a+off,&kind,4);memcpy((char*)b+0x81dfc908-a+off+4,&span,4);}return 1;}''',
 'mmio':r'''static int mmio(uint32_t a,uint32_t*out){if(!mmio_allowed(a))return 0;mmio_reads++;*out=0;const char*c=getenv("MOCK_CASE");if(a>=0x2082a000u&&a<0x2082b000u){char p[1200];snprintf(p,sizeof p,"%s/jpeg-read.marker",output);FILE*f=fopen(p,"a");if(f){fputc('x',f);fclose(f);}if(!strcmp(c,"gated"))abort();}if(a==0x20821020u&&!strcmp(c,"gated"))*out=0x80;if(a==0x20821020u&&!strcmp(c,"read_error"))return 0;if(a==0x20821300u&&!strcmp(c,"stale_dma"))*out=old_pa;if(a==0x20821300u&&!strcmp(c,"normal_pa"))*out=0x94800000u;return 1;}'''}
 for name,body in mocks.items():
  a,b=span(s,name);s=s[:a]+body+s[b:]
 s=s.replace('devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC)','devfd=open("/dev/null",O_RDONLY|O_CLOEXEC)')
 s=re.sub(r'#include "([^"]+)"',lambda m:'#include '+json.dumps(str((C/'src'/m[1]).resolve())),s)
 s='#define ROOT '+json.dumps(str(root))+'\n'+s
 src=T/'production-collector-mock.c';src.write_text(s);exe=T/'collector.host'
 subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation',str(src),'-o',str(exe)],check=True)
 previous=root/'JOB_OLD';previous.mkdir()
 for case,want in [('ok',0),('normal_pa',0),('gated',1),('read_error',1),('stale_dma',1),('front_busy',1)]:
  job=root/('JOB_'+case);job.mkdir();out=job/'rearm-1';env={**os.environ,'KERNEL_FIXTURE':str(K),'MOCK_CASE':case}
  p=subprocess.run([str(exe),'--job',str(job),'--previous',str(previous),'--token','1234567890abcdef1234567890abcdef','--output',str(out)],capture_output=True,text=True,env=env,timeout=15)
  assert p.returncode==want,(case,p.returncode,p.stdout,p.stderr);d=json.loads((out/'result.json').read_text());assert d['resource_snapshots']==3 and d['backend_samples']==16,(case,d)
  proof=out/'REARM.proof';assert proof.exists()==(want==0),(case,d)
  if want==0:
   text=proof.read_text();body,tail=text.split('sha256=');assert hashlib.sha256(body.encode()).hexdigest()==tail.splitlines()[0];assert 'backend_resources_retired=1\n' in body and 'native_outcome_resolved=1\n' in body;assert d['backend_resources_retired']==d['native_outcome_resolved']==1
   if case=='ok':dst=C/'results/session-rearm-positive-fixture.proof';dst.write_text(text);(C/'results/session-rearm-positive-fixture.json').write_text(json.dumps(d,indent=2)+'\n')
  if case in ['gated','read_error']:assert not(out/'jpeg-read.marker').exists(),case
  if case=='front_busy':assert len(list(out.glob('backend-sample-*.bin')))==16 and d['category_failure_mask']&2
  rows.append(dict(case=case,passed=True,exit=p.returncode,reason=d['reason'],category_failure_mask=d['category_failure_mask'],resource_snapshots=d['resource_snapshots'],backend_samples=d['backend_samples'],receipt=proof.exists(),stderr=p.stderr))
 report=dict(format='NX51_REARM_PRODUCTION_COLLECTOR_RO_MOCK1',passed=True,cases=rows,tests_run=len(rows),production_source_sha256=hashlib.sha256(raw.encode()).hexdigest(),compiled_variant_sha256=hashlib.sha256(s.encode()).hexdigest(),test_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),modeled_services=list(mocks),production_control_executed=['main','resources','pool','rootwindow','backend_guards','backend_take','backend_proof','result','save','receipt encoder'],kernel_fixture_sha256=hashlib.sha256(K.read_bytes()).hexdigest(),hardware_tested=False,native_calls=0,limit='All IO/auth/metadata/runtime guards are explicit RO models; actual prior metadata validated separately. This proves production control reachability and failclosed collector order, not camera retirement.')
 (C/'results/session-rearm-collector-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,tests_run=len(rows))))
