#!/usr/bin/env python3
"""Build and bind R7 read-width/fault-scope evidence; no camera/native calls."""
from pathlib import Path
import hashlib,json,os,re,struct,subprocess,sys,tempfile
R=Path(__file__).resolve().parents[1];P=R.parent;W=P.parent
sys.path.insert(0,str(W/'wb-load-p42/deps'))
from elftools.elf.elffile import ELFFile
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rel=lambda p:str(p.relative_to(P))
def run(cmd,**kw):
 try:return subprocess.run([str(x) for x in cmd],check=True,capture_output=True,text=True,timeout=40,**kw)
 except subprocess.CalledProcessError as e:
  print(e.stdout or "",file=sys.stderr);print(e.stderr or "",file=sys.stderr);raise
def main():
 zig=W/'wb-load-p42/deps/ziglang/zig';runtime=W/'wb-load-p42/cross-runtime/arm-linux-musleabi-cross';qemu=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';source=R/'src/nxl-oem-restore.c';header=R/'src/oem_ro_read.h';binary=R/'bin/nxl-oem-restore.arm'
 run([sys.executable,R/'tools/build_oem_restore.py','--zig',zig,'--cross-runtime',runtime])
 results=R/'results';results.mkdir(exist_ok=True);reports=[]
 with tempfile.TemporaryDirectory(prefix='oem-r7-tests-',dir=W) as td:
  t=Path(td)
  for name,count in [('guard',104),('integration',20)]:
   c=R/f'tests/test_oem_ro_{name}.c';exe=t/name
   run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-misleading-indentation',c,'-o',exe])
   args=[exe]+([t/'fault-report'] if name=='integration' else [])
   p=run(args,cwd=t);assert f'PASS {count} ' in p.stdout
   rec={'passed':True,'hardware_tested':False,'tests_run':count,'actual_production_source':True,'test_source_sha256':sha(c),'helper_source_sha256':sha(source),'reader_source_sha256':sha(header),'stdout':p.stdout,'stderr':p.stderr}
   if name=='integration':
    report=json.loads((t/'fault-report/result.json').read_text());assert report['classification']=='SAFE_NO_SUBMIT' and report['native_calls']==report['request_submitted']==report['api_inflight']==0 and report['fault_signal']==11 and report['last_read_width']==4 and report['fault_address']==0x20810308
    rec['actual_generated_fault_result']=report;rec['fault_log_bytes']=(t/'fault-report/restore.log').stat().st_size;assert rec['fault_log_bytes']>0
   out=results/f'recovery-r7-{name}.json';out.write_text(json.dumps(rec,indent=2)+'\n');reports.append((out,count))
  arm=t/'guard.arm';run([sys.executable,R/'tools/build_front.py','--zig',zig,'--cross-runtime',runtime,'--source',R/'tests/test_oem_ro_guard.c','--output',arm])
  p=run([qemu,arm],cwd=t);assert 'PASS 101 ' in p.stdout
  rec={'passed':True,'hardware_tested':False,'actual_ARM_execution':True,'tests_run':101,'source_sha256':{'core/src/oem_ro_read.h':sha(header),'core/tests/test_oem_ro_guard.c':sha(R/'tests/test_oem_ro_guard.c')},'binary_sha256':sha(arm),'stdout':p.stdout,'stderr':p.stderr,'limits':['QEMU user-mode translates truncated host-file BUS si_addr outside the precise guest load window. Three host-truncation assertions run only on host; this ARM run retains exact-window checks and exercises real guest SEGV, masks, and out-of-scope fatal signals.','No actual ARM device bus read or camera hardware is executed.']}
  out=results/'recovery-r7-ARM-signals.json';out.write_text(json.dumps(rec,indent=2)+'\n');reports.append((out,101))
 # The production helper object, not an imitation function, must contain one
 # aligned 32-bit LDR for MMIO and a separate LDRB solely for normal RAM bytes.
 obj=R/'build/nxl-oem-restore.o';instructions={}
 with obj.open('rb') as f:
  e=ELFFile(f);symbols=e.get_section_by_name('.symtab')
  for name,want in [('oem_mmio_load32',[0xe5900000,0xe12fff1e]),('oem_ram_load8',[0xe5d00000,0xe12fff1e])]:
   sym=next(s for s in symbols.iter_symbols() if s.name==name);section=e.get_section(sym['st_shndx']);raw=section.data()[sym['st_value']:sym['st_value']+sym['st_size']];actual=[x[0] for x in struct.iter_unpack('<I',raw)];assert actual==want
   instructions[name]={'bytes':raw.hex(),'ARM_words':[hex(x) for x in actual]}
 out=results/'recovery-r7-ARM-load-width.json';out.write_text(json.dumps({'passed':True,'hardware_tested':False,'tests_run':2,'object_sha256':sha(obj),'binary_sha256':sha(binary),'source_sha256':{'core/src/nxl-oem-restore.c':sha(source),'core/src/oem_ro_read.h':sha(header)},'functions':instructions,'MMIO_load_bytes':4,'MMIO_byte_halfword_double_vector_loads':0},indent=2)+'\n');reports.append((out,2))
 # Existing auth/cleanup/completion contracts execute unchanged native controls.
 run([sys.executable,R/'tests/run_oem_independent_review.py'])
 for name,count in [('original-ui-wait-race.json',4),('independent-oem-contract.json',67),('independent-oem-cleanup.json',6),('independent-oem-auth.json',11),('independent-oem-arm-smoke.json',2)]:reports.append((results/name,count))
 old=(W/'demo-p52-timing-r6/core/src/nxl-oem-restore.c').read_text();current=source.read_text()
 def function(s,n):
  a=s.index(n);a=s.index('{',a);depth=0
  for b in range(a,len(s)):
   depth+=(s[b]=='{')-(s[b]=='}')
   if not depth:return s[a:b+1]
  raise AssertionError(n)
 unchanged={name:function(old,name)==function(current,name) for name in ['static int authenticate_supervised(', 'static int take_manual_locks(', 'static int finish_manual_locks(', 'static int restore_stack(', 'static int quarantine_unknown(', 'static int invoke6_start(', 'static int observe_completion(']}
 assert all(unchanged.values())
 # Recursively bind every local include used by the production helper.
 sources={}
 def include(p):
  if rel(p) in sources:return
  sources[rel(p)]=sha(p)
  for n in re.findall(r'^#include "([^"]+)"',p.read_text(),re.M):include(p.parent/n)
 include(source)
 for p in [Path(__file__),R/'tests/test_oem_ro_guard.c',R/'tests/test_oem_ro_integration.c',R/'tools/build_front.py',R/'tools/build_oem_restore.py']:sources[rel(p)]=sha(p)
 artifacts=[]
 for p,n in reports:
  data=json.loads(p.read_text());assert data['passed'] and not data['hardware_tested'];artifacts.append({'path':rel(p),'sha256':sha(p),'tests_run':n})
 result={'format':'NX51_RECOVERY_RO_WIDTH_R7_TESTS1','passed':True,'hardware_tested':False,'hardware_recovery_proven':False,'tests_run':sum(x['tests_run'] for x in artifacts),'source_sha256':sources,'binary':{'path':rel(binary),'sha256':sha(binary),'bytes':binary.stat().st_size},'binary_sha256':sha(binary),'component_evidence_sha256':{x['path']:x['sha256'] for x in artifacts},'test_artifacts':artifacts,'unchanged_native_protocol_and_ownership_functions':unchanged,'policy':{'RAM_code_range':['0x80000000','0x94000000 exclusive'],'RAM_read_max':4096,'MMIO_exact_whitelist_words':24,'MMIO_load_bytes':4,'CAP_allocations':0,'CAP_releases':0,'MMIO_writes':0,'UI_flag_writes':0,'Wait_calls':0,'fault_scope':'only synchronous positive-code BUS/SEGV in the exact owned PROT_READ load window; output copies and all native work remain outside','pre_submit_ineligible_header_skips_MMIO':True,'post_submit_full_observation_retained':True},'field_cause_limit':'Old field logs are empty before the observed SIGBUS. Source confirms byte-width MMIO reads, but the exact failing field address/stage cannot be proved retrospectively.','limits':['Host and QEMU signal/contract tests are not a successful on-camera OEM recovery demonstration.','QEMU cannot precisely translate the particular truncated host-file BUS address; its three corresponding checks are host-only, without relaxing the production handler.']}
 out=P/'validation/recovery-r7-helper-tests.json';out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'aggregate':str(out),'sha256':sha(out),'tests_run':result['tests_run'],'binary':result['binary']},indent=2))
if __name__=='__main__':main()
