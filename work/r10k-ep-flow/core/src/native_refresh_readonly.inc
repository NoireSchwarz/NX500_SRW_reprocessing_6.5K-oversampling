/* SPDX-License-Identifier: AGPL-3.0-only. Explicit reversible process-RAM
 * resolution mapping. No on-disk app patch, native call or camera flash.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include "sha256.h"
#define APP "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
struct camera {int pid,mem;uint64_t start;uint32_t movie_base,user_base;};
static uint64_t ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000u+t.tv_nsec/1000000u;}
static int text(const char*p,char*b,unsigned cap){int f=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;ssize_t n=read(f,b,cap-1);int ok=n>=0&&n<(ssize_t)cap-1;if(n>=0)b[n]=0;if(close(f))ok=0;return ok;}
static uint64_t identity(int pid){char p[100],b[2048];snprintf(p,sizeof p,"/proc/%d/stat",pid);if(!text(p,b,sizeof b))return 0;char*q=strrchr(b,')');if(!q||q[1]!=' ')return 0;q+=2;for(unsigned i=3;i<22;i++){q=strchr(q,' ');if(!q)return 0;while(*q==' ')q++;}return strtoull(q,NULL,10);}
static int camera_pid(void){DIR*d=opendir("/proc");if(!d)return 0;int pid=0;struct dirent*e;while((e=readdir(d))){char*end;long p=strtol(e->d_name,&end,10);if(*end||p<=1||p>2147483647)continue;char f[100],target[256];snprintf(f,sizeof f,"/proc/%ld/exe",p);ssize_t n=readlink(f,target,sizeof target-1);if(n<0||n>=(ssize_t)sizeof target-1)continue;target[n]=0;if(!strcmp(target,APP)){if(pid){pid=0;break;}pid=(int)p;}}closedir(d);return pid;}
static int firmware(void){char b[100],ver[20],model[20];if(!text("/etc/version.info",b,sizeof b)||sscanf(b,"%19[^\r\n]%*[\r\n]%19[^\r\n]",ver,model)!=2||strcmp(ver,"1.12")||strcmp(model,"NX500"))return 0;
 FILE*f=fopen(APP,"rb");if(!f)return 0;struct sha256 h;sha256_init(&h);unsigned char buf[65536],digest[32];size_t n;uint64_t bytes=0;while((n=fread(buf,1,sizeof buf,f))){sha256_update(&h,buf,n);bytes+=n;}int ok=!ferror(f);if(fclose(f))ok=0;char hash[65];sha256_final(&h,digest);digest_hex(digest,hash);return ok&&bytes==4773516u&&!strcmp(hash,"55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04");}
static int busy(struct camera*c){
 char path[100];snprintf(path,sizeof path,"/proc/%d/fd",c->pid);DIR*d=opendir(path);if(!d)return -1;struct dirent*e;int result=0;
 while((e=readdir(d))){char*end;long fd=strtol(e->d_name,&end,10);if(*end||fd<0)continue;char target[512],info[4096];snprintf(path,sizeof path,"/proc/%d/fd/%ld",c->pid,fd);ssize_t n=readlink(path,target,sizeof target-1);if(n<0){if(errno==ENOENT)continue;result=-1;break;}if(n>=(ssize_t)sizeof target-1){result=-1;break;}target[n]=0;
  const char*ext=strrchr(target,'.');if(!ext||(strcasecmp(ext,".mp4")&&strcasecmp(ext,".srw")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))continue;
  snprintf(path,sizeof path,"/proc/%d/fdinfo/%ld",c->pid,fd);if(!text(path,info,sizeof info)){result=-1;break;}char*q=strstr(info,"flags:");unsigned flags;if(!q||sscanf(q+6,"%o",&flags)!=1){result=-1;break;}if((flags&O_ACCMODE)!=O_RDONLY){result=1;break;}
 }closedir(d);return result;
}
#define VP_MOVIE "/usr/lib/libmmutil_movie.so.0.0.0"
#define VP_USER "/usr/lib/libuser-spec.so"
#define VP_RATE_AT 0xa04u
#define VP_PREF_PHYSICAL 0x85440000u
#define VP_PREF_PAGE 4096u
static const uint32_t vp_rate70[2]={0x50ecf44fu,0x402cf2c0u};
static const uint32_t vp_rate80[2]={0x4000f24bu,0x40c4f2c0u};
/* Accept the exact previous R9q/R9r pair to migrate an already loaded 100M
 * slot; it is no longer a selectable target. All other code is pinned. */
static const uint32_t vp_rate100[2]={0x1000f24eu,0x50f5f2c0u};
static unsigned vp_preferences_valid,vp_ntsc,vp_size,vp_quality;
static uint32_t vp_pref_pointer,vp_pref_map_begin,vp_pref_map_end,vp_raw_preferences[4];
static int vp_pref_errno;
static const char*vp_preference_error="VIDEO_SETTINGS_ROOT_UNAVAILABLE";
static int vp_digest(const char*path,const char*expected){
 FILE*f=fopen(path,"rb");if(!f)return 0;struct sha256 h;sha256_init(&h);unsigned char b[32768],digest[32];size_t n;while((n=fread(b,1,sizeof b,f)))sha256_update(&h,b,n);int ok=!ferror(f);if(fclose(f))ok=0;char hash[65];sha256_final(&h,digest);digest_hex(digest,hash);return ok&&!strcmp(hash,expected);
}
static int vp_base(struct camera*c,const char*path,uint32_t*out){
 char name[100],line[512];snprintf(name,sizeof name,"/proc/%d/maps",c->pid);FILE*f=fopen(name,"r");if(!f)return 0;unsigned found=0;uint32_t base=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,offset;char perms[8],dev[32],file[300];unsigned long inode;
  if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&offset,dev,&inode,file)==7&&!offset&&!strcmp(perms,"r-xp")&&!strcmp(file,path)&&lo>0x10000u&&hi>lo&&hi-lo<0x1000000u){base=lo;found++;}
 }
 int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;if(ok)*out=base;return ok;
}
static int vp_rate_context(struct camera*c){
 if(!c->movie_base)return 1;unsigned char text[636];if(pread(c->mem,text,sizeof text,(off_t)(c->movie_base+0x818u))!=sizeof text)return 0;
 uint32_t current[2];memcpy(current,text+VP_RATE_AT-0x818u,8);if(memcmp(current,vp_rate70,8)&&memcmp(current,vp_rate80,8)&&memcmp(current,vp_rate100,8))return 0;
 memcpy(text+VP_RATE_AT-0x818u,vp_rate70,8);struct sha256 h;unsigned char digest[32];char hex[65];sha256_init(&h);sha256_update(&h,text,sizeof text);sha256_final(&h,digest);digest_hex(digest,hex);
 return !strcmp(hex,"8ce30b8dae3ae56e6aa7a0b1b25c75549f539124ca1b504bc2d974ca14f1f633");
}
static int vp_preference_mapping(struct camera*c,uint32_t pointer,uint32_t*begin,uint32_t*end){
 char name[100],line[512];snprintf(name,sizeof name,"/proc/%d/maps",c->pid);FILE*f=fopen(name,"r");if(!f)return 0;
 unsigned found=0;uint32_t b=0,e=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,offset;char perms[8],dev[32],file[300];unsigned long inode;
  if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&offset,dev,&inode,file)!=7)continue;
  if(strcmp(file,"/dev/mem")||strcmp(perms,"rw-s")||offset!=0x85400000u||lo<0x10000u||hi<=lo||(lo&4095u)||(hi&4095u))continue;
  /* Native pref_rtos_get_fd maps physical 0x85400000 for 0x80000 bytes;
   * get_virtual_address_j rounds that to 0x81000. UserSpec begins +0x40000.
   * Require the resolved native pointer, complete 38016-byte object and
   * exactly this native mapping, not an arbitrary /dev/mem alias. */
  if((uint64_t)hi-lo!=0x81000u||(uint64_t)lo+0x40000u!=pointer||(uint64_t)pointer+38016u>hi)continue;
  b=lo;e=hi;found++;
 }
 int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;if(ok){*begin=b;*end=e;}return ok;
}
static int vp_preferences(struct camera*c){
 uint32_t location,pointer,location_after,pointer_after,a[4],b[4];
 vp_preferences_valid=0;vp_pref_errno=0;vp_preference_error="VIDEO_SETTINGS_ROOT_UNAVAILABLE";
 /* _m_us_data_info has an R_ARM_COPY relocation in di-camera-app. Follow
  * the resolved GOT entry, rather than the DSO's unused local BSS copy. */
 if(!c->user_base||pread(c->mem,&location,4,(off_t)(c->user_base+0x37330u))!=4||location!=0x49d0f8u||pread(c->mem,&pointer,4,(off_t)location)!=4||pointer<0x10000u||pointer>0xbffff000u||(pointer&3u))return 0;
 vp_pref_pointer=pointer;vp_preference_error="VIDEO_SETTINGS_MAPPING_UNAVAILABLE";
 uint32_t map_begin,map_end,map_begin_after,map_end_after;
 if(!vp_preference_mapping(c,pointer,&map_begin,&map_end))return 0;
 vp_pref_map_begin=map_begin;vp_pref_map_end=map_end;
 vp_preference_error="VIDEO_SETTINGS_PHYSICAL_READ_UNAVAILABLE";
 int fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(fd<0){vp_pref_errno=errno;return 0;}
 void*view=mmap(NULL,VP_PREF_PAGE,PROT_READ,MAP_SHARED,fd,(off_t)(uint64_t)VP_PREF_PHYSICAL);int saved=errno;
 if(close(fd)){vp_pref_errno=errno;if(view!=MAP_FAILED)(void)munmap(view,VP_PREF_PAGE);return 0;}
 if(view==MAP_FAILED){vp_pref_errno=saved;return 0;}
 volatile const uint32_t*words=view;
 static const unsigned offsets[4]={0x58u,0x250u,0x254u,0x138u};
 __sync_synchronize();for(unsigned i=0;i<4;i++)a[i]=words[offsets[i]/4u];
 __sync_synchronize();for(unsigned i=0;i<4;i++)b[i]=words[offsets[i]/4u];__sync_synchronize();
 memcpy(vp_raw_preferences,a,sizeof a);
 if(munmap(view,VP_PREF_PAGE)){vp_pref_errno=errno;return 0;}
 vp_preference_error="VIDEO_SETTINGS_CHANGED_DURING_READ";
 if(pread(c->mem,&location_after,4,(off_t)(c->user_base+0x37330u))!=4||location_after!=location||pread(c->mem,&pointer_after,4,(off_t)location_after)!=4||pointer_after!=pointer||memcmp(a,b,sizeof a)||!vp_preference_mapping(c,pointer_after,&map_begin_after,&map_end_after)||map_begin_after!=map_begin||map_end_after!=map_end)return 0;
 vp_preference_error="VIDEO_SETTINGS_VALUE_OUT_OF_RANGE";if(a[0]>1||a[1]>14||a[2]>2)return 0;
 vp_ntsc=a[0]==0;vp_size=a[1];vp_quality=a[2];vp_preferences_valid=1;vp_preference_error="VIDEO_SETTINGS_READ_VERIFIED";return 1;
}
static int vp_library_context(struct camera*c,const char**why){
 *why="VIDEO_NATIVE_MOVIE_DISK_IDENTITY_FAILED";if(!vp_digest(VP_MOVIE,"269f7c69ad4448bfe01f71789132996df703b6ccecdff2a0089fd2bc9503ef3d"))return 0;
 *why="VIDEO_NATIVE_USER_DISK_IDENTITY_FAILED";if(!vp_digest(VP_USER,"2d04ab487cc9980cb87e513b25ff09011839203756d28857eedfd50a9cc730ec"))return 0;
 *why="VIDEO_NATIVE_MOVIE_MAPPING_UNAVAILABLE";if(!vp_base(c,VP_MOVIE,&c->movie_base))return 0;
 *why="VIDEO_NATIVE_USER_MAPPING_UNAVAILABLE";if(!vp_base(c,VP_USER,&c->user_base))return 0;
 *why="VIDEO_BITRATE_CONTEXT_CHANGED";if(!vp_rate_context(c))return 0;
 if(!vp_preferences(c)){*why=vp_preference_error;return 0;}return 1;
}
struct photo_spec {unsigned value;const char*name;};
static const struct photo_spec photos[]={{256,"28"},{512,"13-9"},{768,"7-1"},{1024,"3"},{1280,"23"},{1536,"11-9"},{1792,"6-2"},{2304,"2-4"},{2816,"18-7"},{3072,"9-5"},{3328,"4-7"},{3584,"2"}};
