/* SPDX-License-Identifier: AGPL-3.0-only. Read-only admission for upgrading
 * the exact R7d no-submit incident. Installation preserves every hold. */
static int readonly_install_preflight(const char *job,const char *saved_boot){
 if(!safe(job,1024,1)||!safe(saved_boot,64,0)||!realdir(root)||!no_symlink_parents(root))return 76;
 char path[2048];if(!bounded(path,sizeof path,root,"run.lock"))return 76;
 int runfd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(runfd<0)return 76;
 struct stat st;if(fstat(runfd,&st)||!S_ISREG(st.st_mode)||flock(runfd,LOCK_SH|LOCK_NB)){close(runfd);return 76;}
 int statefd=readonly_state_lock();if(statefd<0){close(runfd);return 76;}
 struct session s,previous;int ok=current(&s)==1&&!strcmp(s.job,job)&&!strcmp(s.boot,saved_boot)&&
  !lease_live(NULL,0)&&preflight_legacy(&s)&&rearm_previous(&s,&previous)&&
  exact_text("/opt/usr/nx-lut-lite/state/p49-experiment.state","NXP33INSTALL1\nREADY=0.3.6-demo50\n");
 if(close(statefd))ok=0;if(close(runfd))ok=0;
 printf("INSTALL_PREFLIGHT_R7F evidence=%s holds_changed=0 session_changed=0 native_calls=0\n",ok?"VERIFIED":"HELD");
 return ok?0:76;
}
static int workflow_locks_absent(void){
 const char *names[]={"engine.lock","trace.lock","memory-test.lock","demo50-recovery.lock"};
 for(unsigned i=0;i<4;i++){
  char path[256];struct stat st;snprintf(path,sizeof path,"/tmp/nx-lut-lite/%s",names[i]);
  if(!lstat(path,&st)||errno!=ENOENT)return 0;
 }
 return 1;
}
static int lock_reset_receipt(const struct session *s,const char *dir){
 const char *token=getenv("NX51_SESSION_TOKEN");char path[2048],want[1600];
 if(!token||strlen(token)!=32||!bounded(path,sizeof path,dir,"LOCKS_RESET"))return 0;
 int n=snprintf(want,sizeof want,"NX51LOCKRESET1\njob=%s\ntoken=%s\nengine=absent\ntrace=absent\nmemory_test=absent\nrecovery=absent\nnext_round=fresh_session_token\nend=NX51LOCKRESETEND1\n",s->job,token);
 return n>0&&n<(int)sizeof want&&exact_text(path,want)&&workflow_locks_absent();
}
