/* SPDX-License-Identifier: AGPL-3.0-only. Actual RESET receipt/absence gates. */
#define open model_open
#define read model_read
#define close model_close
#define fstat model_fstat
#define lstat model_lstat
#define getenv model_getenv
#define main production_session_main_not_run
#include "../src/session.c"
#undef main
#undef open
#undef read
#undef close
#undef fstat
#undef lstat
#undef getenv
#include <assert.h>
static const char receipt[]="NX51LOCKRESET1\njob=/model/JOB_test\ntoken=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\nengine=absent\ntrace=absent\nmemory_test=absent\nrecovery=absent\nnext_round=fresh_session_token\nend=NX51LOCKRESETEND1\n";
static int held=-1,missing_token,short_read,bad_type,bad_close;
static size_t offset;
static char active_token[40]="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
char *model_getenv(const char *name){assert(!strcmp(name,"NX51_SESSION_TOKEN"));return missing_token?NULL:active_token;}
int model_open(const char *path,int flags,...){assert(!strcmp(path,"/model/proof/LOCKS_RESET"));assert((flags&O_ACCMODE)==O_RDONLY&&(flags&O_NOFOLLOW));offset=0;return 42;}
ssize_t model_read(int fd,void *out,size_t n){assert(fd==42);if(short_read)return 0;size_t z=strlen(receipt)-offset;if(n>z)n=z;memcpy(out,receipt+offset,n);offset+=n;return (ssize_t)n;}
int model_close(int fd){assert(fd==42);return bad_close?-1:0;}
int model_fstat(int fd,struct stat *st){assert(fd==42);memset(st,0,sizeof *st);st->st_mode=bad_type?S_IFDIR:S_IFREG;st->st_size=strlen(receipt);return 0;}
int model_lstat(const char *path,struct stat *st){const char *names[]={"engine.lock","trace.lock","memory-test.lock","demo50-recovery.lock"};for(unsigned i=0;i<4;i++){char expected[128];snprintf(expected,sizeof expected,"/tmp/nx-lut-lite/%s",names[i]);if(!strcmp(expected,path)){if((int)i==held){memset(st,0,sizeof *st);st->st_mode=S_IFDIR;return 0;}errno=ENOENT;return -1;}}assert(0);return -1;}
int main(void){unsigned checks=0;struct session s={0};strcpy(s.job,"/model/JOB_test");
 assert(lock_reset_receipt(&s,"/model/proof"));checks++;
 for(unsigned i=0;i<4;i++){held=(int)i;assert(!lock_reset_receipt(&s,"/model/proof"));checks++;}held=-1;
 active_token[0]='b';assert(!lock_reset_receipt(&s,"/model/proof"));checks++;active_token[0]='a';
 missing_token=1;assert(!lock_reset_receipt(&s,"/model/proof"));checks++;missing_token=0;
 strcpy(s.job,"/model/JOB_other");assert(!lock_reset_receipt(&s,"/model/proof"));checks++;strcpy(s.job,"/model/JOB_test");
 short_read=1;assert(!lock_reset_receipt(&s,"/model/proof"));checks++;short_read=0;
 bad_type=1;assert(!lock_reset_receipt(&s,"/model/proof"));checks++;bad_type=0;
 bad_close=1;assert(!lock_reset_receipt(&s,"/model/proof"));checks++;bad_close=0;
 printf("PASS %u actual RESET lock gates: fresh receipt accepted, every remaining lock and stale token/receipt failure rejected\n",checks);return 0;
}
