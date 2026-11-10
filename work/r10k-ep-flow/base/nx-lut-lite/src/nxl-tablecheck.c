/* SPDX-License-Identifier: AGPL-3.0-only
 * Read-only cache verification, no camera devices or settings are accessed. */
#include <asm/unistd.h>
#include <fcntl.h>
#define SLOT 19712u
#define TABLE 19652u
static unsigned char original[SLOT],buffer[SLOT];
static int sc3(int n,int a,int b,int c) {
    register int r0 __asm__("r0")=a,r1 __asm__("r1")=b,r2 __asm__("r2")=c,r7 __asm__("r7")=n;
    __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory","cc");return r0;
}
static int same(const char *a,const char *b) {while(*a && *a==*b){++a;++b;}return *a==*b;}
static void say(const char *s) {unsigned n=0;while(s[n])++n;sc3(__NR_write,1,(int)s,n);}
static int read_exact(const char *path,unsigned char *p) {
    int fd=sc3(__NR_open,(int)path,O_RDONLY,0);if(fd<0)return 0;
    unsigned done=0;int n;
    while(done<SLOT) {
        n=sc3(__NR_read,fd,(int)(p+done),SLOT-done);if(n==-4)continue;
        if(n<=0){sc3(__NR_close,fd,0,0);return 0;}done+=(unsigned)n;
    }
    unsigned char extra;do {n=sc3(__NR_read,fd,(int)&extra,1);}while(n==-4);
    sc3(__NR_close,fd,0,0);return n==0;
}
static int valid(const unsigned char *p) {
    for(unsigned i=3;i<TABLE;i+=4)if(p[i])return 0;
    for(unsigned i=0;i<12;++i)if(p[TABLE+i]!=p[TABLE-4+(i&3)])return 0;
    for(unsigned i=TABLE+12;i<SLOT;++i)if(p[i])return 0;
    return 1;
}
int nx_main(unsigned *stack) {
    int argc=(int)stack[0];char **argv=(char **)(stack+1);
    if(argc!=6 || !same(argv[1],"check")){say("ERR usage: tablecheck check BASE REFERENCE VARIANT FNV32\n");return 1;}
    unsigned expected=0;
    for(unsigned i=0;i<8;++i) {
        unsigned c=(unsigned char)argv[5][i],v;
        if(c>='0' && c<='9')v=c-'0';else if(c>='a' && c<='f')v=c-'a'+10;
        else {say("ERR invalid checksum\n");return 1;}
        expected=(expected<<4)|v;
    }
    if(argv[5][8]){say("ERR invalid checksum length\n");return 1;}
    if(!read_exact(argv[2],original) || !valid(original) || !read_exact(argv[3],buffer)) {
        say("ERR invalid source/reference file\n");return 1;
    }
    for(unsigned i=0;i<SLOT;++i)if(original[i]!=buffer[i]) {
        say("ERR strength cache is stale; original LUT changed\n");return 1;
    }
    if(!read_exact(argv[4],buffer) || !valid(buffer)){say("ERR invalid strength table\n");return 1;}
    unsigned hash=2166136261u;
    for(unsigned i=0;i<SLOT;++i)hash=(hash^buffer[i])*16777619u;
    if(hash!=expected){say("ERR strength checksum mismatch\n");return 1;}
    say("OK strength source and variant verified\n");return 0;
}
__asm__(".global _start\n_start:\nmov r0, sp\nbl nx_main\nmov r7, #1\nsvc 0\n");
