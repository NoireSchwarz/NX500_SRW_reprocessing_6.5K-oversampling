#!/usr/bin/env python3
"""Run actual menu launch functions against temporary shell workers and SD alias.

Only fixed paths and UI calls are modeled. No X, camera, or native ISP calls.
"""
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
R=Path(__file__).resolve().parents[1];P=R.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    menu=(R/'src/menu.c').read_text()
    helpers=menu[menu.index('static FILE *launch_log_open'):menu.index('static void load_names')]
    background=menu[menu.index('static int memory_background'):menu.index('static int inspect')]
    prefix='''#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "HEADER"
static char engine[1100],demo_look[81]="raw422";
static int apply_strength=100;
static int srw_fstat(int fd,struct stat *st){return fstat(fd,st);}
static int nxl_lock(const char *p){int fd=open(p,O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0)return -1;if(flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;}
static void toast_timed(const char *s,int ok,unsigned long ms){(void)s;(void)ok;(void)ms;}
'''.replace('HEADER',str((R/'src/ui_state.h').resolve()))
    suffix='''int main(int argc,char **argv){if(argc!=3)return 2;setenv("NX_LUT_ROOT",argv[1],1);snprintf(engine,sizeof engine,"%s/lut.sh",argv[1]);int rc=memory_background(-1,atoi(argv[2]));if(rc)return rc;int status;while(waitpid(-1,&status,0)>0){}return 0;}
'''
    results=[]
    with tempfile.TemporaryDirectory(prefix='nx51-menu-launch-') as temporary:
        w=Path(temporary);sd=w/'real-sd';sd.mkdir();alias=w/'mnt-mmc';alias.symlink_to(sd,target_is_directory=True);logs=sd/'NXDEMO51/logs';logs.mkdir(parents=True)
        runtime=w/'runtime';runtime.mkdir();(runtime/'bin').mkdir();hold=runtime/'memory-ui.lock'
        source=(prefix+helpers+background+suffix).replace('/mnt/mmc/',str(alias)+'/').replace('/tmp/nx-lut-lite/',str(runtime)+'/')
        def build(name,text):
            c=w/(name+'.c');exe=w/name;c.write_text(text)
            subprocess.run(['cc','-O0','-Wno-unused-function',str(c),'-o',str(exe)],check=True)
            return exe
        exe=build('launch',source)
        def reset():
            for p in logs.glob('*.log'):p.unlink()
        def run(binary=exe):
            p=subprocess.run([str(binary),str(runtime),'0'],capture_output=True,text=True,timeout=15)
            assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
            return (logs/'launch.log').read_text(),(logs/'run.log').read_text()
        script=runtime/'memory-test.sh'
        script.write_text('#!/bin/sh\nprintf "WORKER_STDOUT\\n"\nprintf "WORKER_STDERR\\n" >&2\nexit 3\n')
        launch,runlog=run()
        for log in [launch,runlog]:
            assert 'event=REQUESTED' in log and 'script='+str(runtime)+'/memory-test.sh' in log
            assert 'WORKER_STDOUT' in log and 'WORKER_STDERR' in log and 'event=BACKEND_EXIT value=3' in log
            assert log.index('event=REQUESTED')<log.index('event=UI_LOCK_ACQUIRED')<log.index('event=EXEC_ATTEMPT')
        assert not list((sd/'NXDEMO51').glob('JOB_*')) and not (sd/'NXDEMO51/current.job').exists()
        results.append(dict(case='SD_alias_logs_before_job_and_captures_worker_exit3',passed=True))
        assert (runtime/'memory-action.log').read_text()=='WORKER_STDOUT\nWORKER_STDERR\n'
        results.append(dict(case='legacy_tmp_output_kept',passed=True))
        reset();script.unlink();launch,runlog=run()
        assert 'event=BACKEND_EXIT value=2' in launch and 'memory-test.sh' in launch and ('cannot open' in launch or 'No such file' in launch)
        results.append(dict(case='missing_script_shell_error_saved_to_SD',passed=True))
        reset();script.write_text('#!/bin/sh\nkill -TERM $$\n');launch,runlog=run()
        assert 'event=BACKEND_SIGNAL value=15' in launch
        results.append(dict(case='real_worker_signal_recorded',passed=True))
        reset()
        with hold.open('w') as f:
            import fcntl
            fcntl.flock(f.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
            launch,runlog=run()
        assert 'event=REQUESTED' in launch and 'event=UI_LOCK_BUSY_OR_FAILED' in launch and 'event=EXEC_ATTEMPT' not in launch
        results.append(dict(case='busy_UI_lock_logged_before_worker_start',passed=True))
        reset();bad_shell=build('bad-shell',source.replace('execl("/bin/sh"','execl("'+str(w/'absent-shell')+'"'))
        launch,runlog=run(bad_shell)
        assert 'event=EXEC_FAILED value=127 errno=2' in launch and 'MENU_EXEC_ERROR' in launch and 'event=BACKEND_EXIT value=127' in launch
        results.append(dict(case='exec_errno_and_exit127_preserved',passed=True))
        reset();fork_model=source.replace('static char engine','static pid_t failed_fork(void){errno=EAGAIN;return -1;}\n#define fork failed_fork\nstatic char engine',1)
        failed=build('failed-fork',fork_model)
        p=subprocess.run([str(failed),str(runtime),'0'],capture_output=True,text=True,timeout=15)
        assert p.returncode==1
        launch=(logs/'launch.log').read_text()
        assert 'event=REQUESTED' in launch and 'event=BACKGROUND_FORK_FAILED' in launch and 'errno=11' in launch
        results.append(dict(case='fork_failure_still_has_prior_SD_request_record',passed=True))
    assert 'STOPPED - CHECK NXDEMO51/LOGS' in menu and 'DEMO51 STOPPED - CHECK SD NXDEMO51' not in menu
    results.append(dict(case='failure_toast_points_to_existing_root_launch_log',passed=True))
    data=dict(passed=True,tests_run=len(results),actual_C_launch_functions=True,hardware_tested=False,X11_tested=False,
        path_model='Only fixed paths changed to temporary SD symlink; host fstat stands in for existing ARM __fxstat3 wrapper.',
        source_sha256={str((R/'src/menu.c').relative_to(P)):sha(R/'src/menu.c')},cases=results)
    v=P/'validation';v.mkdir(exist_ok=True);(v/'menu-launch-tests.json').write_text(json.dumps(data,indent=2)+'\n')
    (v/'menu-launch-tests.txt').write_text('PASS '+str(len(results))+' actual C launch and SD logging cases; no camera/UI/hardware\n')
    print(json.dumps(dict(passed=True,tests_run=len(results))))
if __name__=='__main__':main()
