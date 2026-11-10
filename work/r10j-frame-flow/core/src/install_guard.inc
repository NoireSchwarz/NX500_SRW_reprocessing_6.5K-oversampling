/* SPDX-License-Identifier: AGPL-3.0-only. One native attempt per installation.
 * No inference about power cycling from boot_id, uptime, /tmp or clocks.
 * Read-only preflight leaves READY intact. Before the first native request,
 * flock serializes claims and STARTED is persisted. Never auto-reset STARTED.
 */
#include <sys/file.h>
#ifndef P33_INSTALL_STATE_PATH
#define P33_INSTALL_STATE_PATH "/opt/usr/nx-lut-lite/state/p49-experiment.state"
#endif
#define P33_READY_STATE "NXP33INSTALL1\nREADY=0.3.6-demo50\n"
#define P33_STARTED_STATE "NXP33INSTALL1\nSTARTED=0.3.6-demo50\n"
static unsigned p33_guard_acquired,p33_install_prepared;
static int p33_guard_fd=-1;
static const char *p33_guard_reason="INSTALL_GRANT_NOT_PREPARED";
static int p33_grant_read(int fd,char data[4097],size_t *length){
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||lseek(fd,0,SEEK_SET)!=0)return 0;
 ssize_t n=read(fd,data,4096);if(n<0)return 0;data[n]=0;*length=(size_t)n;return 1;
}
static int p33_grant_ready(const char *data,size_t n){return n==sizeof P33_READY_STATE-1&&!memcmp(data,P33_READY_STATE,n);}
static void p33_grant_audit(FILE *f,const char *data,size_t n){
 fputs("PRIOR_STATE_BEGIN\n",f);fwrite(data,1,n,f);fputs("\nPRIOR_STATE_END\n",f);
}
static const char *p33_grant_denial(const char *data,size_t n){
 return n>=sizeof P33_STARTED_STATE-1&&!memcmp(data,P33_STARTED_STATE,sizeof P33_STARTED_STATE-1)?"ATTEMPT_ALREADY_STARTED_THIS_INSTALL_SEE_GUARD_REPORT":"INSTALL_GRANT_INVALID";
}
static int p33_guard_prepare(const char *report){
 p33_install_prepared=0;char path[1200],data[4097];size_t n=0;
 snprintf(path,sizeof path,"%s/p39_attempt_guard.txt",report);FILE *audit=fopen(path,"w");
 if(!audit){p33_guard_reason="ATTEMPT_GUARD_AUDIT_CREATE_FAILED";return 0;}
 fprintf(audit,"format=NXP33INSTALL1\npolicy=once_per_install\nstate_path=%s\n",P33_INSTALL_STATE_PATH);
 int fd=open(P33_INSTALL_STATE_PATH,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK),ok=0;
 if(fd<0){p33_guard_reason="INSTALL_GRANT_MISSING_RUN_DEMO_INSTALLER";goto done;}
 if(!p33_grant_read(fd,data,&n)){p33_guard_reason="INSTALL_GRANT_READ_FAILED";goto done;}
 if(!p33_grant_ready(data,n)){p33_grant_audit(audit,data,n);p33_guard_reason=p33_grant_denial(data,n);goto done;}
 fputs("preflight_did_not_consume_grant=1\n",audit);ok=1;p33_guard_reason="INSTALL_GRANT_PREPARED";
done:
 if(fd>=0)close(fd);fprintf(audit,"result=%s\n",p33_guard_reason);
 if(fflush(audit)||fsync(fileno(audit)))ok=0;if(fclose(audit))ok=0;
 if(!ok&&!strcmp(p33_guard_reason,"INSTALL_GRANT_PREPARED"))p33_guard_reason="ATTEMPT_GUARD_AUDIT_SAVE_FAILED";
 p33_install_prepared=(unsigned)ok;return ok;
}
static int p33_guard_note(const char *phase){return p33_guard_fd>=0&&dprintf(p33_guard_fd,"phase=%s\n",phase)>0&&fsync(p33_guard_fd)==0;}
static void p33_guard_close(void){if(p33_guard_fd>=0){close(p33_guard_fd);p33_guard_fd=-1;}}
static int p33_guard_claim(const char *report){
 if(!p33_install_prepared){p33_guard_reason="INSTALL_GRANT_NOT_PREPARED";return 0;}
 int fd=open(P33_INSTALL_STATE_PATH,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);char data[4097];size_t n=0;
 if(fd<0){p33_guard_reason="INSTALL_GRANT_OPEN_FOR_CLAIM_FAILED";return 0;}
 if(flock(fd,LOCK_EX|LOCK_NB)){p33_guard_reason="INSTALL_GRANT_IN_USE";close(fd);return 0;}
 if(!p33_grant_read(fd,data,&n)||!p33_grant_ready(data,n)){
  p33_guard_reason=n?p33_grant_denial(data,n):"INSTALL_GRANT_READ_FAILED";
  char path[1200];snprintf(path,sizeof path,"%s/p39_attempt_guard.txt",report);FILE *audit=fopen(path,"a");
  if(audit){fprintf(audit,"claim_result=%s\n",p33_guard_reason);p33_grant_audit(audit,data,n);fclose(audit);}close(fd);return 0;
 }
 p33_guard_fd=fd;p33_guard_acquired=1;
 /* Persist consumption before any native call. A crash/partial write fails
  * closed on the next launch; no attempt is re-enabled by process exit. */
 if(ftruncate(fd,0)||lseek(fd,0,SEEK_SET)!=0||dprintf(fd,P33_STARTED_STATE "pid=%ld\nreport=%s\n",(long)getpid(),report)<0||!p33_guard_note("BEFORE_NATIVE_REQUEST")){
  p33_guard_reason="INSTALL_GRANT_CONSUME_SAVE_FAILED";return 0;
 }
 p33_guard_reason="INSTALL_GRANT_CONSUMED";return 1;
}
