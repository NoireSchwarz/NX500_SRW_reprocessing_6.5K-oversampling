from pathlib import Path
import os,sys,json,hashlib,subprocess,re,gc,struct,time
W=Path(sys.argv[1]).resolve()if len(sys.argv)>1 else Path(__file__).resolve().parent;R=W.parent/'repository/nx500';T=W/'build-tree';B=W.parent/'r11k3b-curve-presets-work/build-tree';sys.path[:0]=[str(R/'work/tooling/python'),str(T/'tools')]
from post_file_model import PostFileModel
from capstone import Cs,CS_ARCH_ARM,CS_MODE_ARM,CS_MODE_LITTLE_ENDIAN
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
# Copy exact production functions, including the configured-task guard. Only
# pointer access to mapped device memory and the barrier are fixture endpoints.
def function(path,name):
 s=path.read_text('utf8');a=re.search(r'^static [^\n]*\b'+name+r'\([^\n]*',s,re.M);assert a,name
 start=a.start();opening=s.index('{',a.start());depth=1;i=opening+1
 while depth:
  if s[i]=='{':depth+=1
  elif s[i]=='}':depth-=1
  i+=1
 return s[start:i]
origins=[('core/src/rtos_cc.inc',n)for n in ['cc','cc_dma','cc_configured']]+[('core/src/rtos_gamma.inc','gt'),('core/src/rtos_config_iq.inc','p49_pd_record_single')]
parts=[function(T/p,n)for p,n in origins];fixture=T/'core/tests/param_dma_producer_fixture.h';fixture.write_text('/* Exact production bodies. Verified by test_srw_gamma_gate.py. */\n'+'\n'.join(parts)+'\n',encoding='utf8',newline='\n')
for (p,n),body in zip(origins,parts):assert body in(T/p).read_text('utf8')
runs=W/('gamma-gate-tests-'+str(time.time_ns()));runs.mkdir();env=dict(os.environ,PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',ZIG_GLOBAL_CACHE_DIR=str(W/'zig-global'),ZIG_LOCAL_CACHE_DIR=str(W/'zig-local'))
rows=[]
for old in [True,False]:
 source=T/'core/tests/test_gamma_param_gate.c'
 if old:
  source=runs/'old-gate.c';source.write_text('#define P49_EXPECT_OLD 1\n#define P49_OLD_CONTRACT_PATH "'+(B/'core/src/param_dma_contract.h').as_posix()+'"\n#include "'+(T/'core/tests/test_gamma_param_gate.c').as_posix()+'"\n',encoding='utf8',newline='\n')
 out=runs/('old-gate.arm'if old else'fixed-gate.arm');r=subprocess.run([sys.executable,'-B',str(T/'core/tools/build_front.py'),'--zig',str(R/'work/tooling/python/ziglang/zig.exe'),'--cross-runtime',str(R/'work/tooling/arm-linux-musleabi-cross'),'--source',str(source),'--output',str(out)],env=env,capture_output=True,text=True,encoding='utf8');(runs/(out.stem+'-build.log')).write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stderr[-5000:]
 for job in ['JOB_1794946413_806_4eeed6fb','JOB_1794946510_576_86e13b28']:
  d=runs/(out.stem+'-'+job);d.mkdir();capture=W/'camera-evidence/NXDEMO51'/job/'front_1/front'
  for src,dst in [('selected-c0.bin','c0.bin'),('gamma-test-audit.bin','gamma.bin'),('source-cc-audit.bin','cc.bin')]: (d/dst).write_bytes((capture/src).read_bytes())
  m=PostFileModel(out,[],d);result=m.run();assert result['exit']==0,result;rows.append(dict(case=d.name,**result));print(result['stdout'].strip(),flush=True);del m;gc.collect()
# Actual production RTOS output: permit ONLY cmp #2 -> cmp #4 instructions,
# so no added load/store, native call, IRQ, or cleanup change can slip through.
dis=Cs(CS_ARCH_ARM,CS_MODE_ARM|CS_MODE_LITTLE_ENDIAN);binary_diffs=[]
for name in ['rtos_front.bin','rtos_front_trial.bin']:
 a=(B/'core/bin'/name).read_bytes();b=(T/'core/bin'/name).read_bytes();assert len(a)==len(b)<=65536
 changed=[i for i in range(len(a))if a[i]!=b[i]];assert changed,(name,'no production change')
 words=sorted({i//4*4 for i in changed});decoded=[]
 for at in words:
  x=list(dis.disasm(a[at:at+4],at));y=list(dis.disasm(b[at:at+4],at));assert len(x)==len(y)==1
  before=x[0].mnemonic+' '+x[0].op_str;after=y[0].mnemonic+' '+y[0].op_str
  assert before.startswith('cmp ')and before.endswith(', #2')and after==before[:-1]+'4',(name,at,before,after)
  decoded.append(dict(offset=at,before=before,after=after))
 binary_diffs.append(dict(file=name,bytes=len(b),changed_bytes=changed,instructions=decoded))
for p in ['core/src/rtos_gamma.inc','core/src/rtos_config_iq.inc','core/src/gamma_contract.h','core/src/gamma_host.inc','core/src/gamma_transfer_profile.h','core/src/front_control.inc','core/src/front_layout.h','core/src/alloc_core.h','hardware/src/alloc_core.h','hardware/src/nxl-allocprobe.c','hardware/src/ldc_engine.h','hardware/src/shared_release_controller.inc','common/shared_workspace.h']:
 assert (T/p).read_bytes()==(B/p).read_bytes(),p
result=dict(passed=True,old_rejects_both_actual_failed_requests=True,fixed_accepts_modes_0_through_4=True,unknown_mode_still_rejected=True,actual_producer_and_configured_task_guard_used=True,actual_host_request_producer_used=True,nonce_phase_reserved_and_active_DMA_guards_checked=True,real_camera_C0_Gamma_CC_captures_used=True,actual_RTOS_instruction_changes=binary_diffs,production_functions=[dict(file=p,function=n,sha256=hashlib.sha256(body.encode()).hexdigest())for(p,n),body in zip(origins,parts)],cases=rows,OS_and_device_memory_pointer_endpoints_modeled=True,no_native_ISP_or_MMIO_call_executed=True,physical_camera_tested=False)
(W/'gamma-gate-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8',newline='\n');print('PASS actual Gamma request -> RTOS audit producer -> shared Linux/RTOS validator; old 143 reproduced and both presets accepted.',flush=True)
