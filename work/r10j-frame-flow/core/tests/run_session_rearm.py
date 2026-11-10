#!/usr/bin/env python3
import hashlib,json,subprocess,tempfile,shutil,re
from pathlib import Path
W=Path(__file__).resolve().parents[3];C=Path(__file__).resolve().parents[1];F=W/'demo51-field-success/JOB_1793869199_415_10dc3936';Q=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';checks=[];commands=[]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def run(name,cmd,want=0):
 p=subprocess.run([str(v) for v in cmd],capture_output=True,text=True,timeout=15);assert p.returncode==want,(name,p.returncode,p.stdout,p.stderr);checks.append(dict(case=name,passed=True));commands.append(dict(case=name,exit=p.returncode,stdout=p.stdout,stderr=p.stderr))
run('HOST_30_pure_predicates',[C/'build/test-session-rearm-contract.host'])
run('actual_ARM_30_pure_predicates',[Q,C/'build/test-session-rearm-contract.arm'])
run('HOST_backend_111_exact_kernel_typed_predicates',[C/'build/test-session-rearm-backend.host',W/'demo51-field-success/JOB_1793869199_415_10dc3936/jpeg/attempt_1/hardware/p11_kernel_before.bin'])
run('ARM_backend_111_exact_kernel_typed_predicates',[Q,C/'build/test-session-rearm-backend.arm',W/'demo51-field-success/JOB_1793869199_415_10dc3936/jpeg/attempt_1/hardware/p11_kernel_before.bin'])
run('production_ARM_version',[Q,C/'bin/nxl-session-rearm.arm','--version'])
run('production_ARM_invalid_arguments_no_IO',[Q,C/'bin/nxl-session-rearm.arm'],2)
with tempfile.TemporaryDirectory(prefix='r7-metadata-',dir=C/'build') as t:
 p=Path(t)/F.name;p.mkdir();names=['front_2/front/P49_FRONT_COMPLETE','front_2/front/result.json','front_2/front/case_00/result.json','front_2/front/case_00/post-input.txt','jpeg/attempt_1/hardware/controller-outcome.txt','jpeg/attempt_1/hardware/result.json']
 for name in names:
  dst=p/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(F/name,dst)
 def rebase(raw):
  records=[]
  for record in raw.decode().split('end=NX51END1\n')[:-1]:
   b=record.split('sha256=')[0].replace('/mnt/mmc/NXDEMO51/'+F.name,str(p));records.append(b+'sha256='+hashlib.sha256(b.encode()).hexdigest()+'\nend=NX51END1\n')
  return ''.join(records).encode()
 journal=rebase((F/'STATE.journal').read_bytes());(p/'STATE.journal').write_bytes(journal)
 run('HOST_authoritative_real_journal_full_attestation',[C/'build/test-session-rearm-metadata.host',p])
 run('ARM_authoritative_real_journal_full_attestation',[Q,C/'build/test-session-rearm-metadata.arm',p])
 (p/'SESSION.state').write_text('stale or corrupt mirror is not authority\n');run('HOST_stale_canonical_ignored',[C/'build/test-session-rearm-metadata.host',p])
 (p/'STATE.journal').write_bytes(journal[:-1]);run('HOST_torn_journal_rejected',[C/'build/test-session-rearm-metadata.host',p],1)
 (p/'STATE.journal').write_bytes(journal.replace(b'state=COMPLETE\n',b'state=COMPLETE!\n'));run('HOST_checksum_corruption_rejected',[C/'build/test-session-rearm-metadata.host',p],1)
 (p/'STATE.journal').write_bytes(journal);case=p/'front_2/front/case_00/result.json';case.write_bytes(case.read_bytes()+b' ');run('HOST_case_hash_corruption_rejected',[C/'build/test-session-rearm-metadata.host',p],1)
report=dict(format='NX51_REARM_READONLY_CONTRACT_TESTS_R7',passed=True,tests_run=len(checks),pure_predicate_assertions_per_runtime=30,backend_predicate_assertions_per_runtime=111,backend_retirement_gate_enabled=True,backend_resources_retired_semantics="Fresh 16-sample independent RO retirement plus both free ranges authorizes fresh initialization; does not claim prior quantizer SRAM contents reset",actual_ARM_execution=True,hardware_tested=False,native_calls=0,CAP_releases=0,source_sha256={str(n.relative_to(C)):sha(n) for n in [C/'src/nxl-session-rearm.c',C/'src/session_rearm_contract.h',C/'src/session_rearm_backend.h',C/'src/oem_ro_read.h',C/'tests/test_session_rearm_backend.c',C/'tests/run_session_rearm_collector.py',C/'tests/test_session_rearm_contract.c',C/'tests/test_session_rearm_metadata.c',Path(__file__).resolve()]},binary_sha256=sha(C/'bin/nxl-session-rearm.arm'),binary_bytes=(C/'bin/nxl-session-rearm.arm').stat().st_size,cases=checks,commands=commands,policy='RECEIPT_ONLY_AFTER_INDEPENDENT_RETIREMENT: exact backend guards/layout/clock/engine/IRQ/owner/16 stable samples, both old allocation ranges allfree, known roots clear and parked shell; original UNKNOWN alone is not assumed resolved',limits=['Allocator/root reads on camera not executed here.','Real archive metadata path was projected in a temporary copy and only path/SHA bytes changed.','Original source report files were read only.'])
collector=json.loads((C/'results/session-rearm-collector-tests.json').read_text());assert collector['passed'] and collector['production_source_sha256']==sha(C/'src/nxl-session-rearm.c');report['collector_tests_run']=collector['tests_run'];report['collector_tests_sha256']=sha(C/'results/session-rearm-collector-tests.json');report['production_positive_receipt_sha256']=sha(C/'results/session-rearm-positive-fixture.proof');report['source_sha256'].update({str(p.relative_to(C.parent)):sha(p) for p in [C.parent/'hardware/src/ep_sync_core.h',C.parent/'hardware/src/ep_kernel_guards.h',C.parent/'hardware/src/jpeg_gate_diag.inc']});report['executed_test_binaries_sha256']={str(p.relative_to(C)):sha(p) for p in (C/'build').glob('test-session-rearm-*') if p.is_file()};
(C/'results/session-rearm-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,tests_run=len(checks),binary_sha256=report['binary_sha256'],binary_bytes=report['binary_bytes'])))
