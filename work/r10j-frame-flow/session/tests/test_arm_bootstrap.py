#!/usr/bin/env python3
"""Actual ARM filesystem/bootstrap tests; no camera devices or native jobs.

Production status runs unchanged. A separate non-HOST_MODEL binary changes only
three fixed path constants to reproduce the OEM SD alias inside this workspace.
This is needed because the sandbox denies chroot. It is never deployed.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,tempfile

R=Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--qemu',type=Path,required=True);p.add_argument('--zig',type=Path,required=True);p.add_argument('--cross-runtime',type=Path,required=True);a=p.parse_args()
qemu=a.qemu.resolve();production=R/'bin/nxl-session.arm';cases=[]
def check(name,ok):
    if not ok:raise AssertionError(name)
    cases.append(dict(case=name,passed=True))
def call(exe,*args):return subprocess.run([str(qemu),str(exe),*args],capture_output=True,text=True,timeout=10)
v=call(production,'--version');check('actual_production_ARM_version_marker',v.returncode==0 and 'OEM_SD_ALIAS_BOOT_OPTIONAL_R2' in v.stdout and 'RESOURCE_REARM_R7' in v.stdout)
v=call(production,'status');check('actual_production_status_without_proc_boot_id',v.returncode==0 and 'state=NEED_STILL_INIT\n' in v.stdout and 'current_boot=UNKNOWN\n' in v.stdout and 'UNAVAILABLE_NOT_REQUIRED' in v.stderr)
with tempfile.TemporaryDirectory(prefix='arm-oem-fs-',dir=R/'build') as name:
    fs=Path(name);alias=fs/'mnt/mmc';target=fs/'opt/storage/sdcard';alias.parent.mkdir();target.mkdir(parents=True);alias.symlink_to(target,target_is_directory=True);root=alias/'NXDEMO51'
    wrapper=R/'build/filesystem-model.c';model=R/'build/session-filesystem-model.arm'
    wrapper.write_text('#define DEFAULT_ROOT '+json.dumps(str(root))+'\n#define OEM_SD_ALIAS '+json.dumps(str(alias))+'\n#define OEM_SD_TARGET '+json.dumps(str(target))+'\n#include '+json.dumps(str(R/'src/session.c'))+'\n')
    subprocess.run(['python3',str(R/'tools/build_session.py'),'--zig',str(a.zig),'--cross-runtime',str(a.cross_runtime),'--source',str(wrapper),'--output',str(model)],check=True,capture_output=True,text=True)
    v=call(model,'status');check('ARM_OEM_alias_status_readonly_before_root',v.returncode==0 and not root.exists() and 'state=NEED_STILL_INIT\n' in v.stdout)
    # Existing session source reads the actual urandom device. No entropy/PID
    # mocks or HOST_MODEL branches are enabled in this separate ARM executable.
    v=call(model,'new');check('ARM_OEM_alias_creates_physical_SD_root',v.returncode==0 and (target/'NXDEMO51/current.job').is_file())
    values=dict(line.split('=',1) for line in v.stdout.splitlines() if '=' in line)
    job=Path(values['job']);check('ARM_logical_alias_preserved_in_committed_job',str(job).startswith(str(alias)+'/NXDEMO51/JOB_') and (job/'SESSION.state').is_file() and values['current_boot']=='UNKNOWN')
    v=call(model,'status');check('ARM_committed_state_reloads_through_OEM_alias',v.returncode==0 and 'state=NEED_STILL_INIT\n' in v.stdout and 'kernel_boot_changed=0\n' in v.stdout)
    (target/'NXDEMO51/logs').mkdir();v=call(model,'status');log=(target/'NXDEMO51/logs/session.log').read_text();check('ARM_dedicated_session_log_only',v.returncode==0 and 'phase=ENTRY' in log and not (target/'NX_LUT_DEMO51_SESSION.log').exists())
    state=job/'STATE.journal';original=state.read_bytes();copy=fs/'state-copy';state.rename(copy);state.symlink_to(copy)
    v=call(model,'status');check('ARM_descendant_state_symlink_still_rejected',v.returncode!=0 and 'INVALID_SESSION_RECORD' in v.stderr and copy.read_bytes()==original)
    state.unlink();copy.rename(state)
    alias.unlink();other=fs/'wrong-target';other.mkdir();alias.symlink_to(other,target_is_directory=True)
    v=call(model,'new');check('ARM_non_OEM_alias_target_still_rejected',v.returncode!=0 and 'phase=ROOT_PARENTS result=FAILED' in v.stderr and not (other/'NXDEMO51').exists())
    model_hash=sha(model);wrapper_hash=sha(wrapper)
record=dict(format='NX51_ARM_BOOTSTRAP_R4',passed=True,tests_run=len(cases),cases=cases,hardware_tested=False,actual_ARM_execution=True,HOST_MODEL=False,source_sha256=sha(R/'src/session.c'),source_files={str(p.relative_to(R)):sha(p) for p in [R/'src/session.c',R/'src/journal.inc',R/'src/rearm.inc']},production_binary_sha256=sha(production),filesystem_model_binary_sha256=model_hash,filesystem_wrapper_sha256=wrapper_hash,limits=['Production binary status tested unchanged with missing proc boot ID.','OEM alias creation tests use the same non-HOST_MODEL source with only three fixed path constants redirected to a private workspace.','Sandbox denies chroot; no claim of camera execution or actual OEM mount semantics.'])
(R/'validation/arm-bootstrap-r4.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(dict(passed=True,tests_run=len(cases),hardware_tested=False,actual_ARM_execution=True)))
