/* SPDX-License-Identifier: AGPL-3.0-only
 * R7n installer/rearm: read workflow identity or exact legacy OEM lock bytes.
 * Does not delete locks, signal processes, touch ISP, or certify hardware idle.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/src/sha256.h"
#include "../../common/workflow_hold.h"

static const char *run_root="/tmp/nx-lut-lite", *proc_root="/proc";
static const char *locks[]={"engine.lock","trace.lock","memory-test.lock","demo50-recovery.lock"};
static const char *leaves[]={"NXOEM_OWNER","pid","RESTART_REQUIRED","NX51_OWNER","NX51_ROUND"};
static const char *reason="UNSET";
static int bad(const char *s){reason=s;return -1;}
static int directory(const char *p){return open(p,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);}
static int read_at(int fd,const char *name,char *out,size_t cap){
    int f=openat(fd,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(f<0)return -1;
    struct stat st;
    if(fstat(f,&st)||!S_ISREG(st.st_mode)){close(f);return -1;}
    size_t n=0;
    while(n<cap){ssize_t k=read(f,out+n,cap-n);if(k<0&&errno==EINTR)continue;
        if(k<0){close(f);return -1;}if(!k){close(f);return (int)n;}n+=(size_t)k;}
    close(f);return -1;
}
static int exact_at(int fd,const char *name,const char *want){
    char b[512];int n=read_at(fd,name,b,sizeof b);
    return n>=0&&(size_t)n==strlen(want)&&!memcmp(b,want,(size_t)n);
}
static int owner_at(int fd,char pid[12]){
    char b[128],canonical[128];int n=read_at(fd,"NXOEM_OWNER",b,sizeof b);
    const size_t head=sizeof("NXOEMLOCK1\npid=")-1;
    if(n<(int)head+2||memcmp(b,"NXOEMLOCK1\npid=",head)||b[n-1]!='\n')return bad("OEM_OWNER_BYTES");
    size_t digits=(size_t)n-head-1;
    if(digits>10||b[head]=='0')return bad("OEM_PID_NONCANONICAL");
    unsigned long long value=0;
    for(size_t i=0;i<digits;i++){
        unsigned c=(unsigned char)b[head+i];if(c<'0'||c>'9')return bad("OEM_PID_NONCANONICAL");
        value=value*10+(c-'0');}
    if(value<2||value>2147483647ULL)return bad("OEM_PID_RANGE");
    snprintf(pid,12,"%llu",value);snprintf(canonical,sizeof canonical,"NXOEMLOCK1\npid=%s\n",pid);
    if((size_t)n!=strlen(canonical)||memcmp(b,canonical,(size_t)n))return bad("OEM_OWNER_BYTES");
    return 0;
}
static int pair_member(int run,const char *name,char pid[12]){
    int d=openat(run,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(d<0)return bad("OEM_PAIR_DIRECTORY");
    DIR *ls=fdopendir(dup(d));if(!ls){close(d);return bad("OEM_DIRECTORY_UNREADABLE");}
    int owner=0,leafpid=0,hold=0,unexpected=0;struct dirent *e;
    errno=0;
    while((e=readdir(ls))){
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        if(!strcmp(e->d_name,"NXOEM_OWNER"))owner++;
        else if(!strcmp(e->d_name,"pid"))leafpid++;
        else if(!strcmp(e->d_name,"RESTART_REQUIRED"))hold++;
        else unexpected=1;
    }
    if(errno)unexpected=1;closedir(ls);
    if(unexpected||owner!=1||leafpid+hold>1){
        printf("OEM_MEMBER_REJECTED name=%s owner=%d pid=%d restart=%d unexpected=%d\n",name,owner,leafpid,hold,unexpected);
        close(d);return bad("OEM_PAIR_LEAVES");
    }
    if(owner_at(d,pid)){close(d);return -1;}
    /* Owner-only is a partial manual-lock cleanup state seen in the field.
     * Both directories must still have the exact same OEM owner; process
     * liveness/identity is checked for the entire pair before acceptance.
     * This permits replacing program files only, never retiring the hold.
     */
    if(!leafpid&&!hold){
        printf("OEM_MEMBER name=%s shape=OWNER_ONLY owner_pid=%s holds_preserved=1\n",name,pid);
        close(d);return 0;
    }
    char expected[32];snprintf(expected,sizeof expected,"%s\n",pid);
    int ok=leafpid?exact_at(d,"pid",expected):exact_at(d,"RESTART_REQUIRED","NXOEMHOLD1\nreason=OEM_RESTORE_NOT_VERIFIED\n");
    close(d);return ok?0:bad("OEM_LEAF_BYTES");
}
static int start_ticks(int pfd,const char *pid,char ticks[32]){
    char b[4096];int n=read_at(pfd,"stat",b,sizeof b-1);
    if(n<=0||memchr(b,0,(size_t)n))return -1;b[n]=0;
    size_t p=strlen(pid);if(strncmp(b,pid,p)||b[p]!=' '||b[p+1]!='(')return -1;
    char *r=strrchr(b,')');if(!r||r[1]!=' ')return -1;
    char *s=r+2,*end;
    for(int field=3;field<=22;field++){
        while(*s==' ')s++;end=s;while(*end&&*end!=' '&&*end!='\n')end++;
        if(end==s)return -1;
        if(field==22){size_t k=(size_t)(end-s);if(k>=32)return -1;
            for(size_t i=0;i<k;i++)if(s[i]<'0'||s[i]>'9')return -1;
            memcpy(ticks,s,k);ticks[k]=0;return 0;}
        s=end;
    }
    return -1;
}
static int native_name(const char *s){
    return !strncmp(s,"nxl-",4)||!strncmp(s,"nx-lut",6)||!strncmp(s,"nx_isp",6)||!strncmp(s,"nx-isp",6);
}
static int safe_text(const char *s){
    if(!*s)return 0;
    for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s>126)return 0;
    return 1;
}
static void hex(const void *p,size_t n){const unsigned char *s=p;for(size_t i=0;i<n;i++)printf("%02x",s[i]);}
static int owner_identity(const char *pid){
    int proc=directory(proc_root);if(proc<0)return bad("PROC_UNAVAILABLE");
#ifndef HOLD_HOST_MODEL
    struct statfs fs;
    if(fstatfs(proc,&fs)||fs.f_type!=0x9fa0){close(proc);return bad("PROC_NOT_KERNEL");}
#endif
    char self[4096];int n=read_at(proc,"self/status",self,sizeof self);
    if(n<7||memcmp(self,"Name:\t",6)){close(proc);return bad("PROC_STATUS_UNAVAILABLE");}
    struct stat before;
    if(fstatat(proc,pid,&before,AT_SYMLINK_NOFOLLOW)){
        int absent=errno==ENOENT;close(proc);
        if(!absent)return bad("OWNER_STAT_UNAVAILABLE");
        printf("OWNER_IDENTITY pid=%s state=ABSENT\n",pid);return 0;
    }
    if(!S_ISDIR(before.st_mode)){close(proc);return bad("OWNER_NOT_PROCESS_DIRECTORY");}
    int pfd=openat(proc,pid,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(pfd<0){close(proc);return bad("OWNER_IDENTITY_UNAVAILABLE");}
    char t1[32],t2[32],comm[128],exe[4096];int rc=-1;
    if(start_ticks(pfd,pid,t1))goto unreadable;
    n=read_at(pfd,"comm",comm,sizeof comm-1);
    if(n<=1||comm[n-1]!='\n'||memchr(comm,0,(size_t)n))goto unreadable;
    comm[n-1]=0;
    ssize_t k=readlinkat(pfd,"exe",exe,sizeof exe-1);
    if(k<=0||k>=(ssize_t)sizeof exe-1)goto unreadable;exe[k]=0;
    if(!safe_text(comm)||!safe_text(exe)||exe[0]!='/')goto unreadable;
    struct stat after;
    if(start_ticks(pfd,pid,t2)||strcmp(t1,t2)||fstatat(proc,pid,&after,AT_SYMLINK_NOFOLLOW)||
       !S_ISDIR(after.st_mode)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino){reason="OWNER_IDENTITY_CHANGED";goto out;}
    printf("OWNER_IDENTITY pid=%s start_ticks=%s comm_hex=",pid,t1);hex(comm,strlen(comm));
    printf(" exe_hex=");hex(exe,strlen(exe));putchar('\n');
    const char *base=strrchr(exe,'/');base=base?base+1:exe;
    if(native_name(comm)||native_name(base)){reason="OWNER_IS_LIVE_MODULE_PROCESS";goto out;}
    printf("OWNER_IDENTITY pid=%s state=UNRELATED_PID_REUSE control_plane_only=1\n",pid);
    rc=0;goto out;
unreadable:reason="OWNER_IDENTITY_UNAVAILABLE";
out:close(pfd);close(proc);return rc;
}
static int check_oem(void){
    int run=directory(run_root);if(run<0)return bad("RUNTIME_DIRECTORY_UNAVAILABLE");
    struct stat st;
    for(unsigned i=2;i<4;i++){
        if(!fstatat(run,locks[i],&st,AT_SYMLINK_NOFOLLOW)){close(run);return bad("OTHER_EXPERIMENT_HOLD");}
        if(errno!=ENOENT){close(run);return bad("OTHER_HOLD_UNREADABLE");}
    }
    char a[12],b[12];
    int rc=pair_member(run,locks[0],a)||pair_member(run,locks[1],b);
    close(run);if(rc)return -1;
    if(strcmp(a,b))return bad("OEM_OWNER_PAIR_MISMATCH");
    printf("OEM_PAIR owner_pid=%s exact_bytes=1\n",a);
    if(owner_identity(a))return -1;
    printf("INSTALL_OEM_PAIR_ACCEPTED owner_pid=%s locks_preserved=1 control_plane_only=1 hardware_idle_proof=0\n",a);
    return 0;
}
static void inventory(void){
    int run=directory(run_root);
    if(run<0){printf("HOLD_INVENTORY runtime=UNREADABLE errno=%d\n",errno);return;}
    for(unsigned i=0;i<4;i++){
        struct stat st;
        if(fstatat(run,locks[i],&st,AT_SYMLINK_NOFOLLOW)){
            printf("HOLD_PATH name=%s state=%s errno=%d\n",locks[i],errno==ENOENT?"ABSENT":"UNREADABLE",errno);continue;}
        printf("HOLD_PATH name=%s type=%o inode=%llu\n",locks[i],(unsigned)(st.st_mode&S_IFMT),(unsigned long long)st.st_ino);
        int d=openat(run,locks[i],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(d<0)continue;
        for(unsigned j=0;j<sizeof leaves/sizeof *leaves;j++){
            char b[512];int n=read_at(d,leaves[j],b,sizeof b);
            if(n>=0){unsigned char digest[32];char sha[65];digest_bytes(b,(size_t)n,digest);digest_hex(digest,sha);
                printf("HOLD_LEAF path=%s/%s bytes=%d sha256=%s content_hex=",locks[i],leaves[j],n,sha);hex(b,(size_t)n);putchar('\n');}
            else printf("HOLD_LEAF path=%s/%s state=ABSENT_OR_NOT_BOUNDED_REGULAR\n",locks[i],leaves[j]);
        }
        DIR *ls=fdopendir(dup(d));
        if(ls){struct dirent *e;unsigned seen=0;
            while((e=readdir(ls))&&seen<32){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
                printf("HOLD_ENTRY dir=%s name_hex=",locks[i]);hex(e->d_name,strlen(e->d_name));putchar('\n');seen++;}
            closedir(ls);}
        close(d);
    }
    close(run);
}
static int check_nx51(const char*expected){
 unsigned seen=0;for(unsigned i=0;i<4;i++){char path[1200];struct wh_hold h;snprintf(path,sizeof path,"%s/%s",run_root,locks[i]);
  if(!wh_check(path,expected,&h)){printf("HOLD_DECISION state=BLOCKED reason=LIVE_FOREIGN_OR_MALFORMED_NX51_HOLD locks_changed=0\n");return 20;}
  if(h.present){seen++;printf("NX51_HOLD name=%s job=%s round=%u reason=%s preserved=1\n",locks[i],h.job,h.round,h.reason);}
 }
 printf("HOLD_DECISION state=ACCEPT_CONTROL_PLANE_ONLY holds=%u locks_changed=0 hardware_idle_claim=0\n",seen);return 0;
}
int main(int argc,char **argv){
#ifndef HOLD_HOST_MODEL
 if((argc==2||argc==3)&&!strcmp(argv[1],"--check-nx51-pair"))return check_nx51(argc==3?argv[2]:NULL);
#endif

#ifdef HOLD_HOST_MODEL
    if(argc!=4)return 64;run_root=argv[2];proc_root=argv[3];
#else
    if(argc!=2)return 64;
#endif
    if(!strcmp(argv[1],"--version")){puts("NX51_INSTALL_HOLDCHECK_R7N1 resource_identity=1 read_only=1 hardware_access=0");return 0;}
    if(strcmp(argv[1],"--check-oem-pair")&&strcmp(argv[1],"--inventory"))return 64;
    printf("NX51_INSTALL_HOLDCHECK_R7N1 operation=%s read_only=1\n",argv[1]);
    if(!strcmp(argv[1],"--inventory")){inventory();return 0;}
    int rc=check_oem();
    printf("HOLD_DECISION state=%s reason=%s locks_changed=0\n",rc?"BLOCKED":"ACCEPT_CONTROL_PLANE_ONLY",rc?reason:"EXACT_OEM_OWNER_INACTIVE");
    return rc?20:0;
}
