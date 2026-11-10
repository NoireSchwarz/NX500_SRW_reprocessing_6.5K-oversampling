#define main session_real_entry
#include "../src/session.c"
#undef main
#include <assert.h>
static int call(const char*cmd,const char*a,const char*b){char*args[]={"session","/mnt/mmc/NXDEMO51",(char*)cmd,(char*)a,(char*)b,NULL};return session_real_entry(b?5:a?4:3,args);}
int main(void){
 assert(!setenv("NX51_SESSION_TOKEN","0123456789abcdef0123456789abcdef",1));
 assert(call("new",NULL,NULL)==0&&call("configure","raw422","100")==0&&call("begin","pipeline",NULL)==0);
 struct session s;assert(current(&s)==1&&!strcmp(s.state,"FRONT_RUNNING")&&s.front_attempt==1&&s.backend_attempt==1&&!s.verified&&!s.requires_reset);
 assert(call("begin","backend",NULL)!=0&&call("pipeline-ok","/mnt/mmc/NXDEMO51/JOB_WRONG/front_1/front",NULL)!=0);
 assert(call("fail","SAFE_NO_SUBMIT","BOOT_INITIALIZATION_REQUIRED")==0&&current(&s)==1&&!strcmp(s.state,"RETRYABLE")&&!strcmp(s.resume,"front")&&!s.requires_reset);
 assert(call("begin","pipeline",NULL)==0&&current(&s)==1&&s.front_attempt==2&&s.backend_attempt==2);
 char dir[1200],path[1200];snprintf(dir,sizeof dir,"%s/front_2",s.job);assert(!mkdir(dir,0700));snprintf(path,sizeof path,"%s/front",dir);assert(!mkdir(path,0700));
 assert(call("pipeline-ok",path,NULL)==0&&current(&s)==1&&!strcmp(s.state,"COMPLETE")&&s.verified&&s.requires_reset&&!strcmp(s.reason,"SRW_LUT_JPEG_SAVED"));
 assert(call("begin","pipeline",NULL)!=0);assert(call("new",NULL,NULL)==0&&current(&s)==1&&!strcmp(s.state,"RESTART_REQUIRED"));
 struct session previous;assert(rearm_previous(&s,&previous)&&!strcmp(previous.reason,"SRW_LUT_JPEG_SAVED"));
 puts("PASS actual session atomic pipeline begin/commit, paired attempt counts, no backend split, no-submit retry, wrong path and no reset bypass");return 0;
}
