#!/usr/bin/env python3
"""Actual R7 UI eligibility/router, child logs and popups; no reset authority."""
from pathlib import Path
import ast,hashlib,json,os,subprocess,tempfile
R=Path(__file__).resolve().parents[1];P=R.parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 menu=(R/'src/menu.c').read_text();header=R/'src/ui_state.h'
 helpers=menu[menu.index('static FILE *launch_log_open'):menu.index('static void load_names')]
 confirm=menu[menu.index('static void srw_confirm'):menu.index('static void draw_srw')]
 background=menu[menu.index('static int memory_background'):menu.index('static int inspect')]
 # Reuse only the existing host UI scaffolding, never a second implementation
 # of the actual confirmation/router functions being exercised.
 scaffold=(R/'tools/test_menu_camera_recovery.py').read_text().split("prefix='''",1)[1].split("'''.replace('HEADER'",1)[0]
 prefix=ast.literal_eval("'''"+scaffold+"'''").replace('HEADER',str(header.resolve()))
 prefix=prefix.replace('fprintf(f,"%s\\n",s);','fprintf(f,"%d %lu %s\\n",ok,ms,s);')
 suffix=r'''
static void set(void){memset(&raw_status,0,sizeof raw_status);memset(&help,0,sizeof help);raw_status.valid=1;raw_status.state=NXL_RAW_RESTART_REQUIRED;strcpy(raw_status.resume,"front");strcpy(raw_status.reason,"REAL_KERNEL_RESTART_NOT_VERIFIED");action=quitting=progress_reads=0;srw_row=8;notice[0]=0;}
int main(int argc,char **argv){assert(argc==3);setenv("NX_LUT_ROOT",argv[1],1);snprintf(engine,sizeof engine,"%s/lut.sh",argv[1]);if(!strcmp(argv[2],"ui")){int checks=0;
set();struct nxl_raw_status before=raw_status;assert(nxl_raw_can_rearm(&raw_status));assert(!strcmp(nxl_raw_status_action(&raw_status),"VERIFY RESET"));srw_confirm();assert(action==11&&quitting&&!memcmp(&before,&raw_status,sizeof before));checks++;
set();srw_row=4;srw_confirm();assert(action==11&&quitting);checks++;
set();srw_row=5;srw_confirm();assert(!action&&!quitting&&strstr(notice,"CHECK RAW RESET"));checks++;
const enum nxl_raw_state rejected[]={NXL_RAW_INVALID,NXL_RAW_NEED_STILL_INIT,NXL_RAW_FRONT_RUNNING,NXL_RAW_NEED_JPEG_INIT,NXL_RAW_BACKEND_RUNNING,NXL_RAW_COMPLETE,NXL_RAW_RETRYABLE};
for(unsigned i=0;i<sizeof rejected/sizeof *rejected;i++){set();raw_status.state=rejected[i];assert(!nxl_raw_can_rearm(&raw_status));srw_confirm();assert(!action&&!quitting);checks++;}
for(unsigned i=0;i<7;i++){set();if(i==0)raw_status.valid=0;if(i==1)raw_status.front_verified=1;if(i==2)strcpy(raw_status.resume,"backend");if(i==3)strcpy(raw_status.resume,"none");if(i==4)strcpy(raw_status.reason,"UNKNOWN_REQUIRE_RESTART");if(i==5)strcat(raw_status.reason,"_EXTRA");if(i==6)raw_status.reason[0]=0;assert(!nxl_raw_can_rearm(&raw_status));srw_confirm();assert(!action&&!quitting);checks++;}
set();strcpy(raw_status.reason,"UNKNOWN_REQUIRE_RESTART");srw_row=4;srw_confirm();assert(!action&&!quitting&&srw_row==6&&progress_reads==1);checks++;
const char *record="NX51STATUS1\nversion=0.4.0-demo51-422\nstate=RESTART_REQUIRED\nfront_verified=0\nresume=front\nreason=REAL_KERNEL_RESTART_NOT_VERIFIED\nend=NX51END1\n";struct nxl_raw_status parsed;memset(&parsed,0,sizeof parsed);assert(nxl_raw_status_parse(record,&parsed)&&nxl_raw_can_rearm(&parsed));checks++;
char bad[500];snprintf(bad,sizeof bad,"%s",record);strstr(bad,"end=")[0]=0;memset(&parsed,0,sizeof parsed);assert(!nxl_raw_status_parse(bad,&parsed)&&!nxl_raw_can_rearm(&parsed));checks++;
assert(NXL_DEMO_ROWS==9);checks++;printf("checks=%d\n",checks);return 0;}
int rc=memory_background(-1,4);if(rc)return rc;int status;while(waitpid(-1,&status,0)>0){}return 0;}
'''
 with tempfile.TemporaryDirectory(prefix='nx51-menu-rearm-') as td:
  w=Path(td);sd=w/'card';sd.mkdir();alias=w/'mmc';alias.symlink_to(sd,target_is_directory=True);logs=sd/'NXDEMO51/logs';logs.mkdir(parents=True);runtime=w/'runtime';(runtime/'bin').mkdir(parents=True);toast=w/'toast'
  text=(prefix+helpers+confirm+background+suffix).replace('/mnt/mmc/',str(alias)+'/').replace('/tmp/nx-lut-lite/',str(runtime)+'/');(w/'test.c').write_text(text);subprocess.run(['cc','-O0','-Wno-unused-function',str(w/'test.c'),'-o',str(w/'test')],check=True)
  p=subprocess.run([str(w/'test'),str(runtime),'ui'],check=True,capture_output=True,text=True);assert p.stdout=='checks=21\n',p.stdout
  state=sd/'NXDEMO51/SESSION_UNTOUCHED';state.write_text('RESTART_REQUIRED\n');before=sha(state)
  (runtime/'bin/nxl-session.arm').write_text('#!/bin/sh\nprintf WRONG_DIRECT_SESSION_CALL\nexit 37\n');(runtime/'bin/nxl-session.arm').chmod(0o755)
  (runtime/'lut.sh').write_text('#!/bin/sh\nprintf WRONG_NATIVE_ENGINE_CALL\nexit 39\n')
  script=runtime/'memory-test.sh';script.write_text('#!/bin/sh\n[ "$#" = 1 ] && [ "$1" = --rearm ] || exit 29\nprintf "REARM_FLAG=%s\\n" "$1"\nprintf "PROOF_ONLY_DIAGNOSTIC\\n" >&2\nexit 0\n')
  env={**os.environ,'NX51_TEST_TOAST':str(toast)};subprocess.run([str(w/'test'),str(runtime),'rearm'],env=env,check=True,capture_output=True,text=True,timeout=10)
  for name in ['launch.log','run.log']:
   text=(logs/name).read_text();assert 'operation=rearm-session' in text and 'REARM_FLAG=--rearm' in text and 'PROOF_ONLY_DIAGNOSTIC' in text and 'event=BACKEND_EXIT value=0' in text;assert 'WRONG_' not in text
  assert sha(state)==before and not list((sd/'NXDEMO51').glob('JOB_*'));assert '1 12000 RAW SESSION READY - TAKE JPEG THEN PREPARE RAW' in toast.read_text()
  script.write_text('#!/bin/sh\nprintf "RESOURCE_NOT_PROVEN\\n" >&2\nexit 41\n');subprocess.run([str(w/'test'),str(runtime),'rearm'],env=env,check=True,capture_output=True,text=True,timeout=10);assert '0 12000 RESET NOT PROVEN - CHECK REARM LOG' in toast.read_text();assert sha(state)==before
 assert 'action==11 ? memory_background(lock,4)' in menu and 'MENU_SESSION_REARM=RESOURCE_REARM_R7' in menu
 assert all(x in menu for x in ['--demo-rearm-checking','--demo-rearm-ready','--demo-rearm-held','CHECK RAW RESET'])
 assert 'CHECK RAW RESET - THEN TAKE A PHOTO' in header.read_text() and 'nxl-session-rearm.arm' not in menu and 'ptrace(' not in menu
 rec={'passed':True,'hardware_tested':False,'X11_tested':False,'tests_run':25,'context_cases':21,'row_count':9,'actual_C_context_action':True,'actual_C_shell_dispatch':True,'no_direct_hardware_or_state_permission':True,'existing_state_unchanged':True,'success_and_failure_popups':True,'human_stderr_preserved':True,'source_sha256':{str(p.relative_to(P)):sha(p) for p in [R/'src/menu.c',header,Path(__file__),R/'tools/test_menu_camera_recovery.py']}}
 (P/'validation/menu-rearm-tests.json').write_text(json.dumps(rec,indent=2)+'\n');print(json.dumps({'passed':True,'tests_run':25}))
if __name__=='__main__':main()
