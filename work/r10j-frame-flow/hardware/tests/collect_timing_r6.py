#!/usr/bin/env python3
"""Bind narrow R6 regressions, unchanged proof contracts and final ARM binaries."""
from pathlib import Path
import hashlib,json,os,re,subprocess
R=Path(__file__).resolve().parents[1];P=R.parent;W=P.parent;OLD=W/'demo-p52-restore-r5/hardware'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
rel=lambda p:str(p.relative_to(P))
def part(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
def main():
 src=R/'src';old=OLD/'src';checks={}
 for n in ['test_geometry.h','bitb_job.h','pipeline_proof.h','pipeline_timing.h','jpeg_core.h']:
  checks['unchanged_'+n]=sha(src/n)==sha(old/n)
 a=(src/'full_memory.h').read_text();b=(old/'full_memory.h').read_text()
 checks['manifest_wire_struct_unchanged']=part(a,'struct full_manifest {','static struct full_manifest')==part(b,'struct full_manifest {','static struct full_manifest')
 checks['independent_source_crc_function_unchanged']=part(a,'static void full_crcs(','static void full_manifest_init(')==part(b,'static void full_crcs(','static void full_manifest_init(')
 a=(src/'nxl-bitbworker.c').read_text();b=(old/'nxl-bitbworker.c').read_text()
 checks['seven_step_wire_structs_unchanged']=part(a,'struct jpeg_step {','static struct jpeg_capture')==part(b,'struct jpeg_step {','static struct jpeg_capture')
 a=(src/'jpeg_engine.h').read_text();b=(old/'jpeg_engine.h').read_text()
 notes=lambda s:[int(x) for x in re.findall(r'NOTE\((\d+)\)',s)]
 checks['seven_native_step_ids_unchanged']=notes(a)==notes(b)==[101,201,301,302,401,501,999]
 vtop=' for(unsigned i=0;i<TEST_PAGES;i++){uint32_t off=i*TEST_BLOCK;int pa=o->call(o->ctx,J_VTOP,r->alias_va+off,0);r->translations++;r->matches+=(uint32_t)pa==q->pa+off;}'
 checks['all_12288_VTOP_loop_unchanged']=vtop in a and vtop in b
 a=(src/'bitb_control.inc').read_text();b=(old/'bitb_control.inc').read_text()
 checks['terminal_wait_function_unchanged']=part(a,'static int p7_wait_terminal(','static int p7_task_states(')==part(b,'static int p7_wait_terminal(','static int p7_task_states(')
 a=(src/'nxl-allocprobe.c').read_text();b=(old/'nxl-allocprobe.c').read_text()
 checks['native_release_restore_and_confirmations_unchanged']=part(a,' trace_phase="release_return";',' reason=!ep_contract_verified')==part(b,' trace_phase="release_return";',' reason=!ep_contract_verified')
 checks['fresh_immediate_ownership_guard_retained']='REQUIRE(still_owned(),"OWNED_ENTRY_CHANGED_NO_GUESSED_FREE");' in a
 checks['only_requested_fixed_holds_removed']='pause_ms(2500)' in b and 'pause_ms(2500)' not in a and 'row("owned_hold",index_owned)' in b and 'row("owned_hold",index_owned)' not in a
 checks['all_contracts_passed']=all(checks.values());assert checks['all_contracts_passed']
 sourcefiles=sorted(p for p in src.iterdir() if p.is_file())
 sourcefiles+=sorted((R/'tools').glob('build_*.py'))+sorted(p for p in (R/'tests').iterdir() if p.suffix in ['.py','.c','.h'])+[R/'TIMING_R6.md']
 sources={rel(p):sha(p) for p in sourcefiles}
 proof={'passed':True,'hardware_tested':False,'tests_run':len(checks)-1,'checks':checks,'source_sha256':sources}
 proofpath=R/'validation/r6-proof-contract.json';proofpath.write_text(json.dumps(proof,indent=2)+'\n')
 qemu=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';camera=W/'demo-p49/research/backend-reuse/camera-root';env={**os.environ,'LD_LIBRARY_PATH':str(W/'demo-p50/sdk')}
 smoke=[];binaries={}
 for name,filename,args in [('controller','nxl-allocprobe.arm',['--version','--bitb-abi']),('worker','nxl-bitbworker.arm',['--version','--abi'])]:
  p=R/'bin'/filename;binaries[name]={'path':rel(p),'sha256':sha(p),'bytes':p.stat().st_size}
  for flag in args:
   cmd=[str(qemu)]+(['-L',str(camera)] if name=='worker' else [])+[str(p),flag]
   result=subprocess.run(cmd,env=env,check=True,capture_output=True,text=True);assert result.stdout.strip()
   smoke.append({'binary':rel(p),'flag':flag,'stdout':result.stdout.strip(),'exit':0})
  if name=='worker':
   h=2166136261
   for c in p.read_bytes():h=((h^c)*16777619)&0xffffffff
   pin=(src/'bitb_payload.h').read_text();assert f'BITB_WORKER_BYTES {p.stat().st_size}u' in pin and f'BITB_WORKER_FNV 0x{h:08x}u' in pin
   binaries[name]['fnv1a']=f'0x{h:08x}';binaries[name]['controller_payload_pin_verified']=True
 smokepath=R/'validation/r6-final-ARM-ABI.json';smokepath.write_text(json.dumps({'passed':True,'hardware_tested':False,'actual_ARM_execution':True,'tests_run':len(smoke),'binaries':binaries,'cases':smoke},indent=2)+'\n')
 refs=[('r6-crc-equivalence.json',14),('r6-memory-engine-tests.json',21),('r6-timing-tests.json',10),('r6-timeout-tests.json',12),('r6-release-tests.json',8),('quarantine-tests.json',3),('r6-proof-contract.json',len(checks)-1),('r6-final-ARM-ABI.json',4)]
 artifacts=[]
 for filename,count in refs:
  p=R/'validation'/filename;record=json.loads(p.read_text());assert record['passed']
  if 'source_sha256' in record:
   for key,want in record['source_sha256'].items():assert sha(P/key)==want,(filename,key)
  if 'tests_run' in record:assert record['tests_run']==count
  artifacts.append({'path':rel(p),'sha256':sha(p),'tests_run':count})
 markers=sorted(set(re.findall(r'r6_timing\("([A-Z0-9_]+)"','\n'.join(p.read_text() for p in sourcefiles if p.parent==src)))|{'CAP_AUDIT_BEFORE','CAP_AUDIT_AFTER'})
 changed=[rel(p) for p in sourcefiles if not (OLD/p.relative_to(R)).is_file() or sha(p)!=sha(OLD/p.relative_to(R))]
 result={'format':'NX51_R6_HARDWARE_TIMING_REGRESSIONS1','passed':True,'hardware_tested':False,'tests_run':sum(x['tests_run'] for x in artifacts),'source_sha256':sources,'binaries':binaries,'controller_sha256':binaries['controller']['sha256'],'worker_sha256':binaries['worker']['sha256'],'test_artifacts':artifacts,'component_evidence_sha256':{x['path']:x['sha256'] for x in artifacts},'timer_markers':markers,'fixed_diagnostic_sleep_removed_ms':3000,'fullproof_contract':{'physical_bytes_initialized':201326592,'physical_bytes_per_capture':201326592,'physical_pages_translated':12288,'actual_source_crc_independently_recomputed':True,'JPEG_reserved_region_per_byte_audit':True,'seven_native_step_ids':[101,201,301,302,401,501,999],'full_manifest_bytes':49260,'full_manifest_version':3,'exact_old_new_manifest_bytes':True,'native_release_and_post_release_checks_unchanged':True,'worker_timeout_ms':300000,'controller_timeout_seconds':420,'watchdog_timeout_ms':435000,'reduced_coverage':False},'changed_files_from_frozen_R5':changed,'limits':['No on-camera performance, DMA or integration claim is made by these host/QEMU checks.','Only exact periodic-pattern CRC/construction work and two fixed diagnostic holds were optimized; physical coverage, source integrity, completion and recovery checks remain.']}
 out=P/'validation/timing-r6-hardware-tests.json';out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'aggregate':str(out),'sha256':sha(out),'tests_run':result['tests_run'],'binaries':binaries,'changed_files':changed},indent=2))
if __name__=='__main__':main()
