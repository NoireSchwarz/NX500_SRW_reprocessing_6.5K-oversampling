#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <mntent.h>
#include <sys/vfs.h>
#include "src/sha256.h"

static int same(const char *a,const char *b){
 FILE *x=fopen(a,"rb"),*y=fopen(b,"rb");
 if(!x||!y){if(x)fclose(x);if(y)fclose(y);return 2;}
 unsigned char p[4096],q[4096];int rc=0;
 for(;;){size_t n=fread(p,1,sizeof p,x),m=fread(q,1,sizeof q,y);
  if(n!=m||memcmp(p,q,n)){rc=1;break;}
  if(n<sizeof p){if(ferror(x)||ferror(y))rc=2;break;}}
 fclose(x);fclose(y);return rc;
}
/* Only regular POSIX/Bash shell scripts or 32-bit little-endian ARM ELF.
   Preserve an existing shell interpreter, body, $0 and arguments. */
static int kind(const char *path){
 struct stat st;if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<10||st.st_size>16777216)return 2;
 FILE *f=fopen(path,"rb");if(!f)return 2;unsigned char h[64]={0};size_t n=fread(h,1,sizeof h,f);rewind(f);
 int rc=3;
 if(n>=20&&!memcmp(h,"\177ELF\1\1\1",7)&&h[18]==40&&h[19]==0){puts("elf");rc=0;}
 else {
  char line[128];if(fgets(line,sizeof line,f)&&
   (!strcmp(line,"#!/bin/sh\n")||!strcmp(line,"#!/bin/bash\n"))){
   int c,valid=1;while((c=fgetc(f))!=EOF)if(c==0)valid=0;
   if(valid&&!ferror(f)){puts("script");rc=0;}
  }
 }
 fclose(f);return rc;
}
static int safe_path(const char *s){
 if(s[0]!='/')return 0;
 for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||strchr("/_-.",*s)))return 0;
 return 1;
}
static int wrap(const char *input,const char *output,const char *kick,const char *backup,const char *log){
 if(!safe_path(kick)||!safe_path(backup)||!safe_path(log))return 3;
 FILE *f=fopen(input,"rb");if(!f)return 2;unsigned char h[20]={0};size_t n=fread(h,1,20,f);rewind(f);
 int elf=n==20&&!memcmp(h,"\177ELF\1\1\1",7)&&h[18]==40&&h[19]==0;
 char first[128];
 if(!elf&&(!fgets(first,sizeof first,f)||(strcmp(first,"#!/bin/sh\n")&&strcmp(first,"#!/bin/bash\n")))){fclose(f);return 3;}
 FILE *o=fopen(output,"wb");if(!o){fclose(f);return 2;}
 fputs(elf?"#!/bin/sh\n":first,o);
 fprintf(o,"# NXISP_FULL_HOOK_BEGIN_1_2_2\n(\n    printf 'BT_HOOK_ENTRY version=1.2.2 pid=%%s\\n' \"$$\"\n    if [ -r '%s' ]; then\n        _nxisp_rc=0\n        /bin/sh '%s' || _nxisp_rc=$?\n        printf 'BT_HOOK_EXIT code=%%s\\n' \"$_nxisp_rc\"\n    else\n        printf 'BT_KICK_MISSING\\n'\n    fi\n) >> '%s' 2>&1 || :\n# NXISP_FULL_HOOK_END_1_2_2\n",kick,kick,log);
 if(elf)fprintf(o,"exec '%s' \"$@\"\n",backup);
 else{int c;while((c=fgetc(f))!=EOF)fputc(c,o);}
 int bad=ferror(f)||ferror(o);fclose(f);if(fclose(o))bad=1;return bad?2:0;
}
static int volatile_fs(const char *path){
 struct statfs s;if(statfs(path,&s))return 2;
 unsigned long type=(unsigned long)s.f_type;
 /* Use verified RAM filesystems for the installation-session marker;
    keep boot_id as diagnostic data rather than the only restart criterion. */
 return type==0x01021994ul||type==0x858458f6ul?0:1;
}
/* Longest matching mounted path; the last equal path wins. /proc/mounts can
   contain both rootfs and the real root. No shell evaluation of mount text. */
static int mount_info(const char *table,const char *path){
 FILE *f=setmntent(table,"r");if(!f)return 2;struct mntent *e;char best[1024]="";int ro=0;
 while((e=getmntent(f))){size_t l=strlen(e->mnt_dir);
  if(l>=sizeof best||l<strlen(best)||strncmp(path,e->mnt_dir,l))continue;
  if(l!=1&&path[l]&&path[l]!='/')continue;
  if(!safe_path(e->mnt_dir))continue;
  strcpy(best,e->mnt_dir);ro=hasmntopt(e,"ro")!=NULL;}
 endmntent(f);if(!*best)return 3;printf("%s %s\n",best,ro?"ro":"rw");return 0;
}
/* 0 supported, 1 explicit mismatch, 2 no authoritative record. */
static int version(const char *path){
 FILE *f=fopen(path,"r");if(!f)return 2;char s[256];int model=0,ver=0,wrong=0;
 while(fgets(s,sizeof s,f)){s[strcspn(s,"\r\n")]=0;
  if(!strcmp(s,"NX500"))model=1;
  else if(!strncmp(s,"NX",2))wrong=1;
  if(!strcmp(s,"1.12"))ver=1;
 }
 int bad=ferror(f);fclose(f);if(bad)return 2;
 if(model&&ver)return 0;
 return model||wrong?1:2;
}
/* Bundled digest avoids a dependency on camera sha256sum. Do not hash
   devices or symlinks while establishing package identities. */
static int file_sha256(const char *path){
 struct stat st;if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>128u*1024u*1024u)return 2;
 FILE *f=fopen(path,"rb");if(!f)return 2;struct sha256 sha;sha256_init(&sha);
 unsigned char buf[8192],digest[32];size_t n;while((n=fread(buf,1,sizeof buf,f)))sha256_update(&sha,buf,n);
 int bad=ferror(f);if(fclose(f))bad=1;if(bad)return 2;
 char hex[65];sha256_final(&sha,digest);digest_hex(digest,hex);puts(hex);return 0;
}
int main(int argc,char **v){
 if(argc==3&&!strcmp(v[1],"sha256"))return file_sha256(v[2]);
 if(argc==4&&!strcmp(v[1],"same"))return same(v[2],v[3]);
 if(argc==3&&!strcmp(v[1],"kind"))return kind(v[2]);
 if(argc==7&&!strcmp(v[1],"wrap"))return wrap(v[2],v[3],v[4],v[5],v[6]);
 if(argc==3&&!strcmp(v[1],"volatile-fs"))return volatile_fs(v[2]);
 if(argc==4&&!strcmp(v[1],"mount-info"))return mount_info(v[2],v[3]);
 if(argc==3&&!strcmp(v[1],"version"))return version(v[2]);
 if(argc==3&&!strcmp(v[1],"unlock"))return rmdir(v[2])==0||errno==ENOENT?0:1;
 fprintf(stderr,"fileops: sha256 PATH | same A B | kind PATH | wrap IN OUT KICK BACKUP LOG | volatile-fs DIR | mount-info TABLE PATH | version PATH | unlock DIR\n");return 64;
}
