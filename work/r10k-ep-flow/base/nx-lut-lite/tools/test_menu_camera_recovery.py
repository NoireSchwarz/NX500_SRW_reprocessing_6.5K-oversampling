#!/usr/bin/env python3
"""Run actual camera-recovery UI action and shell dispatch with no hardware."""
from pathlib import Path
import hashlib,json,subprocess,tempfile
R=Path(__file__).resolve().parents[1];P=R.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    menu=(R/'src/menu.c').read_text()
    helpers=menu[menu.index('static FILE *launch_log_open'):menu.index('static void load_names')]
    confirm=menu[menu.index('static void srw_confirm'):menu.index('static void draw_srw')]
    background=menu[menu.index('static int memory_background'):menu.index('static int inspect')]
    prefix='''#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "HEADER"
static char engine[1100],demo_look[81]="raw422",notice[120];
static int apply_strength=100,srw_row,video_enabled,progress_reads;
static volatile sig_atomic_t action,quitting;
static struct nxl_raw_status raw_status;static struct nxl_help_state help;
static unsigned long ticks(void){return 100;}
static void load_progress_status(void){progress_reads++;}
static int srw_fstat(int fd,struct stat *st){return fstat(fd,st);}
static int nxl_lock(const char *p){int fd=open(p,O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0)return -1;if(flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;}
static void toast_timed(const char *s,int ok,unsigned long ms){(void)ok;(void)ms;FILE *f=fopen(getenv("NX51_TEST_TOAST"),"a");if(f){fprintf(f,"%s\\n",s);fclose(f);}}
'''.replace('HEADER',str((R/'src/ui_state.h').resolve()))
    suffix='''static void set(enum nxl_raw_state state,int valid){memset(&raw_status,0,sizeof raw_status);memset(&help,0,sizeof help);raw_status.state=state;raw_status.valid=valid;strcpy(raw_status.reason,"UNKNOWN_HOLD");action=quitting=progress_reads=0;srw_row=7;notice[0]=0;}
int main(int argc,char **argv){assert(argc==3);setenv("NX_LUT_ROOT",argv[1],1);snprintf(engine,sizeof engine,"%s/lut.sh",argv[1]);if(!strcmp(argv[2],"ui")){int checks=0;
const enum nxl_raw_state allowed[]={NXL_RAW_COMPLETE,NXL_RAW_NEED_STILL_INIT,NXL_RAW_NEED_JPEG_INIT,NXL_RAW_RETRYABLE,NXL_RAW_RESTART_REQUIRED,NXL_RAW_INVALID};
for(unsigned i=0;i<sizeof allowed/sizeof *allowed;i++){set(allowed[i],allowed[i]!=NXL_RAW_INVALID);struct nxl_raw_status before=raw_status;srw_confirm();assert(action==10&&quitting&&!memcmp(&before,&raw_status,sizeof before));checks++;}
set(NXL_RAW_FRONT_RUNNING,1);srw_confirm();assert(!action&&!quitting&&strstr(notice,"RUNNING"));checks++;
set(NXL_RAW_BACKEND_RUNNING,1);srw_confirm();assert(!action&&!quitting&&strstr(notice,"RUNNING"));checks++;
set(NXL_RAW_COMPLETE,1);srw_row=4;srw_confirm();assert(!action&&!quitting&&strstr(notice,"NATIVE.JPG"));checks++;
assert(NXL_DEMO_ROWS==9);checks++;printf("checks=%d\\n",checks);return 0;}
int rc=memory_background(-1,3);if(rc)return rc;int status;while(waitpid(-1,&status,0)>0){}return 0;}
'''
    with tempfile.TemporaryDirectory(prefix='nx51-menu-restore-') as tmp:
        w=Path(tmp);sd=w/'sdcard';sd.mkdir();alias=w/'mmc';alias.symlink_to(sd,target_is_directory=True);logs=sd/'NXDEMO51/logs';logs.mkdir(parents=True)
        runtime=w/'runtime';(runtime/'bin').mkdir(parents=True);toast=w/'toast'
        source=(prefix+helpers+confirm+background+suffix).replace('/mnt/mmc/',str(alias)+'/').replace('/tmp/nx-lut-lite/',str(runtime)+'/')
        (w/'test.c').write_text(source);subprocess.run(['cc','-O0','-Wno-unused-function',str(w/'test.c'),'-o',str(w/'test')],check=True)
        ui=subprocess.run([str(w/'test'),str(runtime),'ui'],capture_output=True,text=True,check=True);assert ui.stdout=='checks=10\n'
        (runtime/'bin/nxl-session.arm').write_text('#!/bin/sh\nprintf WRONG_SESSION_CALL\nexit 37\n');(runtime/'bin/nxl-session.arm').chmod(0o755)
        state=sd/'NXDEMO51/SESSION_MODEL_UNTOUCHED';state.write_text('COMPLETE\n');before=sha(state)
        script=runtime/'memory-test.sh';script.write_text('#!/bin/sh\n[ "$#" = 1 ] && [ "$1" = --restore-camera ] || exit 29\nprintf "RESTORE_FLAG=%s\\n" "$1"\nprintf "RESTORE_DIAGNOSTIC\\n" >&2\nexit 0\n')
        import os
        env=os.environ.copy();env['NX51_TEST_TOAST']=str(toast)
        p=subprocess.run([str(w/'test'),str(runtime),'restore'],env=env,capture_output=True,text=True,timeout=10);assert p.returncode==0
        for name in ['launch.log','run.log']:
            text=(logs/name).read_text();assert 'operation=restore-camera' in text and 'RESTORE_FLAG=--restore-camera' in text and 'RESTORE_DIAGNOSTIC' in text and 'event=BACKEND_EXIT value=0' in text
            assert 'WRONG_SESSION_CALL' not in text
        assert sha(state)==before and not list((sd/'NXDEMO51').glob('JOB_*'))
        script.write_text('#!/bin/sh\nprintf "RESTORE_TIMEOUT_MODEL\\n" >&2\nexit 41\n')
        p=subprocess.run([str(w/'test'),str(runtime),'restore'],env=env,capture_output=True,text=True,timeout=10);assert p.returncode==0
        text=toast.read_text();assert 'RECOVERY UNCONFIRMED' in text and 'STOPPED' not in text
        assert sha(state)==before
    flags=['--demo-camera-restoring','--demo-camera-restored','--demo-camera-restore-failed']
    assert all(flag in menu for flag in flags) and 'REAL OEM LIVEVIEW RESTORED' in menu and 'CAMERA_RECOVERY_R5' in menu
    rec=dict(passed=True,tests_run=13,context_cases=10,actual_C_context_action=True,actual_C_shell_dispatch=True,
        COMPLETE_manual_restore_allowed=True,no_session_new_or_run=True,existing_state_unchanged=True,human_stderr_preserved=True,
        failure_popup_unconfirmed=True,hardware_tested=False,X11_tested=False,row_count=9,
        source_sha256={str(p.relative_to(P)):sha(p) for p in [R/'src/menu.c',R/'src/ui_state.h']})
    (P/'validation/menu-camera-recovery-tests.json').write_text(json.dumps(rec,indent=2)+'\n')
    (P/'validation/menu-camera-recovery-tests.txt').write_text('PASS 10 actual C context cases + restore-only shell dispatch + unconfirmed failure popup + CLI identity contract; no camera/hardware\n')
    print(json.dumps(dict(passed=True,tests_run=13)))
if __name__=='__main__':main()
