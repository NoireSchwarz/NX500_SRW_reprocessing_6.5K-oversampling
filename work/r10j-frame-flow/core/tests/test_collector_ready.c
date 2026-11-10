#define main production_entry_not_run
#include "nxl-native-front.c"
#undef main
#include <assert.h>
static void path(char *out,size_t n,const char *dir,const char *name){assert(snprintf(out,n,"%s/%s",dir,name)>0);}
static void marker(const char *dir){
 char p[1200],b[200]={0};path(p,sizeof p,dir,"READY");FILE *f=fopen(p,"rb");assert(f);size_t n=fread(b,1,sizeof b-1,f);assert(!fclose(f));
 assert(n==strlen("P41_COLLECTOR_READY_ONLY\nRAW_PREPARATION_NOT_STARTED\nNATIVE_GO_NOT_SENT\n")&&!strcmp(b,"P41_COLLECTOR_READY_ONLY\nRAW_PREPARATION_NOT_STARTED\nNATIVE_GO_NOT_SENT\n"));
 path(p,sizeof p,dir,"READY.pending");assert(access(p,F_OK)!=0);
}
int main(int argc,char **argv){
 assert(argc==2);char log[1200],p[1200],pending[1200];started=ns();path(log,sizeof log,argv[1],"marker.log");log_file=fopen(log,"w");assert(log_file);
 int gate[2],notice[2];assert(!pipe(gate)&&!pipe(notice));pid_t child=fork();assert(child>=0);
 if(!child){close(gate[1]);close(notice[0]);assert(collector_ready(argv[1]));char c='R';assert(write(notice[1],&c,1)==1);assert(read(gate[0],&c,1)==1);_exit(0);}
 close(gate[0]);close(notice[1]);struct pollfd poller={notice[0],POLLIN,0};assert(poll(&poller,1,1000)==1);char c;assert(read(notice[0],&c,1)==1&&c=='R');marker(argv[1]);int status;
 assert(waitpid(child,&status,WNOHANG)==0);assert(write(gate[1],"G",1)==1);assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 close(gate[1]);close(notice[0]);path(p,sizeof p,argv[1],"READY");assert(!collector_ready(argv[1]));marker(argv[1]);assert(!unlink(p));
 path(pending,sizeof pending,argv[1],"READY.pending");assert(!mkdir(pending,0700));assert(!collector_ready(argv[1])&&access(p,F_OK)!=0);assert(!rmdir(pending));
 char absent[1200];path(absent,sizeof absent,argv[1],"absent/child");assert(!collector_ready(absent));
 assert(collector_ready(argv[1]));marker(argv[1]);assert(native_calls==0);assert(!fclose(log_file));
 puts("PASS collector marker: complete atomic publication while preparation is blocked, existing marker preserved, pending collision/missing directory rejected; no native APIs invoked");return 0;
}
