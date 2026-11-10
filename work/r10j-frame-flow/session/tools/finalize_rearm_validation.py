#!/usr/bin/env python3
"""Bind current R7 session and resource probe evidence; refuses stale component pins."""
from pathlib import Path
import hashlib,json,subprocess,re
P=Path(__file__).resolve().parents[2];W=P.parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
components={'session_host':'session/validation/session-tests.json','session_rearm_host':'session/validation/rearm-session-tests.json','session_arm_bootstrap':'session/validation/arm-bootstrap-r4.json','session_arm_rearm':'session/validation/arm-rearm-tests.json','probe_contract_metadata':'core/results/session-rearm-tests.json','probe_collector':'core/results/session-rearm-collector-tests.json','producer_consumer_receipt':'session/validation/probe-receipt-crossparse.json'}
records={}
for label,rel in components.items():
 d=json.loads((P/rel).read_text());assert d['passed'],(label,'not passed')
 base=P/('core' if label.startswith('probe') else 'session')
 hashes=d.get('source_files',d.get('source_sha256',{}))
 if isinstance(hashes,str):hashes={'src/session.c':hashes}
 for name,value in hashes.items():
  source=base/name
  if not source.is_file():source=P/name
  assert source.is_file() and sha(source)==value,(label,'stale source',name)
 records[label]=dict(path=rel,sha256=sha(P/rel),tests_run=d.get('tests_run'),hardware_tested=d.get('hardware_tested'),actual_ARM_execution=d.get('actual_ARM_execution',False))
session=P/'session/bin/nxl-session.arm';probe=P/'core/bin/nxl-session-rearm.arm';pd=json.loads((P/components['probe_contract_metadata']).read_text());assert pd.get('backend_retirement_gate_enabled') is True,'Old failure-only probe evidence cannot authorize the R7 joint package';assert sha(probe)==pd['binary_sha256']
q=W/'demo-p49/tooling/qemu/usr/bin/qemu-arm-static';versions={}
for label,b in [('session',session),('probe',probe)]:
 r=subprocess.run([str(q),str(b),'--version'],capture_output=True,text=True,check=True,timeout=10);assert 'RESOURCE_REARM_R7' in r.stdout;versions[label]=r.stdout
sources=set()
def collect(f):
 f=f.resolve()
 if f in sources:return
 sources.add(f)
 for rel in re.findall(r'^#include "([^"]+)"',f.read_text(),re.M):
  child=(f.parent/rel).resolve()
  if child.is_file():collect(child)
for f in [P/'session/src/session.c',P/'core/src/nxl-session-rearm.c']:collect(f)
for name in ['session/tests/test_rearm_session.py','session/tests/test_arm_rearm.py','session/tests/test_arm_bootstrap.py','session/tests/test_probe_receipt.py']:sources.add(P/name)
limitations=['No camera resource reclamation or repeat RAW task has been verified by these PC/ARM tests.','Positive session receipts model a trusted probe that independently proved all required native resource predicates; no old COMPLETE status alone is trusted.','A historical UNKNOWN may only be superseded by new proof of both allocation spans and active EP/JPEG resource retirement; this grants fresh initialization, not restoration of old quantizer SRAM or pixels.','Unavailable, clock-gated, unstable or in-use native resources must stay held with a specific diagnostic reason.','No old YUV reuse, CAP release, generic unlock, hardware write or unknown lock deletion is authorized by this path.']
d=dict(format='NX500_RECOVERY_R7_REARM_TESTS1',passed=True,hardware_tested=False,source_sha256={str(f.relative_to(P)):sha(f) for f in sorted(sources)},session_binary_sha256=sha(session),session_binary_bytes=session.stat().st_size,probe_binary_sha256=sha(probe),probe_binary_bytes=probe.stat().st_size,versions=versions,component_evidence=records,component_evidence_sha256={rel:sha(P/rel) for rel in components.values()},binaries={'session':{'path':'session/bin/nxl-session.arm','sha256':sha(session),'bytes':session.stat().st_size},'probe':{'path':'core/bin/nxl-session-rearm.arm','sha256':sha(probe),'bytes':probe.stat().st_size}},backend_retirement_gate_enabled=True,session_scope=dict(current_canonical_seq=1,empty_stage='idle',old_fronts_reused=0,front_attempt=0,backend_attempt=0,accepted_transition='Same job seq2 NEED_STILL_INIT only after live rearm lease + fresh exact receipt',dedicated_mode='rearm',mutators=['rearm-fresh','fail'],pinned_worker='/opt/usr/nx-lut-lite/rearm-session.sh'),receipt_mandatory_predicates=['readonly=1','native_calls=0','resource_snapshots=3','old_range_free=1','backend_range_free=1','backend_resources_retired=1','native_outcome_resolved=1','known_roots_clear=1','code_original=1','shell_parked=1','queue_empty=1','workers_idle=1'],limitations=limitations)
(P/'validation/recovery-r7-rearm-tests.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps(dict(passed=True,session_binary_sha256=d['session_binary_sha256'],probe_binary_sha256=d['probe_binary_sha256'],aggregate_sha256=sha(P/'validation/recovery-r7-rearm-tests.json')),indent=2))
