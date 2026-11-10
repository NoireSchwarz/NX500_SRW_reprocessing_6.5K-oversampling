#!/usr/bin/env python3
"""Actual ARM parsing/auth rejection on private files; no native resources touched."""
from pathlib import Path
import os,sys,hashlib,json,subprocess,tempfile
R=Path(__file__).resolve().parents[1];W=R.parents[1];sys.path.insert(0,str(R/'tests'));import test_session as base
Q=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';Z=W/'wb-load-p42/deps/ziglang/zig';X=W/'wb-load-p42/cross-runtime/arm-linux-musleabi-cross';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();cases=[]
def check(name,ok):
 if not ok:raise AssertionError(name)
 cases.append(dict(case=name,passed=True))
tempfile.tempdir=str(R/'build');f=base.Fixture()
try:
 assert f.call('new').returncode==0
 text=base.FRONT.replace('mkdir "$NX51_JOB/front"','mkdir -p "$NX51_JOB/front_1/front"').replace('front-ok "$NX51_JOB/front"','front-ok "$NX51_JOB/front_1/front"')
 assert f.run(text).returncode==0 and f.run(base.BACK).returncode==0;previous=f.status()[1]['job'];assert f.call('new').returncode==0;job=Path(f.status()[1]['job']);token='a'*32;proof=job/('rearm_'+token);proof.mkdir()
 fields=[('job',str(job)),('previous',previous),('token',token),('readonly','1'),('native_calls','0'),('resource_snapshots','3'),('old_range_free','1'),('backend_range_free','1'),('backend_resources_retired','1'),('native_outcome_resolved','1'),('known_roots_clear','1'),('code_original','1'),('shell_parked','1'),('queue_empty','1'),('workers_idle','1'),('reason','EMPTY_SESSION_RESOURCE_PROOF_VERIFIED')]
 body='NX51REARMPROOF1\n'+''.join(k+'='+v+'\n' for k,v in fields)
 def publish(b):proof.joinpath('REARM.proof').write_text(b+'sha256='+hashlib.sha256(b.encode()).hexdigest()+'\nend=NX51REARMEND1\n')
 publish(body);before={p:p.read_bytes() for p in [job/'STATE.journal',f.root/'CURRENT.journal']};wrapper=R/'build/rearm-record-model.c';exe=R/'build/rearm-record-model.arm'
 wrapper.write_text('#define DEFAULT_ROOT '+json.dumps(str(f.root))+'\n#define main production_main\n#include '+json.dumps(str(R/'src/session.c'))+'\n#undef main\nint main(int argc,char **argv){if(argc==2&&!strcmp(argv[1],"records")){root=DEFAULT_ROOT;struct session s,p;if(current(&s)!=1||!rearm_previous(&s,&p))return 4;int ok=rearm_receipt(&s,&p,getenv("NX51_ARM_PROOF"));printf("eligible=1\\nreceipt_valid=%d\\nfront_attempt=%llu\\nbackend_attempt=%llu\\n",ok,(unsigned long long)s.front_attempt,(unsigned long long)s.backend_attempt);return ok?0:5;}return production_main(argc,argv);}\n')
 subprocess.run([sys.executable,str(R/'tools/build_session.py'),'--zig',str(Z),'--cross-runtime',str(X),'--source',str(wrapper),'--output',str(exe)],check=True,capture_output=True)
 env=dict(os.environ,NX51_SESSION_TOKEN=token,NX51_ARM_PROOF=str(proof));commands=[]
 def call(*args):
  p=subprocess.run([str(Q),str(exe),*args],env=env,capture_output=True,text=True,timeout=10);commands.append(dict(args=list(args),exit=p.returncode,stdout=p.stdout,stderr=p.stderr));return p
 p=call('records');check('ARM_actual_rearm_metadata_and_full_retirement_receipt_parser',p.returncode==0 and 'receipt_valid=1' in p.stdout)
 publish(body.replace('backend_resources_retired=1','backend_resources_retired=0'));p=call('records');check('ARM_missing_backend_retirement_rejected',p.returncode==5 and 'receipt_valid=0' in p.stdout)
 publish(body.replace('token='+token,'token='+'b'*32));check('ARM_stale_proof_token_rejected',call('records').returncode==5);publish(body)
 p=call('status');check('ARM_status_keeps_synthetic_hold',p.returncode==0 and 'state=RESTART_REQUIRED\n' in p.stdout and 'front_attempt=0\n' in p.stdout)
 check('ARM_forged_environment_without_live_rearm_lease_rejected',call('rearm-fresh',str(proof)).returncode==77)
 p=call('rearm',str(f.base/'untrusted.sh'),'--rearm-worker');check('ARM_production_pinned_worker_path_enforced',p.returncode==77 and 'REARM_SCRIPT' in p.stderr)
 check('ARM_tests_preserve_all_authoritative_records',all(p.read_bytes()==b for p,b in before.items()))
 report=dict(format='NX51_R7_ACTUAL_ARM_REARM_RECORD_TESTS1',passed=True,tests_run=len(cases),cases=cases,actual_ARM_execution=True,hardware_tested=False,HOST_MODEL=False,commands=commands,source_sha256={str(p.relative_to(R)):sha(p) for p in [R/'src/session.c',R/'src/journal.inc',R/'src/rearm.inc']},test_source_sha256=sha(Path(__file__)),model_binary_sha256=sha(exe),production_binary_sha256=sha(R/'bin/nxl-session.arm'),limits=['Private fixed-root path projection, not camera execution.','Direct receipt/metadata parser positive is a modeled fully true receipt, not proof of hardware retirement.','Production API entry is also tested to deny forged environment without live mode=rearm lease.','No worker or native helper was executed; production worker-path pin is tested before supervisor execution.'])
 (R/'validation/arm-rearm-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,tests_run=len(cases),hardware_tested=False)))
finally:f.close()
