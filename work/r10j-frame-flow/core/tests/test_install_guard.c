/* SPDX-License-Identifier: AGPL-3.0-only. Actual per-install guard and race. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static char state_path[1200];
#define P33_INSTALL_STATE_PATH state_path
#include "install_guard.inc"
static void put(const char *p,const char *s){FILE *f=fopen(p,"w");assert(f);assert(fputs(s,f)>=0);assert(!fclose(f));}
int main(int argc,char **argv){
 assert(argc==2);char report[1200],other[1200],audit_path[1400],data[8192];unsigned cases=0;
 snprintf(state_path,sizeof state_path,"%s/state",argv[1]);snprintf(report,sizeof report,"%s/report",argv[1]);snprintf(other,sizeof other,"%s/other",argv[1]);assert(!mkdir(report,0700)&&!mkdir(other,0700));
 assert(!p33_guard_prepare(report)&&!strcmp(p33_guard_reason,"INSTALL_GRANT_MISSING_RUN_DEMO_INSTALLER"));cases++;
 put(state_path,"corrupt\n");assert(!p33_guard_prepare(report)&&!strcmp(p33_guard_reason,"INSTALL_GRANT_INVALID"));cases++;
 put(state_path,P33_READY_STATE);assert(p33_guard_prepare(report)&&p33_install_prepared&&!p33_guard_acquired);cases++;
 assert(p33_guard_prepare(report));FILE *f=fopen(state_path,"r");assert(f);size_t n=fread(data,1,sizeof data,f);fclose(f);assert(p33_grant_ready(data,n));cases++;
 assert(p33_guard_claim(report)&&p33_guard_acquired);assert(p33_guard_note("SIMULATED_UNCERTAIN_NATIVE_CALL"));p33_guard_close();cases++;
 assert(!p33_guard_prepare(other)&&!strcmp(p33_guard_reason,"ATTEMPT_ALREADY_STARTED_THIS_INSTALL_SEE_GUARD_REPORT"));cases++;
 snprintf(audit_path,sizeof audit_path,"%s/p39_attempt_guard.txt",other);f=fopen(audit_path,"r");assert(f);n=fread(data,1,sizeof data-1,f);data[n]=0;fclose(f);assert(strstr(data,report)&&strstr(data,"SIMULATED_UNCERTAIN_NATIVE_CALL"));cases++;
 /* A later launch, with any clock/boot identity, cannot reset STARTED. */
 p33_install_prepared=p33_guard_acquired=0;assert(!p33_guard_prepare(other));cases++;
 /* Only explicit reinstallation writes a fresh READY grant. */
 put(state_path,P33_READY_STATE);assert(p33_guard_prepare(report));cases++;
 int gate[2];assert(!pipe(gate));pid_t children[2];
 for(unsigned i=0;i<2;i++){children[i]=fork();assert(children[i]>=0);if(!children[i]){close(gate[1]);char c;assert(read(gate[0],&c,1)==1);int ok=p33_guard_claim(i?other:report);p33_guard_close();_exit(ok?0:42);}}
 close(gate[0]);assert(write(gate[1],"xx",2)==2);close(gate[1]);unsigned winners=0,blocked=0;
 for(unsigned i=0;i<2;i++){int status;assert(waitpid(children[i],&status,0)==children[i]&&WIFEXITED(status));winners+=WEXITSTATUS(status)==0;blocked+=WEXITSTATUS(status)==42;}
 assert(winners==1&&blocked==1);cases++;
 assert(!p33_guard_prepare(other));cases++;
 /* Simulate interrupted grant consumption: empty is never treated as READY. */
 put(state_path,"");assert(!p33_guard_prepare(other)&&!strcmp(p33_guard_reason,"INSTALL_GRANT_INVALID"));cases++;
 printf("PASS %u actual per-install grant/state/race cases; no boot ID dependency; NO CAMERA\n",cases);return 0;
}
