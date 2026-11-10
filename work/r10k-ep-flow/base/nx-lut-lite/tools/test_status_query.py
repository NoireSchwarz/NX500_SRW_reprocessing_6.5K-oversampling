#!/usr/bin/env python3
"""Run real menu query/new-session C functions with stdout and stderr separate.

Uses the actual HOST_MODEL session manager and original ARM output fixtures;
only fixed paths, UI calls, and host fstat/timing adapters are modeled.
"""
from pathlib import Path
import hashlib,json,os,shlex,subprocess,tempfile,time
R=Path(__file__).resolve().parents[1];P=R.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    menu=(R/'src/menu.c').read_text()
    helpers=menu[menu.index('static FILE *launch_log_open'):menu.index('static void load_names')]
    query=menu[menu.index('static void load_raw_status'):menu.index('/* Bounded, no-follow regular-file read')]
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
#include <time.h>
#include <poll.h>
#include "HEADER"
static struct nxl_raw_status raw_status;
static volatile sig_atomic_t quitting;
static char engine[1100],demo_look[81]="raw422";static int apply_strength=100;
static int srw_fstat(int fd,struct stat *st){return fstat(fd,st);}
static unsigned long ticks(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL;}
static int nxl_lock(const char *p){int fd=open(p,O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0)return -1;if(flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;}
static void toast_timed(const char *s,int ok,unsigned long ms){(void)ok;(void)ms;FILE *f=fopen(getenv("NX51_TEST_TOAST"),"a");if(f){fprintf(f,"%s\\n",s);fclose(f);}}
'''.replace('HEADER',str((R/'src/ui_state.h').resolve()))
    suffix='''int main(int argc,char **argv){if(argc!=3)return 2;setenv("NX_LUT_ROOT",argv[1],1);snprintf(engine,sizeof engine,"%s/lut.sh",argv[1]);if(!strcmp(argv[2],"query")){load_raw_status();printf("valid=%d\\nstate=%d\\n",raw_status.valid,raw_status.state);return 0;}int rc=memory_background(-1,1);if(rc)return rc;int status;while(waitpid(-1,&status,0)>0){}return 0;}
'''
    original_stdout=(P/'validation/status-stdout.txt').read_text()
    original_stderr=(P/'validation/status-stderr.txt').read_text()
    assert original_stdout.startswith('NX51STATUS1\n') and 'phase=ENTRY' in original_stderr and 'BOOT_OBSERVATION' in original_stderr
    cases=[]
    with tempfile.TemporaryDirectory(prefix='nx51-query-r3-') as tmp:
        w=Path(tmp);sd=w/'sdcard';sd.mkdir();alias=w/'mmc';alias.symlink_to(sd,target_is_directory=True);logs=sd/'NXDEMO51/logs';logs.mkdir(parents=True)
        runtime=w/'runtime';(runtime/'bin').mkdir(parents=True);manager=runtime/'bin/nxl-session.arm';toast=w/'toast';store=sd/'NXDEMO51'
        source=(prefix+helpers+query+background+suffix).replace('/mnt/mmc/',str(alias)+'/').replace('/tmp/nx-lut-lite/',str(runtime)+'/')
        (w/'query.c').write_text(source)
        subprocess.run(['cc','-O0','-Wno-unused-function',str(w/'query.c'),'-o',str(w/'query')],check=True)
        subprocess.run(['cc','-O2','-DHOST_MODEL',str(P/'session/src/session.c'),'-o',str(w/'session.host')],check=True)
        env=os.environ.copy();env.update(NX51_TEST_TOAST=str(toast),NX51_TEST_SD_ALIAS=str(alias),NX51_TEST_SD_TARGET=str(sd),NX51_TEST_BOOT_ID='11111111-1111-1111-1111-111111111111')
        def script(body):manager.write_text('#!/bin/sh\n'+body+'\n');manager.chmod(0o755)
        actual='case "$1" in /*) shift;; esac\nexec '+shlex.quote(str(w/'session.host'))+' '+shlex.quote(str(store))+' "$@"'
        def reset():
            for p in logs.glob('*.log'):p.unlink()
            if toast.exists():toast.unlink()
        def run(valid,event=None):
            started=time.monotonic();p=subprocess.run([str(w/'query'),str(runtime),'query'],env=env,text=True,capture_output=True,timeout=5)
            elapsed=time.monotonic()-started
            assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
            assert ('valid=1\n' in p.stdout)==valid,(p.stdout,p.stderr)
            log=(logs/'launch.log').read_text() if (logs/'launch.log').exists() else ''
            if event:assert 'event='+event in log,(event,log)
            return log,elapsed
        script(actual);log,_=run(True)
        assert 'NX51SESSION' in log and 'phase=ENTRY' in log and 'STATUS_PROTOCOL_INVALID' not in log
        cases.append(dict(case='real_HOST_session_diagnostics_do_not_pollute_valid_stdout',passed=True))
        reset();(w/'stdout').write_text(original_stdout);(w/'stderr').write_text(original_stderr)
        fixture='cat '+shlex.quote(str(w/'stderr'))+' >&2\ncat '+shlex.quote(str(w/'stdout'))
        script(fixture);log,_=run(True)
        assert original_stderr in log
        cases.append(dict(case='actual_ARM_366byte_stdout_and_220byte_stderr_isolated',passed=True))
        reset();script('printf "UNEXPECTED_STDOUT\\n"\n'+fixture);run(False,'STATUS_PROTOCOL_INVALID')
        cases.append(dict(case='arbitrary_stdout_noise_still_rejected',passed=True))
        reset();script(fixture+'\nexit 7');log,_=run(False,'STATUS_QUERY_EXIT');assert 'value=7' in log
        cases.append(dict(case='valid_record_with_nonzero_exit7_rejected',passed=True))
        reset();script('exec python3 -c '+shlex.quote('import time;time.sleep(2)'));log,elapsed=run(False,'STATUS_QUERY_TIMEOUT');assert elapsed<1.5
        cases.append(dict(case='500ms_timeout_remains_bounded',passed=True,elapsed_ms=round(elapsed*1000)))
        reset();script('exec python3 -c '+shlex.quote('print("X"*3000)'));run(False,'STATUS_STDOUT_OVERFLOW')
        cases.append(dict(case='protocol_stdout_overflow_rejected',passed=True))
        reset();manager.unlink();log,_=run(False,'STATUS_QUERY_EXIT');assert 'STATUS_EXEC_FAILED' in log and 'value=127' in log and 'errno=2' in log
        cases.append(dict(case='exec_failure127_and_errno2_logged_separately',passed=True))
        reset();script('exec python3 -c '+shlex.quote('import os,signal;os.kill(os.getpid(),signal.SIGTERM)'));log,_=run(False,'STATUS_QUERY_SIGNAL');assert 'value=143' in log
        cases.append(dict(case='session_signal_rejected_and_identified',passed=True))
        reset();script('python3 -c '+shlex.quote('import sys;sys.stderr.write("D"*8192+"\\n")')+'\n'+fixture);log,_=run(True);assert 'D'*8192 in log
        cases.append(dict(case='large_stderr_does_not_fill_protocol_pipe',passed=True))
        # Build a real COMPLETE session, so the real `new` command must retain
        # its reset requirement and return a strict RESTART_REQUIRED status.
        front=w/'front';front.mkdir();worker=w/'seed.sh'
        invocation=shlex.quote(str(w/'session.host'))+' '+shlex.quote(str(store))
        worker.write_text('#!/bin/sh\nset -e\n'+invocation+' configure raw422 100\n'+invocation+' begin front\n'+invocation+' front-ok '+shlex.quote(str(front))+'\n'+invocation+' begin backend\n'+invocation+' backend-ok\n');worker.chmod(0o755)
        seed=subprocess.run([str(w/'session.host'),str(store),'run',str(worker)],env=env,capture_output=True,text=True,timeout=5)
        assert seed.returncode==0,(seed.returncode,seed.stdout,seed.stderr)
        reset();script(actual)
        p=subprocess.run([str(w/'query'),str(runtime),'new'],env=env,capture_output=True,text=True,timeout=5)
        assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
        toast_text=toast.read_text();assert 'HARDWARE HOLD' in toast_text and 'NEW JOB CREATED' not in toast_text,toast_text
        log=(logs/'launch.log').read_text();assert 'NX51SESSION' in log and 'event=BACKEND_EXIT' in log and 'state=RESTART_REQUIRED' in log
        cases.append(dict(case='real_new_session_held_state_with_stderr_keeps_held_UI',passed=True))
    assert 'STDOUT_STATUS_ONLY_R3' in menu
    out=dict(passed=True,tests_run=len(cases),actual_C_query=True,actual_C_new_session=True,actual_HOST_session=True,
        original_ARM_output_fixture=True,hardware_tested=False,UI_tested=False,status_timeout_ms=500,cases=cases,
        source_sha256={str(x.relative_to(P)):sha(x) for x in [R/'src/menu.c',R/'src/ui_state.h',P/'session/src/session.c',P/'session/src/journal.inc',P/'session/src/rearm.inc']})
    (P/'validation/status-query-tests.json').write_text(json.dumps(out,indent=2)+'\n')
    (P/'validation/status-query-tests.txt').write_text('PASS '+str(len(cases))+' real C status/new-session cases; stdout-only protocol, separate diagnostics; no camera/UI/hardware\n')
    print(json.dumps(dict(passed=True,tests_run=len(cases))))
if __name__=='__main__':main()
