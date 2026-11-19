/* SPDX-License-Identifier: AGPL-3.0-only. Linux-only passive RAM observation.
 * No ptrace, native calls, allocator transactions, RTOS or MMIO writes. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "../../common/color_observation.h"
#include "../../common/observer_usb.h"
#define PRIVATE "/tmp/nx-lut-lite"
#define CURRENT PRIVATE "/color-observe.current"
#define LOCK PRIVATE "/color-observe.lock"
#define RUN_MAGIC 0x314e5243u
enum {OFF=0,ON=1,WAIT=2,PENDING=3,FULL=4,SAVED=5,FAILED=6};
struct run {uint32_t magic,state,samples,valid,attempt;char ram[160],report[160];};
static volatile sig_atomic_t interrupted;
static void interrupted_by(int sig){(void)sig;interrupted=1;}
static uint64_t stamp(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
static int write_all(int f,const void*p,size_t n){const char*b=p;while(n){ssize_t k=write(f,b,n);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;b+=k;n-=k;}return 1;}
static int regular(int f){struct stat s;return !fstat(f,&s)&&S_ISREG(s.st_mode)&&s.st_nlink==1;}
static int private_dir(const char*p){struct stat s;return !lstat(p,&s)&&S_ISDIR(s.st_mode)&&s.st_uid==0&&!(s.st_mode&0022);}
static int path_ok(const char*p,const char*prefix){size_t n=strlen(prefix);return !strncmp(p,prefix,n)&&p[n]&&strspn(p+n,"0123456789_")==strlen(p+n);}
static int read_run(struct run*s){
 memset(s,0,sizeof*s);int f=open(CURRENT,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return errno==ENOENT?1:0;
 struct stat st;int ok=regular(f)&&!fstat(f,&st)&&st.st_size==sizeof*s&&read(f,s,sizeof*s)==sizeof*s;if(close(f))ok=0;
 return ok&&s->magic==RUN_MAGIC&&s->state<=FAILED&&s->samples<=CO_LIMIT&&s->valid<=s->samples&&memchr(s->ram,0,sizeof s->ram)&&memchr(s->report,0,sizeof s->report)&&path_ok(s->ram,PRIVATE "/color_")&&path_ok(s->report,"/mnt/mmc/NXDEMO51/COLOR_R11L_");
}
static int save_run(const struct run*s){char p[256];snprintf(p,sizeof p,CURRENT ".new.%u",(unsigned)getpid());int f=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 0;int ok=write_all(f,s,sizeof*s);if(close(f))ok=0;if(!ok)return 0;return !rename(p,CURRENT);}
static void response(const struct run*s,int running,const char*reason){unsigned state=s->state;if(!running&&(state==ON||state==WAIT||state==FULL))state=PENDING;
 printf("NXL_COLOR_OBSERVE1\nstate=%u\nsamples=%u\nvalid=%u\nreason=%s\nend=NXL_COLOR_OBSERVE_END1\n",state,s->samples,s->valid,reason);
}
/* Mapping cache contains ordinary firmware RAM only; never a device window. */
struct page {uint32_t at;unsigned char*map;};
struct reader {int fd;unsigned next;struct page page[64];};
static int ram_read(void*context,uint32_t at,void*out,unsigned bytes){
 struct reader*r=context;int normal=at>=0x80000000u&&(uint64_t)at+bytes<=0x94000000ULL;
 int shadow=(at>=0xbb71d200u&&(uint64_t)at+bytes<=0xbb71d3d8ULL)||(at>=0xbb71f200u&&(uint64_t)at+bytes<=0xbb71f3d8ULL);
 if(!bytes||bytes>2048||(!normal&&!shadow))return 0;
 if(shadow){uint32_t pte,bases[2];if(!ram_read(r,0x81002edc,&pte,4)||pte!=0xbb701402u||!ram_read(r,0x813ebf50,bases,8)||bases[0]!=0xbb71b300u||bases[1]!=0xbb71d300u)return 0;}
 unsigned char*d=out;
 while(bytes){uint32_t page=at&~4095u;unsigned i;for(i=0;i<64;i++)if(r->page[i].map&&r->page[i].at==page)break;
  if(i==64){i=r->next++%64;if(r->page[i].map)munmap(r->page[i].map,4096);r->page[i].map=0;
   void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,r->fd,(off_t)(uint64_t)page);if(p==MAP_FAILED)return 0;r->page[i].map=p;r->page[i].at=page;}
  unsigned n=4096-(at&4095u);if(n>bytes)n=bytes;const volatile unsigned char*p=r->page[i].map+(at&4095u);for(unsigned k=0;k<n;k++)d[k]=p[k];
  __asm__ volatile("dmb sy":::"memory");at+=n;d+=n;bytes-=n;
 }return 1;
}
static int find_camera(void){DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;int pid=0;while((e=readdir(d))){if(!*e->d_name||strspn(e->d_name,"0123456789")!=strlen(e->d_name))continue;char p[320],b[64];snprintf(p,sizeof p,"/proc/%s/comm",e->d_name);FILE*f=fopen(p,"r");if(!f)continue;int ok=fgets(b,sizeof b,f)!=NULL;fclose(f);if(ok&&!strcmp(b,"di-camera-app\n")){if(pid){pid=0;break;}pid=atoi(e->d_name);}}closedir(d);return pid>1?pid:0;}
static int media_busy(int pid,char name[128]){
 char path[160];name[0]=0;if(pid<2)return -1;snprintf(path,sizeof path,"/proc/%d/fd",pid);DIR*d=opendir(path);if(!d)return -1;struct dirent*e;int result=0;
 while((e=readdir(d))){char*end;long fd=strtol(e->d_name,&end,10);if(*end||fd<0)continue;char target[512],info[512];snprintf(path,sizeof path,"/proc/%d/fd/%ld",pid,fd);ssize_t n=readlink(path,target,sizeof target-1);if(n<0){if(errno==ENOENT)continue;result=-1;break;}if(n>=(ssize_t)sizeof target-1){result=-1;break;}target[n]=0;char*ext=strrchr(target,'.');if(!ext||(strcasecmp(ext,".mp4")&&strcasecmp(ext,".srw")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))continue;
  snprintf(path,sizeof path,"/proc/%d/fdinfo/%ld",pid,fd);FILE*f=fopen(path,"r");if(!f){result=-1;break;}size_t got=fread(info,1,sizeof info-1,f);int ok=!ferror(f)&&got<sizeof info-1;if(fclose(f))ok=0;info[got]=0;char*q=strstr(info,"flags:");unsigned flags;if(!ok||!q||sscanf(q+6,"%o",&flags)!=1){result=-1;break;}
  if((flags&O_ACCMODE)!=O_RDONLY){result=1;const char*base=strrchr(target,'/');base=base?base+1:target;if(strlen(base)<128)strcpy(name,base);else strcpy(name,"NAME_TOO_LONG");}
 }closedir(d);return result;
}
static int export_records(struct run*s){
 char input[256],output[256];snprintf(input,sizeof input,"%s/records.bin",s->ram);int in=open(input,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(in<0)return 0;struct stat st;int ok=regular(in)&&!fstat(in,&st)&&st.st_size==(off_t)s->samples*sizeof(struct co_record);if(!ok){close(in);return 0;}
 int out=-1;while(s->attempt<32){s->attempt++;snprintf(output,sizeof output,"%s/records-%u.bin",s->report,s->attempt);out=open(output,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(out>=0||errno!=EEXIST)break;}if(out<0){close(in);return 0;}
 struct co_record record,check;for(unsigned i=0;ok&&i<s->samples;i++)ok=read(in,&record,sizeof record)==sizeof record&&co_valid(&record)&&record.index==i&&write_all(out,&record,sizeof record);
 if(ok)ok=!fsync(out)&&lseek(out,0,SEEK_SET)==0&&lseek(in,0,SEEK_SET)==0;
 for(unsigned i=0;ok&&i<s->samples;i++)ok=read(in,&record,sizeof record)==sizeof record&&read(out,&check,sizeof check)==sizeof check&&!memcmp(&record,&check,sizeof record);
 if(close(in))ok=0;if(close(out))ok=0;if(!ok)return 0;
 char meta[768];int n=snprintf(meta,sizeof meta,"{\"format\":\"NXL_COLOR_R11L1\",\"release\":\"R11l\",\"record_bytes\":%u,\"samples\":%u,\"stable_bank0_samples\":%u,\"period_ms\":%u,\"raw_file\":\"records-%u.bin\",\"reads_only\":true,\"RTOS_changed\":false,\"MMIO_reads\":false,\"atomic_request_snapshot\":false,\"hardware_consumption_proven\":false}\n",(unsigned)sizeof record,s->samples,s->valid,CO_PERIOD_MS,s->attempt);
 snprintf(output,sizeof output,"%s/SAVED-%u.json",s->report,s->attempt);out=open(output,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(out<0)return 0;ok=n>0&&n<(int)sizeof meta&&write_all(out,meta,(size_t)n)&&!fsync(out);if(close(out))ok=0;return ok;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_COLOR_OBSERVE=R11l;RAM_READ_ONLY;SCHEMA=1;PERIOD_MS=500");return 0;}
 if(argc<2||argc>3)return 2;int run=!strcmp(argv[1],"--run"),stop=!strcmp(argv[1],"--stop"),status=!strcmp(argv[1],"--status");if((!run&&!stop&&!status)||(run?argc!=3:argc!=2))return 2;
 if(!private_dir(PRIVATE))return 1;int lock=open(LOCK,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||!regular(lock))return 1;int own=flock(lock,LOCK_EX|LOCK_NB)==0;if(!own&&errno!=EWOULDBLOCK)return 1;
 struct run s;if(!read_run(&s))return 1;
 if(status){response(&s,!own,"COLOR_STATUS");return 0;}
 if(stop){
  if(!s.magic||s.state==SAVED){response(&s,0,"COLOR_SAVED");return 0;}
  if(!own){char p[256];snprintf(p,sizeof p,"%s/stop",s.ram);int f=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f>=0)close(f);else if(errno!=EEXIST)return 1;response(&s,1,"COLOR_STOP_REQUESTED");return 0;}
  char media[128];if(!co_export_allowed(nxl_observer_usb_state(),media_busy(find_camera(),media))){s.state=PENDING;save_run(&s);response(&s,0,"COLOR_STOP_VIDEO_UNPLUG_USB");return 1;}
  int ok=export_records(&s);s.state=ok?SAVED:PENDING;save_run(&s);response(&s,0,ok?"COLOR_SAVED":"COLOR_SAVE_RETRY");return ok?0:1;
 }
 if(!own){response(&s,1,"COLOR_ALREADY_RUNNING");return 0;}if(s.magic&&s.state!=SAVED&&s.state!=OFF){response(&s,0,"COLOR_PENDING_SAVE_FIRST");return 1;}
 if(!path_ok(argv[2],"/mnt/mmc/NXDEMO51/COLOR_R11L_")||nxl_observer_usb_state()!=0)return 1;
 int pid=find_camera();char media[128];if(media_busy(pid,media)!=0)return 1;
 struct reader reader={0};reader.fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(reader.fd<0)return 1;
 struct co_record record={0};co_capture(ram_read,&reader,&record);
 if(record.first.manager[0]!=GC_MANAGER_VPTR||record.first.roots[0]!=0x80592640u||record.first.mapping[0]!=0xbb701402u){puts("COLOR_FIRMWARE_OR_MANAGER_IDENTITY_FAILED");return 1;}
 memset(&s,0,sizeof s);s.magic=RUN_MAGIC;s.state=ON;strcpy(s.report,argv[2]);snprintf(s.ram,sizeof s.ram,PRIVATE "/color_%llu_%u",(unsigned long long)stamp(),(unsigned)getpid());if(mkdir(s.ram,0700))return 1;
 char path[256];snprintf(path,sizeof path,"%s/records.bin",s.ram);int output=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(output<0||!save_run(&s))return 1;
 signal(SIGTERM,interrupted_by);signal(SIGINT,interrupted_by);int stopping=0,ok=1;
 for(;;){
  uint64_t start=stamp();snprintf(path,sizeof path,"%s/stop",s.ram);struct stat request;if(!lstat(path,&request))stopping=1;if(interrupted)stopping=1;
  int usb=nxl_observer_usb_state();if(usb!=0)stopping=1;
  memset(&record,0,sizeof record);record.index=s.samples;record.time_lo=(uint32_t)start;record.time_hi=(uint32_t)(start>>32);record.camera_pid=(uint32_t)pid;int busy=media_busy(pid,record.media_name);record.media_busy=(uint32_t)busy;
  if(stopping&&co_export_allowed(usb,busy)){if(close(output))ok=0;output=-1;if(ok)ok=export_records(&s);s.state=ok?SAVED:PENDING;save_run(&s);break;}
  if(s.samples<CO_LIMIT&&!stopping){co_capture(ram_read,&reader,&record);record.duration_us=(uint32_t)((stamp()-start)/1000u);co_commit(&record);if(!write_all(output,&record,sizeof record)){s.state=FAILED;save_run(&s);ok=0;break;}s.samples++;if(record.status==CO_OK)s.valid++;}
  s.state=stopping?WAIT:s.samples==CO_LIMIT?FULL:ON;if(!save_run(&s)){ok=0;break;}
  /* Interrupted or USB pending: retain RAM and exit without SD writes. */
  if(interrupted||usb!=0){s.state=PENDING;save_run(&s);break;}
  uint64_t now=stamp(),period=(uint64_t)CO_PERIOD_MS*1000000u;if(now<start+period){uint64_t left=start+period-now;struct timespec delay={(time_t)(left/1000000000u),(long)(left%1000000000u)};nanosleep(&delay,NULL);}
 }
 if(output>=0)close(output);for(unsigned i=0;i<64;i++)if(reader.page[i].map)munmap(reader.page[i].map,4096);close(reader.fd);close(lock);return ok?0:1;
}
