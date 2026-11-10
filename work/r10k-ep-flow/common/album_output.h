/* SPDX-License-Identifier: AGPL-3.0-only. DCF-sized, exclusive album output.
 * Only the source photo's existing DCIM directory is used; never create or
 * remove album directories. The session supervisor serializes our writers.
 */
#ifndef NXL_ALBUM_OUTPUT_H
#define NXL_ALBUM_OUTPUT_H
#include <sys/stat.h>
#include <errno.h>
static int ao_component(const char*p,size_t n){if(!n||n>128)return 0;for(size_t i=0;i<n;i++)if(!((p[i]>='A'&&p[i]<='Z')||(p[i]>='a'&&p[i]<='z')||(p[i]>='0'&&p[i]<='9')||p[i]=='_'||p[i]=='-'))return 0;return 1;}
static int ao_directory(const char*p,char*out,size_t cap){
 const char*prefix="/mnt/mmc/DCIM/";size_t n=strlen(prefix);if(!p||strncmp(p,prefix,n))return 0;
 const char*slash=strchr(p+n,'/');if(!slash||!ao_component(p+n,(size_t)(slash-p-n))||(size_t)(slash-p)>=cap)return 0;
 memcpy(out,p,(size_t)(slash-p));out[slash-p]=0;return 1;
}
static int ao_target(const char*p,int pending){
 char dir[512];if(!ao_directory(p,dir,sizeof dir))return 0;const char*f=p+strlen(dir)+1;
 if(strlen(f)!=12||strncmp(f,"NXLI",4)||strcmp(f+8,pending?".TMP":".JPG"))return 0;
 unsigned number=0;for(unsigned i=4;i<8;i++){if(f[i]<'0'||f[i]>'9')return 0;number=number*10+(unsigned)(f[i]-'0');}return number!=0;
}
static int ao_parent_real(const char*dir){
 struct stat s;return !lstat("/mnt/mmc/DCIM",&s)&&S_ISDIR(s.st_mode)&&!lstat(dir,&s)&&S_ISDIR(s.st_mode);
}
static int ao_prepare(const char*source){
 char dir[512],pending[600],final[600];if(!ao_directory(source,dir,sizeof dir)||!ao_parent_real(dir))return 0;
 const char*f=source+strlen(dir)+1;size_t n=strlen(f);if(n<5||!ao_component(f,n-4)||(strcmp(f+n-4,".SRW")&&strcmp(f+n-4,".srw")))return 0;
 struct stat st;if(lstat(source,&st)||!S_ISREG(st.st_mode))return 0;
 for(unsigned i=1;i<=9999;i++){
  snprintf(final,sizeof final,"%s/NXLI%04u.JPG",dir,i);errno=0;if(!lstat(final,&st)||errno!=ENOENT)continue;
  snprintf(pending,sizeof pending,"%s/NXLI%04u.TMP",dir,i);errno=0;if(!lstat(pending,&st)||errno!=ENOENT)continue;
  return !setenv("NX51_FINAL_PENDING",pending,1)&&!setenv("NX51_FINAL_JPEG",final,1);
 }return 0;
}
/* NXLI writers are serialized by the pipeline run.lock and the producer
 * exclusively creates TMP. Do not create a JPG placeholder: this camera's
 * exFAT rename-over-existing path leaves a second empty entry even after the
 * placeholder is closed. Publish the verified TMP to an absent JPG name.
 * The native camera uses SAM names. An existing NXLI file stops publication.
 */
static int ao_commit(const char*pending,const char*final){
 char a[512],b[512];if(!ao_target(pending,1)||!ao_target(final,0)||!ao_directory(pending,a,sizeof a)||!ao_directory(final,b,sizeof b)||strcmp(a,b)||strncmp(pending,final,strlen(final)-4)||!ao_parent_real(a))return 0;
 struct stat complete,current;if(lstat(pending,&complete)||!S_ISREG(complete.st_mode)||complete.st_size<=0)return 0;
 errno=0;if(!lstat(final,&current)||errno!=ENOENT)return 0;
 return !rename(pending,final);
}
#endif
