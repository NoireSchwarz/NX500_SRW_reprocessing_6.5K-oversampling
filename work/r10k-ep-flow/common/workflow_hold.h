/* SPDX-License-Identifier: AGPL-3.0-only. Workflow identity, not hardware idle.
 * Diagnostic/control-plane installation preserves these bytes. Retirement
 * requires a separate current resource proof and the live supervisor lease. */
#ifndef NX_WORKFLOW_HOLD_H
#define NX_WORKFLOW_HOLD_H
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
struct wh_hold {char job[1024],boot[81],reason[129],token[33];uint32_t round_pid;unsigned present,round;};
static int wh_id(const char*s,size_t cap){size_t n=strlen(s);if(!n||n>=cap)return 0;for(unsigned i=0;i<n;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'))return 0;return 1;}
static int wh_job(const char*s){const char*p="/mnt/mmc/NXDEMO51/JOB_";return !strncmp(s,p,strlen(p))&&wh_id(s+strlen(p),161);}
static int wh_read(const char*p,char*b,size_t cap){int fd=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&(uint64_t)st.st_size<cap;ssize_t n=ok?read(fd,b,(size_t)st.st_size):-1;if(close(fd))ok=0;if(!ok||n!=st.st_size)return 0;b[n]=0;return strlen(b)==(size_t)n;}
static int wh_field(char**p,const char*k,char*out,size_t cap){char*e=strchr(*p,'\n');size_t n=strlen(k);if(!e||strncmp(*p,k,n)||(size_t)(e-*p)<n||(size_t)(e-*p)-n>=cap)return 0;memcpy(out,*p+n,(size_t)(e-*p)-n);out[e-*p-n]=0;*p=e+1;return 1;}
static int wh_parents(const char*dir){char b[1200];if(!dir||dir[0]!='/'||strlen(dir)>=sizeof b||strstr(dir,".."))return 0;strcpy(b,dir);for(char*p=b+1;*p;p++)if(*p=='/'){*p=0;struct stat st;int ok=!lstat(b,&st)&&S_ISDIR(st.st_mode);*p='/';if(!ok)return 0;}return 1;}
static int wh_check(const char*dir,const char*expected,struct wh_hold*out){
 memset(out,0,sizeof *out);if(!wh_parents(dir))return 0;struct stat st;if(lstat(dir,&st))return errno==ENOENT;if(!S_ISDIR(st.st_mode))return 0;out->present=1;
 DIR*d=opendir(dir);if(!d)return 0;struct dirent*e;unsigned owner=0,hold=0,round=0;int ok=1;
 while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;if(!strcmp(e->d_name,"NX51_OWNER"))owner++;else if(!strcmp(e->d_name,"RESTART_REQUIRED"))hold++;else if(!strcmp(e->d_name,"NX51_ROUND"))round++;else ok=0;}
 if(closedir(d))ok=0;if(!ok||owner!=1||hold!=1||round>1)return 0;
 char path[1200],b[2048],head[40],job2[1024],boot2[81];snprintf(path,sizeof path,"%s/NX51_OWNER",dir);if(!wh_read(path,b,sizeof b))return 0;char*p=b;
 if(!wh_field(&p,"",head,sizeof head)||strcmp(head,"NX51LOCK1")||!wh_field(&p,"job=",out->job,sizeof out->job)||!wh_job(out->job)||!wh_field(&p,"boot=",out->boot,sizeof out->boot)||!wh_id(out->boot,sizeof out->boot)||*p||(expected&&strcmp(expected,out->job)))return 0;
 snprintf(path,sizeof path,"%s/RESTART_REQUIRED",dir);if(!wh_read(path,b,sizeof b))return 0;p=b;
 if(!wh_field(&p,"",head,sizeof head)||strcmp(head,"NX51HOLD1")||!wh_field(&p,"job=",job2,sizeof job2)||strcmp(job2,out->job)||!wh_field(&p,"boot=",boot2,sizeof boot2)||strcmp(boot2,out->boot)||!wh_field(&p,"reason=",out->reason,sizeof out->reason)||!wh_id(out->reason,sizeof out->reason)||*p)return 0;
 if(round){char pid[20];snprintf(path,sizeof path,"%s/NX51_ROUND",dir);if(!wh_read(path,b,sizeof b))return 0;p=b;
  if(!wh_field(&p,"",head,sizeof head)||strcmp(head,"NX51ROUND1")||!wh_field(&p,"token=",out->token,sizeof out->token)||strlen(out->token)!=32||strspn(out->token,"0123456789abcdef")!=32||!wh_field(&p,"pid=",pid,sizeof pid)||!pid[0]||pid[0]=='0'||strspn(pid,"0123456789")!=strlen(pid)||*p)return 0;
  unsigned long long n=strtoull(pid,NULL,10);if(n<2||n>2147483647u)return 0;out->round_pid=(uint32_t)n;out->round=1;
 }
 return 1;
}
#endif
