/* SPDX-License-Identifier: AGPL-3.0-only. Actual production pin verifier with
 * modeled read-only file descriptors containing the exact camera evidence. */
#define open model_open
#define read model_read
#define close model_close
#define fstat model_fstat
#define lstat model_lstat
#define main production_session_main_not_run
#include "../src/session.c"
#undef main
#undef open
#undef read
#undef close
#undef fstat
#undef lstat
#include <assert.h>
#include "fixtures/r7d-preflight-bytes.h"
static unsigned offsets[8],opens;
static int mutate=-1,short_read,close_failed,parent_link;
int model_open(const char *path,int flags,...){
 assert((flags&(O_NOFOLLOW|O_CLOEXEC))==(O_NOFOLLOW|O_CLOEXEC));
 assert((flags&O_ACCMODE)==O_RDONLY);
 const char *base="/model/NXDEMO51/JOB_test/";assert(!strncmp(path,base,strlen(base)));
 for(unsigned i=0;i<8;i++)if(!strcmp(path+strlen(base),preflight_pins[i].leaf)){offsets[i]=0;opens++;return 100+(int)i;}
 return -1;
}
int model_lstat(const char *path,struct stat *st){(void)path;memset(st,0,sizeof *st);st->st_mode=parent_link?S_IFLNK:S_IFDIR;return 0;}
int model_fstat(int fd,struct stat *st){unsigned i=(unsigned)(fd-100);assert(i<8);memset(st,0,sizeof *st);st->st_mode=S_IFREG;st->st_size=legacy_sizes[i]+((int)i==mutate&&!legacy_sizes[i]);return 0;}
ssize_t model_read(int fd,void *out,size_t n){unsigned i=(unsigned)(fd-100);assert(i<8);if(short_read)return 0;if(n>legacy_sizes[i]-offsets[i])n=legacy_sizes[i]-offsets[i];memcpy(out,legacy_bytes[i]+offsets[i],n);if((int)i==mutate&&n)((unsigned char*)out)[0]^=1;offsets[i]+=(unsigned)n;return (ssize_t)n;}
int model_close(int fd){assert(fd>=100&&fd<108);return close_failed?-1:0;}
static struct session fixture_session(void){struct session s={0};s.seq=5;s.front_attempt=1;s.requires_reset=1;s.strength=100;strcpy(s.job,"/model/NXDEMO51/JOB_test");strcpy(s.state,"RESTART_REQUIRED");strcpy(s.front,"-");strcpy(s.stage,"front");strcpy(s.resume,"front");strcpy(s.reason,"FRONT_FAILED_NATIVE_STATE_UNVERIFIED");strcpy(s.outcome,"UNKNOWN_REQUIRE_RESTART");strcpy(s.look,"raw422");return s;}
int main(void){unsigned checks=0;struct session s=fixture_session();assert(preflight_legacy(&s)&&opens==8);checks++;
 for(unsigned i=0;i<8;i++){mutate=(int)i;assert(!preflight_legacy(&s));checks++;}mutate=-1;
 short_read=1;assert(!preflight_legacy(&s));checks++;short_read=0;
 close_failed=1;assert(!preflight_legacy(&s));checks++;close_failed=0;
 parent_link=1;assert(!preflight_legacy(&s));checks++;parent_link=0;
 for(unsigned i=0;i<13;i++){
  s=fixture_session();if(i==0)s.seq++;if(i==1)s.front_attempt++;if(i==2)s.backend_attempt=1;
  if(i==3)s.requires_reset=0;if(i==4)s.verified=1;if(i==5)strcpy(s.state,"FRONT_RUNNING");
  if(i==6)strcpy(s.front,"/some/old/front");if(i==7)strcpy(s.stage,"backend");if(i==8)strcpy(s.resume,"backend");
  if(i==9)strcpy(s.reason,"ORPHANED_RUNNING_STATE");if(i==10)strcpy(s.outcome,"SAFE_RELEASED_COLD");
  if(i==11)strcpy(s.look,"other");if(i==12)s.strength=75;
  unsigned before=opens;assert(!preflight_legacy(&s)&&opens==before);checks++;
 }
 printf("PASS %u actual legacy verifier cases: exact field bytes accepted; changed evidence, partial reads, close errors, symlinks and incompatible states rejected\n",checks);return 0;
}
