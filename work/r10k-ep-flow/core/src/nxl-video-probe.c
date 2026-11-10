/* SPDX-License-Identifier: AGPL-3.0-only. R10k three-mode frame flow probe.
 * Sampling RAM/MMIO access is read-only; marks are bookmarks, no A/B writes.
 * The separate idle arming helper leaves selected video output unchanged.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "sha256.h"
#include "../../common/observer_usb.h"
#include "../../common/video_counter_capture.h"
#include "../../common/video_path_capture.h"
#include "../../common/video_clock_capture.h"
#include "../../common/video_qos_capture.h"
#include "../../common/video_bus_capture.h"
#include "../../common/video_latency_capture.h"
#include "../../common/video_flow_capture.h"
#include "../../common/video_ep_readback.h"
#include "../../common/video_completion_capture.h"
#include "../../common/video_compare_state.h"
#include "../../common/video_progress_capture.h"
#include "../../common/video_phase_capture.h"
#include "../../common/video_counter_profile.h"
#include "../../lens/lens_ro_guard.h"
#define VP_ROOT "/tmp/nx-lut-lite"
#define VP_RAM "/dev/shm/nx-lut-lite"
#define VP_CURRENT VP_RAM "/video-probe.current"
#define VP_LEASE VP_ROOT "/lens-evidence.fd"
#define VP_REPORT "/mnt/mmc/NXDEMO51/VIDEO_STAGE_"
#define VP_BUFFER_BYTES (VP_HEADER_BYTES+VP_MAX_SAMPLES*sizeof(struct vp_record))
#define VP_CONTROL_XOR 0x75e38b91u
#define VP_STOP_XOR 0xa18c39f7u
#define VP_APP "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
static volatile sig_atomic_t interrupted;
static const char*last_reason="TRACE_UNAVAILABLE";
static uint64_t clock_ns(clockid_t id){struct timespec t;if(clock_gettime(id,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void stopped(int s){(void)s;interrupted=1;}
static int read_text(const char*p,char*b,unsigned cap){int f=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;ssize_t n=read(f,b,cap-1);int ok=n>=0&&n<(ssize_t)cap-1;if(n>=0)b[n]=0;if(close(f))ok=0;return ok;}
static uint64_t ticks(int pid){char p[80],b[2048];snprintf(p,sizeof p,"/proc/%d/stat",pid);if(!read_text(p,b,sizeof b))return 0;char*q=strrchr(b,')');if(!q||q[1]!=' ')return 0;q+=2;for(unsigned i=3;i<22;i++){q=strchr(q,' ');if(!q)return 0;while(*q==' ')q++;}return strtoull(q,NULL,10);}
static int camera_pid(void){DIR*d=opendir("/proc");if(!d)return 0;int result=0;struct dirent*e;while((e=readdir(d))){char*end;long p=strtol(e->d_name,&end,10);if(*end||p<=1||p>2147483647)continue;char q[80],target[256];snprintf(q,sizeof q,"/proc/%ld/exe",p);ssize_t n=readlink(q,target,sizeof target-1);if(n<0||n>=(ssize_t)sizeof target-1)continue;target[n]=0;if(!strcmp(target,VP_APP)){if(result){result=0;break;}result=(int)p;}}closedir(d);return result;}
static int camera_busy(int pid){
 char root[80];snprintf(root,sizeof root,"/proc/%d/fd",pid);DIR*d=opendir(root);if(!d)return ticks(pid)?-1:0;int result=0;struct dirent*e;unsigned visited=0;
 while((e=readdir(d))){char*end;long f=strtol(e->d_name,&end,10);if(*end||f<0)continue;if(++visited>512){result=-1;break;}char p[112],target[512],b[1024];snprintf(p,sizeof p,"%s/%ld",root,f);ssize_t n=readlink(p,target,sizeof target-1);if(n<0){if(errno==ENOENT)continue;result=-1;break;}if(n>=(ssize_t)sizeof target-1){result=-1;break;}target[n]=0;
  const char*ext=strrchr(target,'.');if(!ext||(strcasecmp(ext,".mp4")&&strcasecmp(ext,".srw")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))continue;
  snprintf(p,sizeof p,"/proc/%d/fdinfo/%ld",pid,f);if(!read_text(p,b,sizeof b)){result=-1;break;}char*q=strstr(b,"flags:");unsigned flags;if(!q||sscanf(q+6,"%o",&flags)!=1){result=-1;break;}if((flags&O_ACCMODE)!=O_RDONLY){result=1;break;}
 }closedir(d);return result;
}
static int report_path(const char*p){size_t n=strlen(VP_REPORT);if(strncmp(p,VP_REPORT,n)||strlen(p)>=256||!p[n])return 0;for(p+=n;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='_'))return 0;return 1;}
static int buffer_path(const char*p){const char*prefix=VP_RAM "/video-buffer-";size_t n=strlen(prefix);if(strncmp(p,prefix,n)||strlen(p)>=160||!p[n])return 0;for(p+=n;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')||*p=='-'))return 0;return 1;}
static int private_directory(const char*p){struct stat st;return !lstat(p,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&!(st.st_mode&022);}
static int tmp_directory(void){return private_directory(VP_ROOT);}
static int ram_directory(void){struct stat st;struct statfs fs;if(lstat("/dev/shm",&st)||!S_ISDIR(st.st_mode)||statfs("/dev/shm",&fs)||((unsigned long)fs.f_type!=0x01021994u&&(unsigned long)fs.f_type!=0x858458f6u))return 0;if(mkdir(VP_RAM,0700)&&errno!=EEXIST)return 0;return private_directory(VP_RAM);}
static int regular_fd(int fd,off_t bytes){struct stat st;return !fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==getuid()&&(st.st_mode&077)==0&&(bytes<0||st.st_size==bytes);}
static int all_write(int fd,const void*p,size_t n){const unsigned char*b=p;while(n){ssize_t k=write(fd,b,n);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;b+=k;n-=(size_t)k;}return 1;}
static int all_read(int fd,void*p,size_t n){unsigned char*b=p;while(n){ssize_t k=read(fd,b,n);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;b+=k;n-=(size_t)k;}return 1;}
static int lease_busy(void){int f=open(VP_LEASE,O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;int k=flock(f,LOCK_EX|LOCK_NB),busy=k<0&&(errno==EAGAIN||errno==EWOULDBLOCK);if(!k)flock(f,LOCK_UN);close(f);return busy;}
static int same_program(int pid){char p[80],a[1024],b[1024];snprintf(p,sizeof p,"/proc/%d/exe",pid);ssize_t x=readlink(p,a,sizeof a),y=readlink("/proc/self/exe",b,sizeof b);return x>0&&x<(ssize_t)sizeof a&&y==x&&!memcmp(a,b,(size_t)x);}
static int header_valid(const struct vp_header*h){return !memcmp(h->magic,"NXLVCTR1",8)&&h->schema==7&&h->header_bytes==VP_HEADER_BYTES&&h->record_bytes==sizeof(struct vp_record)&&h->capacity==VP_MAX_SAMPLES&&h->count<=VP_MAX_SAMPLES&&h->phase<=VP_FAILED&&h->pid>1&&h->camera_pid>1&&h->cookie&&h->worker_ticks&&memchr(h->report,0,sizeof h->report)&&memchr(h->buffer,0,sizeof h->buffer)&&report_path(h->report)&&buffer_path(h->buffer);}
struct control {int fd,live;struct vp_header*h;void*mapping;};
static int load_control(struct control*c,int writable){
 memset(c,0,sizeof*c);c->fd=-1;char b[1100],path[160],report[256],extra;unsigned pid,cookie;unsigned long long start;
 if(!tmp_directory()||!private_directory(VP_RAM)||!read_text(VP_CURRENT,b,sizeof b)||sscanf(b,"NXLVPROBE1\n%u\n%llu\n%159[^\n]\n%255[^\n]\n%u\n%c",&pid,&start,path,report,&cookie,&extra)!=5||pid<=1||!start||!cookie||!buffer_path(path)||!report_path(report))return 0;
 c->fd=open(path,(writable?O_RDWR:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW);if(c->fd<0||!regular_fd(c->fd,VP_BUFFER_BYTES))goto bad;
 if(writable&&flock(c->fd,LOCK_EX|LOCK_NB))goto bad;
 c->mapping=mmap(NULL,VP_HEADER_BYTES,PROT_READ|(writable?PROT_WRITE:0),MAP_SHARED,c->fd,0);if(c->mapping==MAP_FAILED){c->mapping=NULL;goto bad;}c->h=c->mapping;
 if(!header_valid(c->h)||strcmp(path,c->h->buffer)||strcmp(report,c->h->report)||pid!=c->h->pid||start!=c->h->worker_ticks||cookie!=c->h->cookie)goto bad;
 c->live=lease_busy()&&ticks((int)pid)==start&&same_program((int)pid);return 1;
bad:if(c->mapping)munmap(c->mapping,VP_HEADER_BYTES);if(c->fd>=0)close(c->fd);memset(c,0,sizeof*c);c->fd=-1;return 0;
}
static void unload(struct control*c){if(c->mapping)munmap(c->mapping,VP_HEADER_BYTES);if(c->fd>=0)close(c->fd);c->fd=-1;c->mapping=NULL;}
static void protocol(const struct vp_header*h,int live,const char*reason){const char*state=!h?"OFF":h->phase==VP_SAVED?"OFF":live?(h->phase==VP_RUNNING?"ON":h->phase==VP_FULL?"FULL":"SAVING"):"PENDING";printf("NXL_VIDEO_PROBE1\nstate=%s\nreason=%s\nreport=%s\nsamples=%u\nperiod_ms=%u\nend=NXL_VIDEO_PROBE_END1\n",state,reason,h?h->report:"",h?h->count:0,h?h->period_ms:VP_PERIOD_MS);}
static int ready(int*pid){int usb=nxl_usb_physical_state();if(usb){last_reason=usb>0?"TRACE_USB_CONNECTED":"TRACE_USB_PORT_UNAVAILABLE";return 0;}*pid=camera_pid();if(*pid<=1||!ticks(*pid)){last_reason="TRACE_CAMERA_UNAVAILABLE";return 0;}int busy=camera_busy(*pid);if(busy){last_reason=busy>0?"TRACE_STOP_VIDEO_BEFORE_START":"TRACE_CAMERA_STATE_UNAVAILABLE";return 0;}return 1;}
static int make_current(const struct vp_header*h){char path[160],b[900];snprintf(path,sizeof path,VP_RAM "/video-current-%u",h->pid);int n=snprintf(b,sizeof b,"NXLVPROBE1\n%u\n%llu\n%s\n%s\n%u\n",h->pid,(unsigned long long)h->worker_ticks,h->buffer,h->report,h->cookie);int f=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;int ok=n>0&&n<(int)sizeof b&&all_write(f,b,(size_t)n);if(close(f))ok=0;if(ok&&rename(path,VP_CURRENT))ok=0;if(!ok)unlink(path);return ok;}
static int ready_file(const struct vp_header*h){struct stat st;if(lstat("/mnt/mmc/NXDEMO51",&st)||!S_ISDIR(st.st_mode)||lstat(h->report,&st)||!S_ISDIR(st.st_mode))return 0;int d=open(h->report,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)return 0;int f=openat(d,"READY",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);char b[160];int n=snprintf(b,sizeof b,"NXLVREADY1\npid=%u\ncookie=%u\nrecord_bytes=%u\ncapacity=%u\n",h->pid,h->cookie,h->record_bytes,h->capacity);int ok=f>=0&&all_write(f,b,(size_t)n);if(f>=0&&close(f))ok=0;close(d);return ok;}
static int existing_output(int d,const struct vp_header*expected,const struct vp_record*records,const void*extension,struct vp_header*out){
 int f=openat(d,"counts.bin",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return errno==ENOENT?0:-1;
 unsigned char padding[VP_HEADER_BYTES];int ok=all_read(f,padding,sizeof padding);struct vp_header got={0};if(ok)memcpy(&got,padding,sizeof got);
 struct stat st;if(!ok||!header_valid(&got)||got.phase!=VP_SAVED||got.cookie!=expected->cookie||got.pid!=expected->pid||got.count!=expected->count||strcmp(got.report,expected->report)||got.header_hash!=vp_hash(&got,offsetof(struct vp_header,header_hash))||fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)(VP_HEADER_BYTES+got.count*sizeof(struct vp_record)))ok=0;
 for(unsigned i=0;ok&&i<got.count;i++){struct vp_record r;ok=all_read(f,&r,sizeof r)&&vp_record_valid(&r)&&!memcmp(&r,records+i,sizeof r);}
 if(ok&&memcmp(padding+VP_QOS_OFFSET,extension,VP_HEADER_BYTES-VP_QOS_OFFSET))ok=0;
 if(close(f))ok=0;if(ok)*out=got;return ok?1:-1;
}
static int receipt(int d,const struct vp_header*h,const char*b,unsigned n){
 int f=openat(d,"DONE",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(f>=0){char got[400];ssize_t k=read(f,got,sizeof got);int ok=k==(ssize_t)n&&!memcmp(got,b,n);if(close(f))ok=0;return ok;}
 if(errno!=ENOENT)return 0;char part[80];snprintf(part,sizeof part,"DONE-%08x-%u.part",h->cookie,(unsigned)getpid());
 f=openat(d,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;
 int ok=all_write(f,b,n)&&!fsync(f);if(close(f))ok=0;
 if(ok){struct stat st;ok=nxl_usb_physical_state()==0&&fstatat(d,"DONE",&st,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT&&!renameat(d,part,d,"DONE");}
 if(!ok&&nxl_usb_physical_state()==0)unlinkat(d,part,0);return ok;
}
/* R10k marks are bookmarks only; sampling NEVER writes device memory. */
static void vpc_event(struct vp_compare*p,unsigned request){p->request=request;p->changed_ns=clock_ns(CLOCK_MONOTONIC);p->sequence++;uint32_t*out=p->event[(p->sequence-1u)%3u];out[0]=p->sequence;out[1]=request;out[2]=p->phase;out[3]=p->result;out[4]=(uint32_t)(p->changed_ns/1000000u);}
static void vpc_init(struct vp_compare*p,struct vp_compare*out){memset(p,0,sizeof*p);memcpy(p->magic,"NXLQOSAB",8);p->schema=1;p->bytes=sizeof*p;p->original=p->current=VP_UNKNOWN;p->phase=VPC_BASELINE;p->result=VPC_OK;vpc_event(p,0);vpc_publish(out,p);}
static int vpc_restore(struct vp_compare*p,struct vp_compare*out){if(p->owned)return 0;vpc_publish(out,p);return 1;}
static void vpc_toggle(struct vp_compare*p,struct vp_compare*out,unsigned request,int camera){(void)camera;vpc_event(p,request);vpc_publish(out,p);}
static const char*vpc_reason(const struct vp_compare*p){return vpc_valid(p)?"TRACE_MARKED":"TRACE_COMPARE_STATE_INVALID";}
static int vpc_recover(struct vp_header*h){
 struct vp_compare*out=(struct vp_compare*)((unsigned char*)h+VP_COMPARE_OFFSET),p=*out;
 if(!vpc_valid(&p)){last_reason="TRACE_COMPARE_STATE_INVALID";return 0;}
 if(!vpc_restore(&p,out)){h->reason|=512u;last_reason="TRACE_COMPARE_RESTORE_FAILED";return 0;}return 1;
}
static int export_buffer(struct vp_header*h){
 if(!header_valid(h)||h->phase==VP_SAVED)return h&&h->phase==VP_SAVED;
 int current_camera=camera_pid();if(nxl_usb_physical_state()!=0||!current_camera||camera_busy(current_camera)!=0){last_reason="TRACE_SAVE_PENDING_STOP_VIDEO";return 0;}
 if(!vpc_recover(h))return 0;
 int f=-1,d=-1,input=-1,ok=0,part_created=0;void*map=MAP_FAILED;char expected[160],got[160]={0},part[80];
 snprintf(part,sizeof part,"counts-%08x-%u.part",h->cookie,(unsigned)getpid());
 d=open(h->report,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)goto done;
 int k=openat(d,"READY",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(k<0)goto done;ssize_t n=read(k,got,sizeof got-1);close(k);int m=snprintf(expected,sizeof expected,"NXLVREADY1\npid=%u\ncookie=%u\nrecord_bytes=%u\ncapacity=%u\n",h->pid,h->cookie,h->record_bytes,h->capacity);if(n!=m||memcmp(got,expected,(size_t)m))goto done;
 input=open(h->buffer,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(input<0||!regular_fd(input,VP_BUFFER_BYTES))goto done;
 map=mmap(NULL,VP_BUFFER_BYTES,PROT_READ,MAP_SHARED,input,0);if(map==MAP_FAILED)goto done;
 struct vp_header final=*h;struct vp_record*r=(struct vp_record*)((unsigned char*)map+VP_HEADER_BYTES);unsigned count=0;
 while(count<final.count&&vp_record_valid(r+count)&&r[count].sequence==count)count++;
 if(count!=final.count){final.reason|=0x100u;final.count=count;}final.finished_ns=count?r[count-1].end_ns:final.started_ns;final.cpu_finished_ns=count?r[count-1].cpu_ns:final.cpu_started_ns;final.published_ns=clock_ns(CLOCK_MONOTONIC);final.phase=VP_SAVED;final.stop_request=final.mark_request=final.control_cookie=0;final.header_hash=vp_hash(&final,offsetof(struct vp_header,header_hash));
 struct vp_header prior;int existed=existing_output(d,&final,r,(unsigned char*)map+VP_QOS_OFFSET,&prior);if(existed<0){last_reason="TRACE_OUTPUT_CONFLICT";goto done;}
 if(existed)final=prior;
 else {
  f=openat(d,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)goto done;part_created=1;
  unsigned char padding[VP_HEADER_BYTES];memcpy(padding,map,sizeof padding);memcpy(padding,&final,sizeof final);if(!all_write(f,padding,sizeof padding)||!all_write(f,r,count*sizeof*r)||fsync(f))goto done;
  if(close(f)){f=-1;goto done;}f=-1;
  struct stat absent;if(!fstatat(d,"counts.bin",&absent,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT||nxl_usb_physical_state()!=0||renameat(d,part,d,"counts.bin"))goto done;part_created=0;
 }
 char b[400];m=snprintf(b,sizeof b,"NXLVIDDONE1\nsamples=%u\nrecord_bytes=%u\nbytes=%u\nlast_period_ms=%u\nmax_sample_ns=%llu\nreason_flags=%u\n",count,(unsigned)sizeof*r,VP_HEADER_BYTES+count*(unsigned)sizeof*r,final.period_ms,(unsigned long long)final.max_sample_ns,final.reason);
 if(m<=0||m>=(int)sizeof b||!receipt(d,&final,b,(unsigned)m))goto done;
 memcpy(h,&final,sizeof final);ok=1;last_reason="TRACE_SAVED";
done:if(map!=MAP_FAILED)munmap(map,VP_BUFFER_BYTES);if(input>=0)close(input);if(f>=0)close(f);if(part_created&&d>=0&&nxl_usb_physical_state()==0)unlinkat(d,part,0);if(d>=0)close(d);return ok;
}
struct mapped_page {uint32_t address;volatile unsigned char*memory;};
struct runtime {int dev,mem,pid;uint64_t start;struct mapped_page pages[32];unsigned page_count,eviction,reads,bytes;};
static int physical_allowed(uint32_t a,unsigned n){
 if(!n||(a&3u)||(n&3u)||(n>128u&&!(a==VP_LATENCY_DATA&&n==VP_LATENCY_BYTES)))return 0;
 if(vp_ep_readback_range(a,n))return 1;
 if(n==4u&&(a==VC_STATUS||a==VC_ENABLE||a==0x208210b0u))return 1;
 for(unsigned i=0;i<8;i++)if(n==4u&&a==vpg_address[i])return 1;
 for(unsigned i=0;i<9;i++)if(n==4u&&a==vp_clock_address[i])return 1;
 for(unsigned i=0;i<VP_QOS_REGS;i++)if(n==4u&&a==vp_qos_address[i])return 1;
 if(n==4u&&a>=0x20810000u&&a<0x20822000u)for(unsigned i=0;i<VP_BUS_REGS;i++)if(a==vp_bus_address[i])return 1;
 /* Native mutable descriptors live in RTOS RAM. Never accept MMIO or
  * Linux user pointers here. Typed callers bracket mutable roots. */
 if(a>=0x81006000u&&(uint64_t)a+n<=0x94000000ULL)return 1;
 if(n==4u&&a==0x801cd9bcu)return 1;
 if((a==0x80592354u&&n==8u)||(a==0x8059237cu&&n==4u))return 1;
 if(a>=0x813ee860u+32u*4u&&(uint64_t)a+n<=0x813ee860u+160u*4u)return 1;
 if(a>=0x8100e400u&&(uint64_t)a+n<=0x8100e400u+26u*40u)return 1;
 for(unsigned i=0;i<4;i++)if(a==vp_pp_addresses[i]&&n==4)return 1;
 if(n==4&&(a==0x8146e438u||a==0x813f1b30u||a==0x813ebf60u||a==0x8014e7ecu))return 1;
 if((a==0x80592294u&&n==8u)||(a==0x805922bcu&&n==4u))return 1;
 for(unsigned i=0;i<sizeof vp_rom_guards/sizeof*vp_rom_guards;i++)if(a>=vp_rom_guards[i].address&&(uint64_t)a+n<=vp_rom_guards[i].address+32u)return 1;
 return 0;
}
static int physical(void*opaque,uint32_t a,void*out,unsigned n){struct runtime*c=opaque;if(!physical_allowed(a,n))return 0;c->reads++;c->bytes+=n;unsigned copied=0;while(copied<n){uint32_t at=a+copied,base=at&~4095u;unsigned i;for(i=0;i<c->page_count;i++)if(c->pages[i].memory&&c->pages[i].address==base)break;if(i==c->page_count){int fresh=i<32u;if(!fresh){i=c->eviction++%32u;if(c->pages[i].memory&&munmap((void*)c->pages[i].memory,4096))return 0;c->pages[i].memory=NULL;}void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,c->dev,(off_t)(uint64_t)base);if(p==MAP_FAILED){c->pages[i].address=0;return 0;}c->pages[i]=(struct mapped_page){base,p};if(fresh)c->page_count++;}unsigned k=4096u-(at&4095u);if(k>n-copied)k=n-copied;if(!lens_ro_load(c->pages[i].memory+(at&4095u),(unsigned char*)out+copied,k))return 0;copied+=k;}return 1;}
static int process(void*opaque,uint32_t a,void*out,unsigned n){struct runtime*c=opaque;if(!vp_user(a,n)||!n||n>256u)return 0;c->reads++;c->bytes+=n;return pread(c->mem,out,n,(off_t)(uint64_t)a)==(ssize_t)n;}
static int hash_library(const struct vp_library*p){char path[120];snprintf(path,sizeof path,"/usr/lib/%s",p->name);int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;struct sha256 h;sha256_init(&h);unsigned char b[8192],digest[32];unsigned bytes=0;ssize_t n;while((n=read(f,b,sizeof b))>0){bytes+=(unsigned)n;sha256_update(&h,b,(size_t)n);}int ok=n==0;if(close(f))ok=0;char hex[65];sha256_final(&h,digest);digest_hex(digest,hex);return ok&&bytes==p->bytes&&!strcmp(hex,p->sha256);}
static int library_bases(int pid,uint32_t bases[3]){char path[100];snprintf(path,sizeof path,"/proc/%d/maps",pid);FILE*f=fopen(path,"r");if(!f)return 0;char line[1200];unsigned seen[3]={0};memset(bases,0,12);while(fgets(line,sizeof line,f)){unsigned lo,hi;unsigned long long off;char mode[8],name[512];if(sscanf(line,"%x-%x %7s %llx %*s %*u %511[^\n]",&lo,&hi,mode,&off,name)!=5||off||mode[0]!='r'||!strchr(mode,'x'))continue;for(unsigned i=0;i<3;i++){char expected[100];snprintf(expected,sizeof expected,"/usr/lib/%s",vp_libraries[i].name);if(!strcmp(name,expected)){bases[i]=lo;seen[i]++;}}}int ok=!ferror(f)&&seen[0]==1&&seen[1]==1&&seen[2]==1;if(fclose(f))ok=0;return ok;}
static int profile(struct runtime*c,uint32_t bases[3]){
 char b[100],v[20],m[20];if(!read_text("/etc/version.info",b,sizeof b)||sscanf(b,"%19[^\r\n]%*[\r\n]%19[^\r\n]",v,m)!=2||strcmp(v,"1.12")||strcmp(m,"NX500"))return 0;
 for(unsigned i=0;i<3;i++)if(!hash_library(vp_libraries+i))return 0;
 if(!library_bases(c->pid,bases))return 0;
 if(!lens_ro_begin())return 0;c->dev=open("/dev/mem",O_RDONLY|O_CLOEXEC|O_SYNC);if(c->dev<0)return 0;
 snprintf(b,sizeof b,"/proc/%d/mem",c->pid);c->mem=open(b,O_RDONLY|O_CLOEXEC);if(c->mem<0)return 0;
 for(unsigned i=0;i<sizeof vp_rom_guards/sizeof*vp_rom_guards;i++){uint32_t got[8];if(!physical(c,vp_rom_guards[i].address,got,32)||memcmp(got,vp_rom_guards[i].words,32))return 0;}
 return ticks(c->pid)==c->start;
}
static void runtime_close(struct runtime*c){lens_ro_end();for(unsigned i=0;i<c->page_count;i++)if(c->pages[i].memory)munmap((void*)c->pages[i].memory,4096);c->page_count=0;if(c->mem>=0)close(c->mem);if(c->dev>=0)close(c->dev);c->mem=c->dev=-1;}
static void cpu_report(uint32_t*out){
 char b[96];out[0]=out[1]=0;
 if(read_text("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",b,sizeof b)){char*end;unsigned long v=strtoul(b,&end,10);while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')end++;if(!*end&&end!=b&&v>=10000u&&v<=4000000u)out[0]=(uint32_t)v;}
 if(read_text("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",b,sizeof b)){b[strcspn(b,"\r\n")]=0;const char*names[]={"interactive","performance","ondemand","powersave","userspace","conservative"};for(unsigned i=0;i<6;i++)if(!strcmp(b,names[i]))out[1]=i+1u;}
}
/* Native NX500 ch_a show calls its live PLL/divider getter, then "%lu\n".
 * Two matching read-only results are a current readback, not a DVFS request.
 * Missing/invalid sysfs is recorded as unavailable and never stops capture. */
static uint32_t ddr_value(void){
 char b[64],*end;if(!read_text("/sys/devices/platform/drime5-ddrfreq/ch_a",b,sizeof b)||b[0]<'0'||b[0]>'9')return 0;
 errno=0;unsigned long v=strtoul(b,&end,10);while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')end++;
 return !errno&&!*end&&v>=25000000u&&v<=1000000000u?(uint32_t)v:0;
}
static void ddr_report(uint32_t*out){uint32_t a=ddr_value(),b=ddr_value();out[0]=a;out[1]=!a||!b?3u:a==b?1u:2u;}
static void close_inherited(void){long n=sysconf(_SC_OPEN_MAX);if(n<0||n>65536)n=65536;for(int i=3;i<n;i++)close(i);unsetenv("NX51_SESSION_TOKEN");unsetenv("NX51_SESSION_ROOT");unsetenv("NX51_JOB");}
static uint64_t vc_now(void*c){(void)c;return clock_ns(CLOCK_MONOTONIC);}
static void vc_wait(void*c){(void)c;struct timespec t={0,1000000};(void)clock_nanosleep(CLOCK_MONOTONIC,0,&t,NULL);}
static int run(const char*report){
 close_inherited();int lease=-1,buffer=-1,result=1,armed=0;char created_path[160]={0};void*map=MAP_FAILED;struct runtime c={.dev=-1,.mem=-1};struct vp_header*h=NULL;uint32_t bases[3];
 struct vp_compare compare={0},*comparison=NULL;
 if(!report_path(report)||!tmp_directory()){last_reason="TRACE_PRIVATE_DIRECTORY_INVALID";goto done;}
 char retired[160]={0};struct control old;if(load_control(&old,0)){int pending=old.h->phase!=VP_SAVED;if(!pending)strcpy(retired,old.h->buffer);unload(&old);if(pending){last_reason="TRACE_PENDING_SAVE_FIRST";goto done;}}else{struct stat st;if(!lstat(VP_CURRENT,&st)||errno!=ENOENT){last_reason="TRACE_REGISTRY_INVALID";goto done;}}
 lease=open(VP_LEASE,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lease<0||!regular_fd(lease,-1)||flock(lease,LOCK_EX|LOCK_NB)){last_reason="TRACE_OTHER_RECORDER_ACTIVE";goto done;}
 if(!ready(&c.pid))goto done;last_reason="TRACE_RAM_FILESYSTEM_UNAVAILABLE";if(!ram_directory())goto done;c.start=ticks(c.pid);last_reason="TRACE_FIRMWARE_OR_LIBRARY_MISMATCH";if(!profile(&c,bases)||camera_busy(c.pid)!=0)goto done;
 char path[160];uint64_t started=clock_ns(CLOCK_MONOTONIC);uint32_t cookie=(uint32_t)(started^(started>>32)^getpid());if(!cookie)cookie=1;snprintf(path,sizeof path,VP_RAM "/video-buffer-%u-%08x",(unsigned)getpid(),cookie);
 buffer=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);last_reason="TRACE_RAM_BUFFER_UNAVAILABLE";if(buffer<0)goto done;strcpy(created_path,path);if(ftruncate(buffer,VP_BUFFER_BYTES)||!regular_fd(buffer,VP_BUFFER_BYTES))goto done;
 map=mmap(NULL,VP_BUFFER_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,buffer,0);if(map==MAP_FAILED)goto done;memset(map,0,VP_BUFFER_BYTES);h=map;
 memcpy(h->magic,"NXLVCTR1",8);h->schema=7;h->header_bytes=VP_HEADER_BYTES;h->record_bytes=sizeof(struct vp_record);h->capacity=VP_MAX_SAMPLES;h->phase=VP_RUNNING;h->pid=(unsigned)getpid();h->camera_pid=(unsigned)c.pid;h->worker_ticks=ticks(getpid());h->camera_ticks=c.start;h->cookie=cookie;h->modules_base=bases[0];h->core_base=bases[1];h->osal_base=bases[2];h->period_ms=VP_PERIOD_MS;strcpy(h->report,report);strcpy(h->buffer,path);strcpy(h->revision,"R10k VIDEO_STAGE_COUNTERS");
 struct vp_reader reader={&c,physical,process,bases[0],bases[1]};struct vp_record warm={0};(void)vp_capture(&reader,&warm);struct vp_path_ring*paths=(struct vp_path_ring*)((unsigned char*)map+VP_PATH_OFFSET);vp_path_init(paths);struct vp_clock_ring*clocks=(struct vp_clock_ring*)((unsigned char*)map+VP_CLOCK_OFFSET);vp_clock_init(clocks);struct vp_recording_watch watch={0};struct vp_qos_ring*qos=(struct vp_qos_ring*)((unsigned char*)map+VP_QOS_OFFSET);vp_qos_init(qos);struct vp_bus_ring*bus=(struct vp_bus_ring*)((unsigned char*)map+VP_BUS_OFFSET);vp_bus_init(bus);uint64_t qos_due=0;uint64_t path_due=0,clock_due=0;
 comparison=(struct vp_compare*)((unsigned char*)map+VP_COMPARE_OFFSET);vpc_init(&compare,comparison);struct vp_phase_ring*phases=(struct vp_phase_ring*)((unsigned char*)map+VP_PHASE_OFFSET);vp_phase_init(phases);uint64_t flow_since=0;unsigned flow_generation=0;uint32_t flow_key[8]={0};
 last_reason="TRACE_REGISTRY_OR_READY_FAILED";if(!ready_file(h)||!make_current(h))goto done;armed=1;
 if(retired[0]&&buffer_path(retired))unlink(retired);
 /* No report-directory fd, SD stream, formatter, hash of frame data, camera
  * suspension, per-frame ioctl or background dlog process in the loop.
  * Only the original readback clock selectors are sampled, at most 0.2 Hz. */
 h->started_ns=clock_ns(CLOCK_MONOTONIC);h->cpu_started_ns=clock_ns(CLOCK_PROCESS_CPUTIME_ID);
 uint64_t next=h->started_ns,identity_due=next+1000000000u,budget_due=next+10000000000u,last_budget_cpu=h->cpu_started_ns,last_budget_time=next,idle_since=0,cpu_due=0;uint32_t previous[8]={0},epoch=0,last_mark=0,cpu[2]={0};struct vp_record*records=(struct vp_record*)((unsigned char*)map+VP_HEADER_BYTES);
 signal(SIGINT,stopped);signal(SIGTERM,stopped);signal(SIGHUP,stopped);signal(SIGPIPE,SIG_IGN);
 for(;;){
  uint64_t stamp=clock_ns(CLOCK_MONOTONIC);int usb=nxl_usb_physical_state();
  if(usb){h->phase=VP_USB;h->reason|=usb>0?1u:2u;last_reason="TRACE_USB_EXIT_RAM_RETAINED";result=0;break;}
  if(stamp>=identity_due){identity_due=stamp+1000000000u;if(ticks(c.pid)!=c.start){h->phase=VP_CAMERA_GONE;h->reason|=4u;last_reason="TRACE_CAMERA_EXIT_RAM_RETAINED";result=0;break;}}
  uint32_t control_cookie=__atomic_load_n(&h->control_cookie,__ATOMIC_ACQUIRE);
  if(interrupted||(control_cookie==(cookie^VP_CONTROL_XOR)&&__atomic_load_n(&h->stop_request,__ATOMIC_RELAXED)==(cookie^VP_STOP_XOR)))h->phase=VP_STOPPING;
  if(h->phase==VP_RUNNING&&control_cookie==(cookie^VP_CONTROL_XOR)){
   unsigned request=__atomic_load_n(&h->mark_request,__ATOMIC_RELAXED);
   if(request!=last_mark){vpc_toggle(&compare,comparison,request,c.pid);last_mark=request;cpu_due=clock_due=qos_due=0;flow_since=0;__atomic_store_n(&h->control_reserved,request,__ATOMIC_RELEASE);}
  }
  if(h->phase==VP_STOPPING){int busy=camera_busy(c.pid);if(!busy){if(!idle_since)idle_since=stamp;if(stamp-idle_since>=1000000000u){if(!vpc_restore(&compare,comparison)){h->phase=VP_FAILED;h->reason|=512u;last_reason="TRACE_COMPARE_RESTORE_FAILED";break;}runtime_close(&c);if(export_buffer(h)){result=0;break;}h->phase=VP_FAILED;h->reason|=8u;last_reason="TRACE_EXPORT_FAILED_RAM_RETAINED";break;}}else idle_since=0;}
  else if(h->count<VP_MAX_SAMPLES){
   struct vp_record*s=records+h->count;memset(s,0,sizeof*s);s->begin_ns=stamp;s->sequence=h->count;s->period_ms=h->period_ms;c.reads=c.bytes=0;(void)vp_capture(&reader,s);(void)vp_latency_capture(&reader,s);vp_completion_capture(&reader,s,vc_now,vc_wait,NULL,compare.phase,compare.result,compare.current);vp_progress_capture(&reader,s,vc_now,NULL);vp_flow_capture(&reader,s);vp_ep_readback_capture(&reader,s,vc_now,NULL);
   if(stamp>=path_due&&vp_video_flow_mode(s)&&(s->valid&(VP_POLICY|VP_BACKEND))==(VP_POLICY|VP_BACKEND)){
    int active=vp_recording_active(&watch,s);unsigned phase=active?1u:0u;
    uint32_t elapsed=(uint32_t)((stamp-h->started_ns)/1000000u);
    vp_path_capture(&reader,paths,s,elapsed);
    if(stamp>=clock_due){uint32_t ddr[2];ddr_report(ddr);vp_clock_capture(&reader,clocks,elapsed,ddr[0],ddr[1]);clock_due=stamp+5000000000u;}
    if(stamp>=qos_due){vp_qos_capture(&reader,qos,s,elapsed,phase);vp_bus_capture(&reader,bus,s,elapsed,phase);qos_due=stamp+5000000000u;}
    uint32_t key[8]={s->rtos[0],s->rtos[2],s->hevc[9],s->source[0],s->source[5],s->flow[2],s->flow[3],phase};
    if(!flow_since||memcmp(flow_key,key,sizeof key)){memcpy(flow_key,key,sizeof key);flow_generation++;flow_since=stamp;}
    if(stamp-flow_since>=2000000000u&&!(s->valid&VP_CHANGED)&&vp_phase_needed(phases,flow_generation)){uint32_t ddr[2];ddr_report(ddr);vp_phase_capture(&reader,phases,s,elapsed,flow_generation,phase,last_mark,ddr);}
    path_due=stamp+1000000000u;
   }
   s->read_calls=c.reads;s->read_bytes=c.bytes;
   if(stamp>=cpu_due){cpu_report(cpu);cpu_due=stamp+1000000000u;}s->reserved[4]=cpu[0];s->reserved[5]=cpu[1];
   uint32_t keys[8]={(s->valid&VP_CAMERA)?s->camera[0]:0,(s->valid&VP_SOURCE)?s->source[0]:0,(s->valid&VP_HEVC)?s->hevc[0]:0,(s->valid&VP_POLICY)?s->rtos[0]:0,(s->valid&VP_ENCODER)?s->encoder[0]:0,s->rtos[2],s->flow[2],s->flow[3]};if(h->count&&memcmp(keys,previous,sizeof keys))epoch++;memcpy(previous,keys,sizeof keys);s->epoch=epoch;
   s->mark=last_mark;s->end_ns=clock_ns(CLOCK_MONOTONIC);s->cpu_ns=clock_ns(CLOCK_PROCESS_CPUTIME_ID);uint64_t duration=s->end_ns-s->begin_ns;if(duration>h->max_sample_ns)h->max_sample_ns=duration;h->total_sample_ns+=duration;
   s->commit=vp_hash(s,offsetof(struct vp_record,commit))^0x91d6037bu;__sync_synchronize();h->count++;
   if(h->count==VP_MAX_SAMPLES){h->phase=VP_FULL;h->reason|=16u;}
   if(stamp>=budget_due){uint64_t cpu=s->cpu_ns;if(cpu>last_budget_cpu&&stamp>last_budget_time&&(cpu-last_budget_cpu)*200u>stamp-last_budget_time&&h->period_ms<1000u){h->period_ms=h->period_ms==200u?500u:1000u;h->reason|=32u;}last_budget_cpu=cpu;last_budget_time=stamp;budget_due=stamp+10000000000u;}
  }
  next+=(uint64_t)h->period_ms*1000000u;stamp=clock_ns(CLOCK_MONOTONIC);if(next<=stamp){h->deadline_misses++;next=stamp+(uint64_t)h->period_ms*1000000u;}
  struct timespec until={(time_t)(next/1000000000u),(long)(next%1000000000u)};while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&until,NULL)==EINTR&&!interrupted){}
 }
 if(h->phase!=VP_SAVED){h->finished_ns=clock_ns(CLOCK_MONOTONIC);h->cpu_finished_ns=clock_ns(CLOCK_PROCESS_CPUTIME_ID);}
done:if(comparison&&compare.owned&&!vpc_restore(&compare,comparison)){if(h)h->reason|=512u;last_reason="TRACE_COMPARE_RESTORE_FAILED";result=1;}runtime_close(&c);if(h&&result&&h->phase==VP_RUNNING){h->phase=VP_FAILED;h->reason|=64u;}if(map!=MAP_FAILED)munmap(map,VP_BUFFER_BYTES);if(buffer>=0)close(buffer);if(!armed&&created_path[0])unlink(created_path);if(lease>=0)close(lease);if(result)fprintf(stderr,"%s\n",last_reason);return result;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_VIDEO_PROBE=R10k; READ_ONLY_SAMPLING; PAIRED_HW_STATUS; BOUNDED_PENDING_FOLLOWUPS; THREE_MODE_NATIVE_FLOW; BOOKMARKS_ONLY; EP_AUX_READBACK_MATCHED_FRAMES; NO_DEVICE_WRITES; NO_MPTOP_OPEN; DDR_A_ACTUAL_SYSFS_5S; TMPFS_BUFFER; IDLE_EXPORT; 5HZ_ADAPTIVE; NO_PTRACE_NO_IOCTL_NO_SD_HOTPATH; PAIRED_IPC_EP_PROGRESS; PINNED_MODE_PHASE_DMA_DDR; TARGET_FPS_AND_NATIVE_GLOBALS");return 0;}
 if(argc==3&&!strcmp(argv[1],"--run"))return run(argv[2]);
 if(argc==2&&!strcmp(argv[1],"--ready")){int pid;if(ready(&pid)){protocol(NULL,0,"TRACE_READY_TO_ARM");return 0;}protocol(NULL,0,last_reason);return 1;}
 if(argc!=2||(strcmp(argv[1],"--status")&&strcmp(argv[1],"--stop")&&strcmp(argv[1],"--mark")))return 2;
 int command=strcmp(argv[1],"--status");struct control c;if(!load_control(&c,command)){struct stat st;int corrupt=!lstat(VP_CURRENT,&st)||errno!=ENOENT;protocol(NULL,0,corrupt?"TRACE_REGISTRY_INVALID":"TRACE_OFF");return corrupt?1:!strcmp(argv[1],"--mark")?3:0;}
 int result=0;const char*reason=c.h->phase==VP_SAVED?"TRACE_SAVED":c.live?"TRACE_ACTIVE":"TRACE_RAM_PENDING";
 if(!command&&c.live&&c.h->phase==VP_RUNNING){struct vp_compare p=*(struct vp_compare*)((unsigned char*)c.h+VP_COMPARE_OFFSET);if(vpc_valid(&p)&&p.phase==VPC_PERFORMANCE&&p.owned)reason="TRACE_COMPARE_QOS_B";}
 if(!strcmp(argv[1],"--mark")){
  if(!c.live||c.h->phase!=VP_RUNNING){result=3;reason="TRACE_NOT_RUNNING";}
  else if(nxl_usb_physical_state()!=0||camera_busy((int)c.h->camera_pid)!=0){result=3;reason="TRACE_COMPARE_STOP_VIDEO_FIRST";}
  else{
   unsigned request=__atomic_add_fetch(&c.h->mark_request,1,__ATOMIC_RELAXED);__atomic_store_n(&c.h->control_cookie,c.h->cookie^VP_CONTROL_XOR,__ATOMIC_RELEASE);
   reason="TRACE_COMPARE_REQUESTED";
   for(unsigned i=0;i<100;i++){
    if(__atomic_load_n(&c.h->control_reserved,__ATOMIC_ACQUIRE)==request){struct vp_compare p=*(struct vp_compare*)((unsigned char*)c.h+VP_COMPARE_OFFSET);reason=vpc_reason(&p);result=strcmp(reason,"TRACE_MARKED")?3:0;break;}
    if(c.h->phase!=VP_RUNNING){reason="TRACE_NOT_RUNNING";result=3;break;}
    struct timespec pause={0,20000000};(void)clock_nanosleep(CLOCK_MONOTONIC,0,&pause,NULL);
   }
  }
 }
 if(!strcmp(argv[1],"--stop")&&c.h->phase!=VP_SAVED){if(c.live){__atomic_store_n(&c.h->stop_request,c.h->cookie^VP_STOP_XOR,__ATOMIC_RELAXED);__atomic_store_n(&c.h->control_cookie,c.h->cookie^VP_CONTROL_XOR,__ATOMIC_RELEASE);reason="TRACE_SAVE_REQUESTED";}else if(!export_buffer(c.h)){result=1;reason=last_reason;}else reason="TRACE_SAVED";}
 protocol(c.h,c.live,reason);unload(&c);return result;
}
