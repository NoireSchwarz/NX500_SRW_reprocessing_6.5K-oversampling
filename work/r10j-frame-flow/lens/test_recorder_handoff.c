#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../common/ldc_observation.h"
static uint32_t top[1024],ldc[1024];static uint64_t clock_ns=1000000000u;static unsigned records,maps,loads;static struct lh_snapshot latest;
static int dev=7,directory=-1;static FILE*context_file;
static uint64_t now(void){return clock_ns;}
static unsigned live_top,live_ldc,unmaps,fail_unmap;
static void fixture_maps(void){
 FILE*f=fopen("/proc/200/maps","w");assert(f);
 if(live_top)fprintf(f,"50000000-50001000 r--s 20821000 00:01 1 /dev/mem\n");
 if(live_ldc)fprintf(f,"50001000-50002000 r--s 20823000 00:01 1 /dev/mem\n");
 assert(!fclose(f));
}
static void*readonly_map(void*addr,size_t bytes,int prot,int flags,int fd,off_t offset){
 assert(!addr&&bytes==4096&&prot==PROT_READ&&flags==MAP_SHARED&&fd==7);maps++;if(offset==0x20821000)live_top=1;else live_ldc=1;fixture_maps();assert(offset==0x20821000||offset==0x20823000);return offset==0x20821000?(void*)top:(void*)ldc;
}
static int readonly_unmap(void*addr,size_t bytes){assert((addr==top||addr==ldc)&&bytes==4096);if(fail_unmap&&addr==ldc){errno=EBUSY;return -1;}if(addr==top)live_top=0;else live_ldc=0;unmaps++;fixture_maps();return 0;}
static int lens_ro_load(const volatile void*from,void*out,unsigned n){loads++;memcpy(out,(const void*)from,n);return 1;}
static int frame(FILE*f,unsigned kind,unsigned seq,uint64_t begin,uint64_t end,const void*data,unsigned bytes){(void)f;(void)seq;assert(kind==15&&begin<=end&&bytes==sizeof latest);latest=*(const struct lh_snapshot*)data;records++;return 1;}
#define mmap readonly_map
#define munmap readonly_unmap
#include "ldc-hardware-capture.inc"
#undef mmap
#undef munmap

static int pid=100,watch_pid;static unsigned p7_peer_scans,p7_peer_pids;static char p7_peer_audit[65536];static size_t p7_peer_audit_bytes;
static int file_line(const char*p,char*b,size_t n){FILE*f=fopen(p,"r");if(!f)return 0;int ok=fgets(b,n,f)!=NULL;fclose(f);return ok;}
static void p7_peer_reject(unsigned scan,int candidate,const char*why,const char*value){
 if(p7_peer_audit_bytes>=sizeof p7_peer_audit)return;
 int n=snprintf(p7_peer_audit+p7_peer_audit_bytes,sizeof p7_peer_audit-p7_peer_audit_bytes,"scan=%u pid=%d rejected=%s detail=%s\n",scan,candidate,why,value?value:"");
 if(n>0){size_t available=sizeof p7_peer_audit-p7_peer_audit_bytes;p7_peer_audit_bytes+=(size_t)n<available?(size_t)n:available-1;}
}
static int p7_other_ep_users(int child){
 DIR *proc=opendir("/proc");if(!proc)return 0;struct dirent *e;int ok=1;unsigned scan=++p7_peer_scans;
 while((e=readdir(proc))){int candidate=atoi(e->d_name);if(candidate<=0||candidate==pid||candidate==getpid()||candidate==child||candidate==watch_pid)continue;
  char path[180],line[512];unsigned long pages=0;
  snprintf(path,sizeof path,"/proc/%d/statm",candidate);
  if(!file_line(path,line,sizeof line)||sscanf(line,"%lu",&pages)!=1){if(kill(candidate,0)==0){p7_peer_reject(scan,candidate,"STATM_UNREADABLE","");ok=0;break;}continue;}
  if(!pages){
   if(p7_peer_audit_bytes+100>=sizeof p7_peer_audit){ok=0;break;}
   int len=snprintf(p7_peer_audit+p7_peer_audit_bytes,sizeof p7_peer_audit-p7_peer_audit_bytes,"scan=%u pid=%d no_userspace_MM=1\n",scan,candidate);if(len>0)p7_peer_audit_bytes+=(size_t)len;continue;
  }
  snprintf(path,sizeof path,"/proc/%d/maps",candidate);FILE *maps=fopen(path,"r");if(maps){
   while(fgets(line,sizeof line,maps)){unsigned lo,hi,off;char perms[8];
    if(strstr(line,"/dev/drime5_ep")){p7_peer_reject(scan,candidate,"EP_MAPPING",line);ok=0;break;}
    if(strstr(line,"/dev/mem")&&sscanf(line,"%x-%x %7s %x",&lo,&hi,perms,&off)==4&&off<0x2082c000u&&(uint64_t)off+hi-lo>0x20820000u){p7_peer_reject(scan,candidate,"EP_DEVMEM_MAPPING",line);ok=0;break;}
   }if(ferror(maps)){p7_peer_reject(scan,candidate,"MAPS_READ_ERROR","");ok=0;}fclose(maps);if(!ok)break;
  }else {int alive=kill(candidate,0)==0;if(alive){p7_peer_reject(scan,candidate,"MAPS_UNREADABLE","");ok=0;break;}continue;}
  snprintf(path,sizeof path,"/proc/%d/fd",candidate);DIR *fds=opendir(path);if(!fds){if(kill(candidate,0)==0){p7_peer_reject(scan,candidate,"FDS_UNREADABLE","");ok=0;break;}continue;}struct dirent *fd;
  while((fd=readdir(fds))){if(fd->d_name[0]=='.')continue;char link[512],target[256];snprintf(link,sizeof link,"%s/%s",path,fd->d_name);ssize_t n=readlink(link,target,sizeof target-1);if(n<0)continue;target[n]=0;if(!strcmp(target,"/dev/drime5_ep")){p7_peer_reject(scan,candidate,"EP_FD",link);ok=0;break;}}
  closedir(fds);p7_peer_pids++;
  if(p7_peer_audit_bytes+120>=sizeof p7_peer_audit){ok=0;break;}
  int len=snprintf(p7_peer_audit+p7_peer_audit_bytes,sizeof p7_peer_audit-p7_peer_audit_bytes,"scan=%u pid=%d maps_readable=1 fd_directory_readable=1 EP_conflict=%u\n",scan,candidate,!ok);if(len>0)p7_peer_audit_bytes+=(size_t)len;
  if(!ok)break;
 }closedir(proc);return ok;
}
static unsigned active_recorder=1,skip_pump;static int client_directory;
static int lens_active(char*r,size_t n){assert(n>=1024);if(!active_recorder)return 0;strcpy(r,"/mnt/mmc");return 1;}
static int peer_sleep(const struct timespec*a,struct timespec*b){(void)a;(void)b;clock_ns+=20000000;if(!skip_pump)lens_ldc_hardware_control();return 0;}
#define directory client_directory
#define nanosleep peer_sleep
static int lens_control(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXLENSWATCH7 R9a native LDC hardware / Gamma19 / packed DDR; bounded; own lock; read-only");return 0;}
 char report[1024]={0};int active=lens_active(report,sizeof report);
 if(argc==2&&!strcmp(argv[1],"--active")){if(active){puts(report);return 0;}puts("NO_LIVE_LENS_RECORDER");return 3;}
 if(!active)return 3;
 directory=open(report,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)return 2;
 if(argc==3&&!strcmp(argv[1],"--yield-hardware")){
  if(!lhy_token(argv[2])||!lhy_publish(directory,"hardware-yield.request",argv[2])){close(directory);return 1;}
  uint64_t deadline=now()+3000000000u;
  for(unsigned i=0;i<300&&now()<deadline;i++){
   char acknowledged[33],current[1024];
   if(lhy_read(directory,"hardware-yield.ack",acknowledged)&&!strcmp(acknowledged,argv[2])&&lens_active(current,sizeof current)&&!strcmp(current,report)){
    printf("RECORDER_EP_MMIO_YIELDED report=%s token=%s\n",report,argv[2]);close(directory);return 0;
   }
   struct timespec delay={0,10000000};nanosleep(&delay,NULL);
  }
  close(directory);puts("RECORDER_EP_MMIO_YIELD_UNCONFIRMED");return 1;
 }
 if(argc==2&&!strcmp(argv[1],"--stop")){
  int f=openat(directory,"STOP",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);int saved=errno;if(f>=0){close(f);fsync(directory);}close(directory);if(f<0&&saved!=EEXIST)return 1;puts(report);return 0;
 }
 if(argc==3&&!strcmp(argv[1],"--mark")){
  size_t n=strlen(argv[2]);if(!n||n>80){close(directory);return 2;}for(size_t i=0;i<n;i++)if(!((argv[2][i]>='a'&&argv[2][i]<='z')||(argv[2][i]>='A'&&argv[2][i]<='Z')||(argv[2][i]>='0'&&argv[2][i]<='9')||argv[2][i]=='_'||argv[2][i]=='-')){close(directory);return 2;}
  int f=openat(directory,"marks.tsv",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0){close(directory);return 1;}
  int ok=dprintf(f,"%llu\t%s\n",(unsigned long long)now(),argv[2])>0&&!fsync(f);if(close(f))ok=0;close(directory);return ok?0:1;
 }
 close(directory);return 2;
}

#undef directory
#undef nanosleep

static int client(const char*token){char*a[]={"watcher","--yield-hardware",(char*)token};return lens_control(3,a);}
int main(void){
 const char*old="00000000000000000000000000000000",*a="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",*b="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",*c="cccccccccccccccccccccccccccccccc";
 directory=open("/mnt/mmc",O_RDONLY|O_DIRECTORY|O_CLOEXEC);assert(directory>=0);
 lens_ldc_hardware_sample(0,0);assert(maps==2&&records==1);
 assert(!p7_other_ep_users(-1)&&strstr(p7_peer_audit,"EP_DEVMEM_MAPPING"));
 assert(lhy_publish(directory,"hardware-yield.ack",old));
 lens_ldc_hardware_control();assert(!lh_lens.yielded&&live_top&&live_ldc);
 assert(client("BAD")==1&&!lh_lens.yielded);
 fail_unmap=1;assert(lhy_publish(directory,"hardware-yield.request",a));clock_ns+=20000000;lens_ldc_hardware_control();
 char token[33];assert(lh_lens.yielded&&!live_top&&live_ldc&&lhy_read(directory,"hardware-yield.ack",token)&&!strcmp(token,old));
 unsigned before_loads=loads,before_maps=maps;lens_ldc_hardware_sample(1,1);assert(loads==before_loads&&maps==before_maps);
 assert(client(b)==1&&!p7_other_ep_users(-1)); /* failed unmap cannot grant pipeline entry */
 fail_unmap=0;assert(client(c)==0&&!live_top&&!live_ldc&&p7_other_ep_users(-1));
 assert(lhy_read(directory,"hardware-yield.ack",token)&&!strcmp(token,c));
 unsigned count=unmaps;assert(client(a)==0&&unmaps==count);lens_ldc_hardware_sample(2,1);assert(loads==before_loads&&maps==before_maps);
 skip_pump=1;assert(client(b)==1); /* stale different-token acknowledgement */
 active_recorder=0;assert(client(a)==3);
 assert(lens_ldc_hardware_end()&&unmaps==count);close(directory);
 puts("PASS exact peer scanner rejects observer RO maps; acknowledged unmap clears conflict; no remap; failed unmap, stale ack, invalid token, timeout and missing recorder stop safely");return 0;
}
