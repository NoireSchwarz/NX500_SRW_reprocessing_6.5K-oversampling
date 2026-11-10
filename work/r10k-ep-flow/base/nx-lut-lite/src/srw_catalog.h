/* SPDX-License-Identifier: AGPL-3.0-only
 * Read-only source selection. No decompression, ISP calls or image writes.
 */
#ifndef NXL_SRW_CATALOG_H
#define NXL_SRW_CATALOG_H
#include <dirent.h>
#if defined(__arm__)
/* NX500 libc 2.13 exports __[lf]xstat, not modern lstat/fstat symbols.
 * Native __xstat_conv accepts version 3 and writes this ARM32 layout. */
#include <stddef.h>
_Static_assert(sizeof(struct stat)==88,"NX500 stat ABI size");
_Static_assert(offsetof(struct stat,st_mode)==16,"NX500 stat ABI mode");
_Static_assert(offsetof(struct stat,st_size)==44,"NX500 stat ABI size field");
extern int __lxstat(int,const char *,struct stat *);
extern int __fxstat(int,int,struct stat *);
static int srw_lstat(const char *p,struct stat *s) {return __lxstat(3,p,s);}
static int srw_fstat(int fd,struct stat *s) {return __fxstat(3,fd,s);}
#else
static int srw_lstat(const char *p,struct stat *s) {return lstat(p,s);}
static int srw_fstat(int fd,struct stat *s) {return fstat(fd,s);}
#endif
#define NXL_SRW_MAX 1024
static char srw_files[NXL_SRW_MAX][512],srw_source[512];
static int srw_count,srw_cursor,srw_file_mode,srw_row,srw_scan_limited;
static unsigned srw_scanned;
static int srw_suffix(const char *s) {
    size_t n=strlen(s);
    return n>4 && s[n-4]=='.' && (s[n-3]=='s'||s[n-3]=='S') &&
        (s[n-2]=='r'||s[n-2]=='R') && (s[n-1]=='w'||s[n-1]=='W');
}
static int srw_compare(const void *a,const void *b) { return strcmp(a,b); }
static void srw_scan_dir(const char *path,int depth) {
    DIR *dir=opendir(path);if(!dir)return;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        if(++srw_scanned>16384 || srw_count==NXL_SRW_MAX) {srw_scan_limited=1;break;}
        if(entry->d_name[0]=='.')continue;
        char file[512];struct stat st;
        int n=snprintf(file,sizeof file,"%s/%s",path,entry->d_name);
        if(n<0 || (unsigned)n>=sizeof file || srw_lstat(file,&st))continue;
        if(S_ISDIR(st.st_mode) && depth)srw_scan_dir(file,depth-1);
        else if(S_ISREG(st.st_mode) && srw_suffix(entry->d_name))strcpy(srw_files[srw_count++],file);
        if(srw_scan_limited)break;
    }
    closedir(dir);
}
static void srw_scan(void) {
    srw_count=srw_cursor=srw_scan_limited=0;srw_scanned=0;
    srw_scan_dir("/mnt/mmc/DCIM",1);
    qsort(srw_files,(size_t)srw_count,sizeof srw_files[0],srw_compare);
    if(srw_count)srw_cursor=srw_count-1;
    for(int i=0;i<srw_count;++i)if(!strcmp(srw_files[i],srw_source))srw_cursor=i;
}
static const char *srw_basename(const char *s) {
    const char *p=strrchr(s,'/');return p?p+1:s;
}
/* Header/container checks only: not proof of Bayer data or successful decode. */
static int srw_container_check(const char *path) {
    if(!path[0])return 0;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return 0;
    struct stat st;unsigned char b[8];
    int ok=!srw_fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_size>=8 && read(fd,b,8)==8;
    close(fd);if(!ok)return 0;
    unsigned offset;
    if(b[0]=='I' && b[1]=='I' && b[2]==42 && !b[3])
        offset=(unsigned)b[4]|((unsigned)b[5]<<8)|((unsigned)b[6]<<16)|((unsigned)b[7]<<24);
    else if(b[0]=='M' && b[1]=='M' && !b[2] && b[3]==42)
        offset=((unsigned)b[4]<<24)|((unsigned)b[5]<<16)|((unsigned)b[6]<<8)|b[7];
    else return 0;
    return offset>=8 && (unsigned long long)offset+2<=(unsigned long long)st.st_size;
}
#endif
