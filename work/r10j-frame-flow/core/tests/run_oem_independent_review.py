#!/usr/bin/env python3
"""Bind independent R5 boundary tests and current ARM smoke to exact sources."""
from pathlib import Path
import hashlib, json, re, subprocess, sys, tempfile

R = Path(__file__).resolve().parents[1]
W = R.parents[1]
RESULTS = R / 'results'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    RESULTS.mkdir(exist_ok=True)
    source = R / 'src/nxl-oem-restore.c'
    header = R / 'src/oem_restore_contract.h'
    source_before = sha(source)
    header_before = sha(header)
    guard_before = sha(R / 'src/oem_restore_guards.h')
    test_sources = [R / 'tests' / n for n in [
        'test_original_ui_wait_race.py', 'test_oem_restore_contract.c',
        'test_oem_restore_cleanup.py', 'test_oem_restore_auth.py',
        'run_oem_independent_review.py']]
    for name in ['test_original_ui_wait_race.py', 'test_oem_restore_cleanup.py',
                 'test_oem_restore_auth.py']:
        p = subprocess.run([sys.executable, str(R / 'tests' / name)],
                           check=True, capture_output=True, text=True)
        print(p.stdout.strip())

    fixture = W / 'demo51-field-success/JOB_1793869199_415_10dc3936/front_2/front/case_00/p48_idle_handoff.bin'
    contract_test = R / 'tests/test_oem_restore_contract.c'
    with tempfile.TemporaryDirectory(prefix='oem-contract-final-') as td:
        exe = Path(td) / 'contract'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(contract_test), '-o', str(exe)], check=True)
        result = subprocess.run([str(exe), str(fixture)], check=True,
                                capture_output=True, text=True)
    checks = int(re.search(r'PASS (\d+)', result.stdout)[1])
    contract = dict(format='NX51_INDEPENDENT_OEM_CONTRACT_TEST1', passed=True,
                    checks=checks, hardware_tested=False, native_commands_submitted=0,
                    source_sha256=sha(contract_test), contract_sha256=sha(header),
                    actual_field_fixture=str(fixture.relative_to(W)),
                    actual_field_fixture_sha256=sha(fixture), stdout=result.stdout,
                    limits=['Contract scenarios do not execute native APIs or ptrace.'])
    (RESULTS / 'independent-oem-contract.json').write_text(json.dumps(contract, indent=2) + '\n')

    binary = R / 'bin/nxl-oem-restore.arm'
    qemu = W / 'demo-p49/tooling/qemu/usr/bin/qemu-arm-static'
    arm = []
    for argument, expected in [('--version', 'OEM_LIVEVIEW_RESTORE_R5'),
                               ('--abi', 'handle,27,0,0,0,0')]:
        p = subprocess.run([str(qemu), str(binary), argument],
                           check=True, capture_output=True, text=True, timeout=10)
        assert expected in p.stdout
        arm.append(dict(argument=argument, exit=p.returncode, stdout=p.stdout.strip()))
    smoke = dict(format='NX51_INDEPENDENT_OEM_ARM_SMOKE1', passed=True,
                 actual_ARM_execution=True, binary_sha256=sha(binary),
                 cases=arm, hardware_tested=False, native_commands_submitted=0,
                 limits=['Only version and ABI branches execute; no runtime camera success claim.'])
    (RESULTS / 'independent-oem-arm-smoke.json').write_text(json.dumps(smoke, indent=2) + '\n')
    assert (source_before, header_before, guard_before) == (
        sha(source), sha(header), sha(R / 'src/oem_restore_guards.h')), 'Runtime changed during independent review'

    names = ['original-ui-wait-race.json', 'independent-oem-contract.json',
             'independent-oem-cleanup.json', 'independent-oem-auth.json',
             'independent-oem-arm-smoke.json']
    reports = {name: json.loads((RESULTS / name).read_text()) for name in names}
    assert all(v['passed'] and not v['hardware_tested'] for v in reports.values())
    assert reports['independent-oem-cleanup.json']['helper_source_sha256'] == source_before
    assert reports['independent-oem-auth.json']['source_sha256'] == source_before
    review = dict(format='NX51_INDEPENDENT_OEM_REVIEW_R5_1', passed=True,
                  hardware_tested=False, hardware_recovery_proven=False,
                  sources={str(p.relative_to(R)): sha(p) for p in [source, header,
                           R / 'src/oem_restore_guards.h', R / 'src/native_allocator.inc']},
                  test_sources={str(p.relative_to(R)): sha(p) for p in test_sources},
                  current_ARM=dict(path=str(binary.relative_to(R)), sha256=sha(binary),
                                   bytes=binary.stat().st_size),
                  reports={name: sha(RESULTS / name) for name in names},
                  counts=dict(contract=checks, actual_original_ARM_handshake=4,
                              actual_stack_fragment=6, actual_auth_and_flock=11,
                              actual_ARM_version_ABI=2),
                  review_findings_closed=[
                      'Successful field handoff with nonzero retained config is accepted; pending/active words still refuse.',
                      'Stack restore is idempotent and cannot write a detached running shell stack.',
                      'Exact engine.lock/trace.lock names are accepted by no-links checks.',
                      'Fresh stop/reverse/duplicate completion evidence cannot certify the start transaction.',
                      'Unbounded OEM UI Wait is not used; original ARM replay demonstrates its lost-signal and shared-waiter hazards.'],
                  runtime_review=[
                      'Supervised authentication binds parent epoch/token and owned engine/trace markers while run.lock is held.',
                      'Manual recovery uses real run.lock contention and preserves foreign or unknown holds.',
                      'Unknown inflight path group-stops and detaches with SIGSTOP without writing stack or registers; no safe-return claim is made.',
                      'Native submit is single-shot and completion is observed only after safe shell restoration/detach.'],
                  verification_scopes=[
                      'Host contract and extracted stack fragment with modeled memory I/O.',
                      'Actual auth/manual functions and real POSIX flock, with private process-identity fixtures.',
                      'Original OEM ARM Wait/Complete under Unicorn with modeled diagnostics/POSIX semaphore.',
                      'Current production ARM binary --version/--abi under QEMU only.'],
                  limits=[
                      'No on-camera execution of R5 recovery is established by these tests.',
                      'Full live ptrace scheduling, firmware callback delivery and return to physical preview still require the planned camera test.',
                      'External observation cannot exclude an unobservable concurrent UI transaction between all samples.'])
    (RESULTS / 'independent-oem-review.json').write_text(json.dumps(review, indent=2) + '\n')
    print(json.dumps(dict(passed=True, counts=review['counts'],
                          evidence=str(RESULTS / 'independent-oem-review.json'),
                          runtime_sha256=source_before, ARM_sha256=sha(binary))))


if __name__ == '__main__':
    main()
