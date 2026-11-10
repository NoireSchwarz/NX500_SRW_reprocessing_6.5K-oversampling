#!/usr/bin/env python3
"""Run actual auth/manual-lock functions against real flock contention.

Only fixed filesystem paths and /proc/stat observations are redirected into
a private fixture. No native API, ptrace, hardware or image task is executed.
"""
from pathlib import Path
import fcntl, hashlib, json, os, subprocess, tempfile

R = Path(__file__).resolve().parents[1]
SOURCE = R / 'src/nxl-oem-restore.c'
TOKEN = '0123456789abcdef0123456789abcdef'
JOB = 'JOB_review'


def main():
    original = SOURCE.read_text()
    cases = []
    with tempfile.TemporaryDirectory(prefix='oem-auth-review-') as td:
        base = Path(td)
        root, run, proc = base / 'NXDEMO51', base / 'run', base / 'proc'
        root.mkdir(); run.mkdir(); proc.mkdir()
        job = root / JOB; job.mkdir()
        (root / 'recovery').mkdir()
        lock = (root / 'run.lock').open('w+')
        model = original.replace('#define OEM_ROOT "/mnt/mmc/NXDEMO51"',
                                 '#define OEM_ROOT ' + json.dumps(str(root)))
        model = model.replace('#define OEM_RUN "/tmp/nx-lut-lite"',
                              '#define OEM_RUN ' + json.dumps(str(run)))
        model = model.replace('#include "../../common/session_lease_paths.h"', '#define NX51_LEASE_PATH ' + json.dumps(str(run / 'NX51.active.lease')))
        assert model.count('"/proc/%d/stat"') == 1
        model = model.replace('"/proc/%d/stat"', json.dumps(str(proc / '%d/stat')))
        model = '#define main original_helper_main\n' + model + r'''
#undef main
int main(int argc,char **argv){
 if(argc!=3)return 90;directory=argv[2];
 if(!strcmp(argv[1],"supervised")){
  int pp=0,gp=0;uint64_t pt=0,gt=0;
  int a=process_identity(getppid(),&pp,&pt),b=process_identity(pp,&gp,&gt);
  fprintf(stderr,"auth model parent=%d pp=%d ticks=%llu/%llu proc=%d/%d paths=%d/%d owners=%d/%d\n",getppid(),pp,(unsigned long long)pt,(unsigned long long)gt,a,b,no_links(getenv("NX51_SESSION_ROOT")),no_links(getenv("NX51_JOB")),lock_owner_matches("engine.lock",getppid(),getenv("NX51_JOB")),lock_owner_matches("trace.lock",getppid(),getenv("NX51_JOB")));
  return authenticate_supervised()?0:1;
 }
 int ok=take_manual_locks();
 finish_manual_locks(!strcmp(argv[1],"manual-unknown"));
 if(manual_run_fd>=0)close(manual_run_fd);
 return ok?0:1;
}
'''
        c, exe = base / 'model.c', base / 'model'
        c.write_text(model)
        build = subprocess.run(['cc', '-O2', '-Wno-unused-function', '-Wno-misleading-indentation',
                                '-I', str((R / 'src').resolve()), str(c), '-o', str(exe)],
                               capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stderr)
        runner_pid = os.getpid()
        supervisor_pid = 9001

        def stat(pid, parent, ticks):
            p = proc / str(pid); p.mkdir(exist_ok=True)
            fields = ['S', str(parent)] + ['0'] * 17 + [str(ticks)]
            (p / 'stat').write_text(str(pid) + ' (test worker) ' + ' '.join(fields) + '\n')

        stat(runner_pid, supervisor_pid, 111)
        stat(supervisor_pid, 1, 222)
        lease = run / 'NX51.active.lease'
        lease.write_text('pid=9001\nstart_ticks=222\ntoken=' + TOKEN + '\nmode=normal\nend=NX51LEASE2\n')
        valid_lease = lease.read_bytes()
        env = dict(os.environ, NX51_SESSION_ROOT=str(root), NX51_SESSION_TOKEN=TOKEN,
                   NX51_JOB=str(job))
        report = job / 'restore-camera-front-2'

        def owner(name, pid=runner_pid):
            d = run / name; d.mkdir(exist_ok=True)
            (d / 'pid').write_text(str(pid) + '\n')
            (d / 'NX51_OWNER').write_text('NX51LOCK1\njob=' + str(job) + '\nboot=known-boot\n')

        def clear(name):
            d = run / name
            if d.exists():
                for p in d.iterdir(): p.unlink()
                d.rmdir()

        def call(mode='supervised', path=report, environment=None):
            v = subprocess.run([str(exe), mode, str(path)], env=environment or env,
                               capture_output=True, text=True, timeout=5)
            if not cases and v.returncode:
                raise AssertionError(v.stderr)
            return v.returncode

        owner('engine.lock'); owner('trace.lock')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        assert call() == 0
        cases.append('actual_flock_plus_exact_parent_epoch_fixture_and_owned_locks_authorizes')
        bad = dict(env, NX51_SESSION_TOKEN='f' * 32)
        assert call(environment=bad) != 0
        cases.append('parent_lock_without_matching_token_cannot_authorize')
        lease.write_bytes(valid_lease.replace(b'start_ticks=222', b'start_ticks=223'))
        assert call() != 0
        cases.append('reused_or_wrong_supervisor_epoch_refused')
        lease.write_bytes(valid_lease.replace(b'mode=normal', b'mode=full'))
        assert call() != 0
        cases.append('full_prepare_lease_cannot_start_liveview_recovery')
        lease.write_bytes(valid_lease)
        owner('trace.lock', runner_pid + 1)
        assert call() != 0
        cases.append('foreign_trace_owner_refused')
        owner('trace.lock')
        assert call(path=job / 'restore-camera-front-02') != 0
        assert call(path=job / 'unrelated-report') != 0
        cases.append('borrowed_lease_bound_to_exact_report_namespace')
        assert call(mode='manual', path=root / 'recovery/RESTORE_busy') != 0
        cases.append('manual_cannot_bypass_real_parent_flock_with_valid_environment')
        fcntl.flock(lock, fcntl.LOCK_UN)
        assert call() != 0
        cases.append('stale_lease_without_held_parent_flock_refused')
        clear('engine.lock'); clear('trace.lock')

        assert call(mode='manual', path=root / 'recovery/RESTORE_clean') == 0
        assert not (run / 'engine.lock').exists() and not (run / 'trace.lock').exists()
        cases.append('manual_takes_and_releases_only_its_own_workflow_locks')
        (run / 'trace.lock').mkdir(); (run / 'trace.lock/FOREIGN').write_text('unchanged\n')
        assert call(mode='manual', path=root / 'recovery/RESTORE_foreign') != 0
        assert not (run / 'engine.lock').exists()
        assert (run / 'trace.lock/FOREIGN').read_text() == 'unchanged\n'
        cases.append('manual_partial_acquisition_rolls_back_own_engine_preserves_foreign_trace')
        clear('trace.lock')
        assert call(mode='manual-unknown', path=root / 'recovery/RESTORE_unknown') == 0
        for name in ['engine.lock', 'trace.lock']:
            d = run / name
            assert (d / 'RESTART_REQUIRED').is_file() and (d / 'NXOEM_OWNER').is_file()
            assert not (d / 'pid').exists()
        cases.append('manual_unknown_keeps_restart_holds_and_does_not_leave_live_pid_claim')
        lock.close()

    report = dict(format='NX51_INDEPENDENT_OEM_AUTH1', passed=True,
                  tests_run=len(cases), cases=cases, actual_source_functions=True,
                  actual_POSIX_flock=True, hardware_tested=False,
                  source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  fixed_paths_redirected=True,
                  limits=['Process identity reads use private /proc/stat observations; no camera/native API is executed.'])
    (R / 'results/independent-oem-auth.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
