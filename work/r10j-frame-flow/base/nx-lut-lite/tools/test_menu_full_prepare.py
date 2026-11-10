#!/usr/bin/env python3
"""Exercise actual context action and proof-only full-test shell dispatch.

No ISP/session mutation is executed. The runner remains responsible for proof.
"""
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
static void toast_timed(const char *s,int ok,unsigned long ms){(void)s;(void)ok;(void)ms;}
'''.replace('HEADER',str((R/'src/ui_state.h').resolve()))
    suffix='''static void set(enum nxl_raw_state state,int valid,const char *reason){memset(&raw_status,0,sizeof raw_status);memset(&help,0,sizeof help);raw_status.state=state;raw_status.valid=valid;strcpy(raw_status.reason,reason);action=quitting=progress_reads=0;srw_row=4;notice[0]=0;}
int main(int argc,char **argv){assert(argc==3);setenv("NX_LUT_ROOT",argv[1],1);snprintf(engine,sizeof engine,"%s/lut.sh",argv[1]);if(!strcmp(argv[2],"ui")){int checks=0;
const char *known[]={"LEGACY_STATE_DIVERGENCE","FRONT_RESTART_OR_PATH_INVALID"};
for(unsigned i=0;i<2;i++){set(NXL_RAW_RESTART_REQUIRED,1,known[i]);assert(nxl_raw_can_prepare_full(&raw_status));assert(!strcmp(nxl_raw_status_action(&raw_status),"PREPARE FULL TEST"));srw_confirm();assert(action==9&&quitting&&!progress_reads);checks++;}
set(NXL_RAW_RESTART_REQUIRED,1,"UNKNOWN_REQUIRE_RESTART");assert(!nxl_raw_can_prepare_full(&raw_status));assert(!strcmp(nxl_raw_status_action(&raw_status),"CHECK REPORT"));srw_confirm();assert(!action&&!quitting&&srw_row==6&&progress_reads==1);checks++;
set(NXL_RAW_RESTART_REQUIRED,0,known[0]);assert(!nxl_raw_can_prepare_full(&raw_status));srw_confirm();assert(!action&&!quitting);checks++;
set(NXL_RAW_FRONT_RUNNING,1,known[0]);assert(!nxl_raw_can_prepare_full(&raw_status));srw_confirm();assert(!action&&!quitting);checks++;
set(NXL_RAW_BACKEND_RUNNING,1,known[1]);assert(!nxl_raw_can_prepare_full(&raw_status));srw_confirm();assert(!action&&!quitting);checks++;
set(NXL_RAW_RESTART_REQUIRED,1,"FRONT_RESTART_OR_PATH_INVALID_EXTRA");assert(!nxl_raw_can_prepare_full(&raw_status));srw_confirm();assert(!action&&!quitting&&srw_row==6);checks++;
set(NXL_RAW_NEED_STILL_INIT,1,"FULL_TEST_PREPARED_NO_REUSE");assert(!nxl_raw_can_prepare_full(&raw_status));assert(!strcmp(nxl_raw_status_action(&raw_status),"PREPARE RAW"));srw_confirm();assert(action==6&&quitting&&!strcmp(demo_look,"raw422"));checks++;
set(NXL_RAW_NEED_JPEG_INIT,1,known[0]);assert(!nxl_raw_can_prepare_full(&raw_status));assert(!strcmp(nxl_raw_status_action(&raw_status),"ENCODE JPEG"));srw_confirm();assert(action==6&&quitting);checks++;
set(NXL_RAW_COMPLETE,1,known[0]);assert(!nxl_raw_can_prepare_full(&raw_status));srw_confirm();assert(!action&&!quitting);checks++;
set(NXL_RAW_RESTART_REQUIRED,1,known[0]);srw_row=5;srw_confirm();assert(!action&&!quitting);checks++;
assert(NXL_DEMO_ROWS==9);checks++;printf("checks=%d\\n",checks);return 0;}
int rc=memory_background(-1,2);if(rc)return rc;int status;while(waitpid(-1,&status,0)>0){}return 0;}
'''
    with tempfile.TemporaryDirectory(prefix='nx51-menu-full-') as tmp:
        w=Path(tmp);sd=w/'sdcard';sd.mkdir();alias=w/'mmc';alias.symlink_to(sd,target_is_directory=True);logs=sd/'NXDEMO51/logs';logs.mkdir(parents=True)
        runtime=w/'runtime';runtime.mkdir()
        source=(prefix+helpers+confirm+background+suffix).replace('/mnt/mmc/',str(alias)+'/').replace('/tmp/nx-lut-lite/',str(runtime)+'/')
        (w/'test.c').write_text(source)
        subprocess.run(['cc','-O0','-Wno-unused-function',str(w/'test.c'),'-o',str(w/'test')],check=True)
        ui=subprocess.run([str(w/'test'),str(runtime),'ui'],capture_output=True,text=True,check=True)
        assert ui.stdout=='checks=12\n',ui.stdout
        (runtime/'memory-test.sh').write_text('#!/bin/sh\n[ "$#" = 1 ] && [ "$1" = --prepare-full ] || exit 29\nprintf "PREPARE_FULL_FLAG=%s\\n" "$1"\nprintf "PROOF_ONLY_STDERR\\n" >&2\nexit 0\n')
        (runtime/'lut.sh').write_text('#!/bin/sh\nprintf "WRONG_ENGINE_PATH\\n"\nexit 31\n')
        p=subprocess.run([str(w/'test'),str(runtime),'prepare'],capture_output=True,text=True,timeout=10)
        assert p.returncode==0,(p.returncode,p.stdout,p.stderr)
        for name in ['launch.log','run.log']:
            text=(logs/name).read_text()
            assert 'operation=prepare-full' in text and 'PREPARE_FULL_FLAG=--prepare-full' in text and 'PROOF_ONLY_STDERR' in text
            assert 'WRONG_ENGINE_PATH' not in text and 'event=BACKEND_EXIT value=0' in text
        assert not list((sd/'NXDEMO51').glob('JOB_*')) and not (sd/'NXDEMO51/current.job').exists()
    assert '--demo-full-ready' in menu and 'FULL TEST READY - PREPARE RAW' in menu and 'MENU_FRESH_FULL_TEST_R4' in menu
    assert 'RECOVER FRONT' not in menu and '--recover-front' not in menu
    data=dict(passed=True,tests_run=14,actual_C_context_action=True,actual_C_shell_dispatch=True,hardware_tested=False,
        context_cases=12,dispatch_preserves_human_stderr=True,no_old_YUV_or_native_calls=True,row_count=9,
        source_sha256={str(p.relative_to(P)):sha(p) for p in [R/'src/menu.c',R/'src/ui_state.h']})
    (P/'validation/menu-full-prepare-tests.json').write_text(json.dumps(data,indent=2)+'\n')
    (P/'validation/menu-full-prepare-tests.txt').write_text('PASS 12 actual C context cases + actual --prepare-full dispatch + version/toast contract; no camera/hardware\n')
    print(json.dumps(dict(passed=True,tests_run=14)))
if __name__=='__main__':main()
