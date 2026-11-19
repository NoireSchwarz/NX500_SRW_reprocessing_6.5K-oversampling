/* SPDX-License-Identifier: AGPL-3.0-only. R10u passive native IPC buffer association.
 * The sampling key never arms RTOS code, attaches threads or injects calls.
 * A pre-existing verified timer is optional; absent timing is marked missing.
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
#include "../../common/video_fullwidth_blocks.h"
#include "../../common/video_fast_capture.h"
#include "../../common/video_ep_route_capture.h"
#include "../../common/video_ipc_link_capture.h"
#include "../../common/video_hevc_trace.h"
#include "../../common/video_observer_budget.h"
#include "../../common/video_output_capture.h"
#include "../../common/video_buffer_capture.h"
#include "../../common/video_stage_capture.h"
#include "../../common/video_stage_async.h"
#include "../../common/video_memory_experiment.h"
#include "../../common/video_hevc_gate.h"
#include "../../common/video_hevc_gate_identity.h"
#include "../../lens/lens_ro_guard.h"
#define VP_ROOT "/tmp/nx-lut-lite"
#define VP_RAM "/dev/shm/nx-lut-lite"
#define VP_CURRENT VP_RAM "/video-probe.current"
#define VP_LEASE VP_ROOT "/lens-evidence.fd"
#define VP_REPORT "/mnt/mmc/NXDEMO51/VIDEO_STAGE_"
#define VP_BUFFER_BYTES BV_END
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
static int header_valid(const struct vp_header*h){return !memcmp(h->magic,"NXLVCTR1",8)&&h->schema==8&&h->header_bytes==VP_HEADER_BYTES&&h->record_bytes==sizeof(struct vp_record)&&h->capacity==VP_MAX_SAMPLES&&h->count<=VP_MAX_SAMPLES&&h->phase<=VP_FAILED&&h->pid>1&&h->camera_pid>1&&h->cookie&&h->worker_ticks&&memchr(h->report,0,sizeof h->report)&&memchr(h->buffer,0,sizeof h->buffer)&&report_path(h->report)&&buffer_path(h->buffer);}
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
/* Status reads only our own RAM-file metadata, never target or device memory. */
static uint32_t dm_control_status(const struct control*c){
 struct bv_header b;
 if(pread(c->fd,&b,sizeof b,BV_OFFSET)!=(ssize_t)sizeof b||
 b.w[0]!=BV_MAGIC||b.w[1]!=8u||b.w[2]!=sizeof(struct bv_record)||
 b.w[3]!=BV_CAPACITY||b.w[12]||b.w[23]!=c->h->cookie||b.w[24]!=c->h->pid||
 b.w[25]!=(uint32_t)c->h->started_ns||b.w[26]!=(uint32_t)(c->h->started_ns>>32)||
 b.w[27]!=c->h->camera_pid||b.w[28]!=(uint32_t)c->h->camera_ticks||
 b.w[29]!=(uint32_t)(c->h->camera_ticks>>32)||b.w[30]!=c->h->modules_base)return 0;
 uint32_t v=b.w[20];unsigned status=v&255u,reason=(v>>8)&255u,progress=(v>>16)&255u;
 if((v&0xff000000u)!=DM_STATUS_TAG||status<DM_READY||status>DM_UNAVAILABLE||
 progress>3u||(status==DM_DONE&&progress!=3u)||
 ((status==DM_READY||status==DM_UNAVAILABLE)&&progress)||
 (status==DM_RUNNING&&!progress)||
 reason>8u)return 0;
 return v;
}
static void protocol(const struct vp_header*h,int live,const char*reason,uint32_t memory){
 const char*state=!h?"OFF":h->phase==VP_SAVED?"OFF":live?(h->phase==VP_RUNNING?"ON":h->phase==VP_FULL?"FULL":"SAVING"):"PENDING";
 static const char*names[]={"UNKNOWN","READY","RUNNING","DONE","ABORTED","UNAVAILABLE"};
 unsigned experiment=memory&255u,why=(memory>>8)&255u,epoch=(memory>>16)&255u;
 if(!memory){experiment=h?0:6;why=epoch=0;if(h&&h->reason&0x1000u)experiment=DM_UNAVAILABLE;}
 printf("NXL_VIDEO_PROBE3\nstate=%s\nreason=%s\nreport=%s\nsamples=%u\nperiod_ms=%u\n"
 "experiment=%s\nexperiment_epoch=%u\nexperiment_reason=%u\nend=NXL_VIDEO_PROBE_END3\n",
 state,reason,h?h->report:"",h?h->count:0,h?h->period_ms:VP_PERIOD_MS,
 experiment==6?"NONE":names[experiment],epoch,why);
}
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
static int export_fast(int d,const unsigned char*map,unsigned cookie){
 struct vf_header z=*(const struct vf_header*)(map+VF_META_OFFSET);
 const struct vf_record*r=(const struct vf_record*)(map+VF_DATA_OFFSET);
 if(memcmp(z.magic,"NXLFAST1",8)||z.schema!=4||z.record_bytes!=sizeof*r||z.capacity!=VF_CAPACITY||z.count>VF_CAPACITY)return 0;
 for(unsigned i=0;i<z.count;i++)if(r[i].sequence!=i||r[i].commit!=(vp_hash(r+i,offsetof(struct vf_record,commit))^VF_XOR))return 0;
 z.hash=vf_header_hash(&z);
 int f=openat(d,"frame-flow.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(f>=0){struct vf_header old;int ok=read(f,&old,sizeof old)==sizeof old&&!memcmp(&old,&z,sizeof z);unsigned char b[4096];size_t at=0,n=(size_t)z.count*sizeof*r;while(ok&&at<n){size_t k=n-at;if(k>sizeof b)k=sizeof b;ssize_t got=read(f,b,k);ok=got==(ssize_t)k&&!memcmp(b,(const unsigned char*)r+at,k);at+=k;}if(ok)ok=read(f,b,1)==0;if(close(f))ok=0;return ok;}
 if(errno!=ENOENT)return 0;
 char part[80];snprintf(part,sizeof part,"frame-flow-%08x-%u.part",cookie,(unsigned)getpid());
 f=openat(d,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;
 int ok=all_write(f,&z,sizeof z)&&all_write(f,r,(size_t)z.count*sizeof*r)&&!fsync(f);if(close(f))ok=0;
 struct stat st;if(ok)ok=nxl_usb_physical_state()==0&&fstatat(d,"frame-flow.bin",&st,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT&&!renameat(d,part,d,"frame-flow.bin");
 if(!ok&&nxl_usb_physical_state()==0)unlinkat(d,part,0);return ok;
}
static int export_hevc(int d,const unsigned char*map,unsigned cookie){
 struct pl_header z=*(const struct pl_header*)(map+PL_OFFSET);
 const struct pl_record*r=(const struct pl_record*)(map+PL_DATA);
 if(z.w[0]!=PL_MAGIC||z.w[1]!=2||z.w[2]!=sizeof*r||z.w[3]!=PL_CAPACITY||z.w[4]>PL_CAPACITY||z.w[12]!=512u)return 0;
 for(unsigned i=0;i<z.w[4];i++)if(r[i].sequence!=i||r[i].commit!=(vp_hash(r+i,offsetof(struct pl_record,commit))^PL_XOR))return 0;
 z.w[31]=vp_hash(&z,124);
 int f=openat(d,"hevc-flow.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(f>=0){struct pl_header old;int ok=read(f,&old,sizeof old)==sizeof old&&!memcmp(&old,&z,sizeof z);unsigned char b[4096];size_t at=0,n=(size_t)z.w[4]*sizeof*r;while(ok&&at<n){size_t k=n-at;if(k>sizeof b)k=sizeof b;ssize_t got=read(f,b,k);ok=got==(ssize_t)k&&!memcmp(b,(const unsigned char*)r+at,k);at+=k;}if(ok)ok=read(f,b,1)==0;if(close(f))ok=0;return ok;}
 if(errno!=ENOENT)return 0;
 char part[80];snprintf(part,sizeof part,"hevc-flow-%08x-%u.part",cookie,(unsigned)getpid());
 f=openat(d,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;
 int ok=all_write(f,&z,sizeof z)&&all_write(f,r,(size_t)z.w[4]*sizeof*r)&&!fsync(f);if(close(f))ok=0;
 struct stat st;if(ok)ok=nxl_usb_physical_state()==0&&fstatat(d,"hevc-flow.bin",&st,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT&&!renameat(d,part,d,"hevc-flow.bin");
 if(!ok&&nxl_usb_physical_state()==0)unlinkat(d,part,0);return ok;
}
static int export_buffers(int d,const unsigned char*map,unsigned cookie){
 struct bv_header z=*(const struct bv_header*)(map+BV_OFFSET);
 const struct bv_record*r=(const struct bv_record*)(map+BV_DATA);
 const struct bm_slot*m=(const struct bm_slot*)(map+BM_OFFSET);
 if(z.w[0]!=BV_MAGIC||(z.w[1]!=1u&&z.w[1]!=2u&&z.w[1]!=3u&&z.w[1]!=4u&&z.w[1]!=5u&&z.w[1]!=6u&&z.w[1]!=7u&&z.w[1]!=8u)||z.w[2]!=sizeof*r||z.w[3]!=BV_CAPACITY||z.w[4]>BV_CAPACITY||z.w[12]>BM_CAPACITY||z.w[13]!=sizeof*m||z.w[23]!=cookie)return 0;
 if(z.w[1]>=7u&&z.w[12])return 0;
 for(unsigned i=0;i<z.w[4];i++)if(r[i].sequence!=i||r[i].commit!=(vp_hash(r+i,offsetof(struct bv_record,commit))^BV_XOR))return 0;
 struct bm_slot const*slots[BM_CAPACITY];uint32_t heads[BM_CAPACITY][512];
 for(unsigned i=0;i<z.w[12];i++){
  slots[i]=m+i;memcpy(heads[i],m[i].h,sizeof heads[i]);
  if(heads[i][0]!=0x314d424eu||heads[i][10]!=BM_BLOCKS||heads[i][16]>BM_BYTES||heads[i][17]>BM_BYTES)return 0;
  for(unsigned pass=0;pass<2;pass++){if(heads[i][11+pass]!=vp_hash(m[i].table[pass],heads[i][16+pass]))return 0;heads[i][20+pass]=vp_hash(m[i].table[pass],BM_BYTES);}
  if(heads[i][2]==BM_READING){heads[i][2]=BM_STOPPED;z.w[21]++;}
  heads[i][511]=vp_hash(heads[i],511*4u);
 }
 z.w[31]=vp_hash(&z,124u);
 int f=openat(d,"buffer-flow.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),existing=f>=0;
 if(f<0&&errno!=ENOENT)return 0;
 char part[80];snprintf(part,sizeof part,"buffer-flow-%08x-%u.part",cookie,(unsigned)getpid());
 if(!existing){f=openat(d,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;}
 const unsigned char*parts[2+BM_CAPACITY*3];size_t bytes[2+BM_CAPACITY*3];unsigned n=0;
 parts[n]=(const unsigned char*)&z;bytes[n++]=sizeof z;parts[n]=(const unsigned char*)r;bytes[n++]=z.w[4]*sizeof*r;
 for(unsigned i=0;i<z.w[12];i++){parts[n]=(const unsigned char*)heads[i];bytes[n++]=sizeof heads[i];for(unsigned p=0;p<2;p++){parts[n]=slots[i]->table[p];bytes[n++]=BM_BYTES;}}
 int ok=1;unsigned char tmp[4096];
 for(unsigned i=0;ok&&i<n;i++)for(size_t at=0;ok&&at<bytes[i];){size_t k=bytes[i]-at;if(k>sizeof tmp)k=sizeof tmp;
  ok=existing?(all_read(f,tmp,k)&&!memcmp(tmp,parts[i]+at,k)):all_write(f,parts[i]+at,k);at+=k;}
 if(ok)ok=existing?read(f,tmp,1)==0:fsync(f)==0;if(close(f))ok=0;
 if(!existing){struct stat st;if(ok)ok=nxl_usb_physical_state()==0&&fstatat(d,"buffer-flow.bin",&st,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT&&!renameat(d,part,d,"buffer-flow.bin");if(!ok&&nxl_usb_physical_state()==0)unlinkat(d,part,0);}
 return ok;
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
 if(!export_fast(d,map,h->cookie)||!export_hevc(d,map,h->cookie)||!export_buffers(d,map,h->cookie)){last_reason="TRACE_OBSERVATION_OUTPUT_PENDING_OR_CONFLICT";goto done;}
 char b[400];m=snprintf(b,sizeof b,"NXLVIDDONE1\nsamples=%u\nrecord_bytes=%u\nbytes=%u\nlast_period_ms=%u\nmax_sample_ns=%llu\nreason_flags=%u\n",count,(unsigned)sizeof*r,VP_HEADER_BYTES+count*(unsigned)sizeof*r,final.period_ms,(unsigned long long)final.max_sample_ns,final.reason);
 if(m<=0||m>=(int)sizeof b||!receipt(d,&final,b,(unsigned)m))goto done;
 memcpy(h,&final,sizeof final);ok=1;last_reason="TRACE_SAVED";
done:if(map!=MAP_FAILED)munmap(map,VP_BUFFER_BYTES);if(input>=0)close(input);if(f>=0)close(f);if(part_created&&d>=0&&nxl_usb_physical_state()==0)unlinkat(d,part,0);if(d>=0)close(d);return ok;
}
struct mapped_page {uint32_t address;volatile unsigned char*memory;};
struct runtime {int dev,mem,pid;uint64_t start;struct mapped_page pages[32];unsigned page_count,eviction,reads,bytes;volatile unsigned char*allocator_map;unsigned allocator_map_bytes;};
static int physical_allowed(uint32_t a,unsigned n){
 if(!n||(a&3u)||(n&3u))return 0;
 if(a==0x208210d0u&&n==12u)return 1; /* exactly three existing RO status words; no ACK hole */
 if(bv_read_range(a,n))return 1;
 if(n>128u&&!(a==VP_LATENCY_DATA&&n==VP_LATENCY_BYTES))return 0;
 if(vp_ep_readback_range(a,n)||vr_read_range(a,n)||pl_read_range(a,n))return 1;
 for(unsigned i=0;i<sizeof pl_rom_guards/sizeof*pl_rom_guards;i++)if(a==pl_rom_guards[i].address&&n==32u)return 1;
 for(unsigned i=0;i<sizeof bv_rom_guards/sizeof*bv_rom_guards;i++)if(a==bv_rom_guards[i].address&&n==32u)return 1;
 if(n==4u&&(a==VC_STATUS||a==VC_ENABLE||a==0x208210b0u))return 1;
 for(unsigned j=1;j<VFW_BLOCK_COUNT;j++)if(a>=vfw_blocks[j].address&&(uint64_t)a+n<=vfw_blocks[j].address+vfw_blocks[j].bytes)return 1;
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
static int physical(void*opaque,uint32_t a,void*out,unsigned n){struct runtime*c=opaque;if(!physical_allowed(a,n))return 0;c->reads++;c->bytes+=n;
 if(a>=BM_TABLE&&(uint64_t)a+n<=BM_TABLE+BM_BYTES){
  if(!c->allocator_map){unsigned size=((BM_TABLE&4095u)+BM_BYTES+4095u)&~4095u;void*p=mmap(NULL,size,PROT_READ,MAP_SHARED,c->dev,(off_t)(uint64_t)(BM_TABLE&~4095u));if(p==MAP_FAILED)return 0;c->allocator_map=p;c->allocator_map_bytes=size;}
  unsigned at=0;while(at<n){unsigned k=4096u-((a+at)&4095u);if(k>n-at)k=n-at;if(!lens_ro_load(c->allocator_map+(a+at-(BM_TABLE&~4095u)),(unsigned char*)out+at,k))return 0;at+=k;}return 1;
 }
 unsigned copied=0;while(copied<n){uint32_t at=a+copied,base=at&~4095u;unsigned i;for(i=0;i<c->page_count;i++)if(c->pages[i].memory&&c->pages[i].address==base)break;if(i==c->page_count){int fresh=i<32u;if(!fresh){i=c->eviction++%32u;if(c->pages[i].memory&&munmap((void*)c->pages[i].memory,4096))return 0;c->pages[i].memory=NULL;}void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,c->dev,(off_t)(uint64_t)base);if(p==MAP_FAILED){c->pages[i].address=0;return 0;}c->pages[i]=(struct mapped_page){base,p};if(fresh)c->page_count++;}unsigned k=4096u-(at&4095u);if(k>n-copied)k=n-copied;if(!lens_ro_load(c->pages[i].memory+(at&4095u),(unsigned char*)out+copied,k))return 0;copied+=k;}return 1;}
static int process(void*opaque,uint32_t a,void*out,unsigned n){struct runtime*c=opaque;if(!vp_user(a,n)||!n||n>4096u)return 0;c->reads++;c->bytes+=n;return pread(c->mem,out,n,(off_t)(uint64_t)a)==(ssize_t)n;}
#include "video_stage_runtime.inc"
static int hash_library(const struct vp_library*p){char path[120];snprintf(path,sizeof path,"/usr/lib/%s",p->name);int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;struct sha256 h;sha256_init(&h);unsigned char b[8192],digest[32];unsigned bytes=0;ssize_t n;while((n=read(f,b,sizeof b))>0){bytes+=(unsigned)n;sha256_update(&h,b,(size_t)n);}int ok=n==0;if(close(f))ok=0;char hex[65];sha256_final(&h,digest);digest_hex(digest,hex);return ok&&bytes==p->bytes&&!strcmp(hex,p->sha256);}
static int library_bases(int pid,uint32_t bases[3]){char path[100];snprintf(path,sizeof path,"/proc/%d/maps",pid);FILE*f=fopen(path,"r");if(!f)return 0;char line[1200];unsigned seen[3]={0};memset(bases,0,12);while(fgets(line,sizeof line,f)){unsigned lo,hi;unsigned long long off;char mode[8],name[512];if(sscanf(line,"%x-%x %7s %llx %*s %*u %511[^\n]",&lo,&hi,mode,&off,name)!=5||off||mode[0]!='r'||!strchr(mode,'x'))continue;for(unsigned i=0;i<3;i++){char expected[100];snprintf(expected,sizeof expected,"/usr/lib/%s",vp_libraries[i].name);if(!strcmp(name,expected)){bases[i]=lo;seen[i]++;}}}int ok=!ferror(f)&&seen[0]==1&&seen[1]==1&&seen[2]==1;if(fclose(f))ok=0;return ok;}
static int profile(struct runtime*c,uint32_t bases[3]){
 char b[100],v[20],m[20];if(!read_text("/etc/version.info",b,sizeof b)||sscanf(b,"%19[^\r\n]%*[\r\n]%19[^\r\n]",v,m)!=2||strcmp(v,"1.12")||strcmp(m,"NX500"))return 0;
 for(unsigned i=0;i<3;i++)if(!hash_library(vp_libraries+i))return 0;
 if(!library_bases(c->pid,bases)){last_reason="TRACE_HEVC_GATE_RESTART_REQUIRED";return 0;}
 if(!lens_ro_begin())return 0;c->dev=open("/dev/mem",O_RDONLY|O_CLOEXEC|O_SYNC);if(c->dev<0)return 0;
 snprintf(b,sizeof b,"/proc/%d/mem",c->pid);c->mem=open(b,O_RDONLY|O_CLOEXEC);if(c->mem<0)return 0;
 for(unsigned i=0;i<sizeof vp_rom_guards/sizeof*vp_rom_guards;i++){uint32_t got[8];if(!physical(c,vp_rom_guards[i].address,got,32))return 0;if(memcmp(got,vp_rom_guards[i].words,32)){if(vp_rom_guards[i].address==0x80023c20u)last_reason="TRACE_RESTART_REQUIRED_OLD_IRQ_TRACE";return 0;}}
 for(unsigned i=0;i<sizeof pl_rom_guards/sizeof*pl_rom_guards;i++){
  uint32_t got[8];if(!physical(c,pl_rom_guards[i].address,got,32u)||memcmp(got,pl_rom_guards[i].words,32u))return 0;
 }
 for(unsigned i=0;i<sizeof bv_rom_guards/sizeof*bv_rom_guards;i++){uint32_t got[8];if(!physical(c,bv_rom_guards[i].address,got,32u)||memcmp(got,bv_rom_guards[i].words,32u))return 0;}
 /* Entire timer code must be uniformly OEM stock or this revision
  * implementation. Do not patch/repair an unknown or partial installation. */
 unsigned timer=3u;
 for(unsigned j=1;j<VFW_BLOCK_COUNT;j++)for(unsigned k=0;k<vfw_blocks[j].bytes/4;){uint32_t words[32];unsigned n=vfw_blocks[j].bytes/4-k;if(n>32)n=32;if(!physical(c,vfw_blocks[j].address+4*k,words,4*n))return 0;if(memcmp(words,vfw_stock(vfw_blocks+j)+k,4*n))timer&=~1u;if(memcmp(words,vfw_full_code(vfw_blocks+j)+k,4*n))timer&=~2u;if(!timer)return 0;k+=n;}
 return ticks(c->pid)==c->start;
}
static void runtime_close(struct runtime*c){lens_ro_end();if(c->allocator_map)munmap((void*)c->allocator_map,c->allocator_map_bytes);c->allocator_map=NULL;c->allocator_map_bytes=0;for(unsigned i=0;i<c->page_count;i++)if(c->pages[i].memory)munmap((void*)c->pages[i].memory,4096);c->page_count=0;if(c->mem>=0)close(c->mem);if(c->dev>=0)close(c->dev);c->mem=c->dev=-1;}
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
static uint64_t pl_cpu(void*c){(void)c;return clock_ns(CLOCK_PROCESS_CPUTIME_ID);}
static void vc_wait(void*c){(void)c;struct timespec t={0,1000000};(void)clock_nanosleep(CLOCK_MONOTONIC,0,&t,NULL);}
static void pl_wait(void*c){(void)c;struct timespec t={0,2000000};(void)clock_nanosleep(CLOCK_MONOTONIC,0,&t,NULL);}
#include "video_hevc_gate_runtime.inc"
static int run(const char*report){
 close_inherited();int lease=-1,buffer=-1,result=1,armed=0;char created_path[160]={0};void*map=MAP_FAILED;struct runtime c={.dev=-1,.mem=-1};struct vp_header*h=NULL;uint32_t bases[3];
 struct vz_runtime stage_runtime={.r=&c,.syscall_fd=-1,.scan={.fd=-1}};
 struct vz_io stage_io={&stage_runtime,NULL,vz_runtime_list,vz_runtime_task,vz_runtime_select,vz_runtime_fd,vz_runtime_pixel,vz_runtime_pthread_base,vz_runtime_maps_step};
 struct vp_compare compare={0},*comparison=NULL;struct hg_runtime gate={.fd=-1};
 if(!report_path(report)||!tmp_directory()){last_reason="TRACE_PRIVATE_DIRECTORY_INVALID";goto done;}
 char retired[160]={0};struct control old;if(load_control(&old,0)){int pending=old.h->phase!=VP_SAVED;if(!pending)strcpy(retired,old.h->buffer);unload(&old);if(pending){last_reason="TRACE_PENDING_SAVE_FIRST";goto done;}}else{struct stat st;if(!lstat(VP_CURRENT,&st)||errno!=ENOENT){last_reason="TRACE_REGISTRY_INVALID";goto done;}}
 lease=open(VP_LEASE,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lease<0||!regular_fd(lease,-1)||flock(lease,LOCK_EX|LOCK_NB)){last_reason="TRACE_OTHER_RECORDER_ACTIVE";goto done;}
 if(!ready(&c.pid))goto done;last_reason="TRACE_RAM_FILESYSTEM_UNAVAILABLE";if(!ram_directory())goto done;c.start=ticks(c.pid);last_reason="TRACE_FIRMWARE_OR_LIBRARY_MISMATCH";if(!profile(&c,bases)||camera_busy(c.pid)!=0)goto done;
 stage_runtime.pthread_layout_verified=vz_pthread_identity(); /* failure disables wait classification, not sampling */
 char path[160];uint64_t started=clock_ns(CLOCK_MONOTONIC);uint32_t cookie=(uint32_t)(started^(started>>32)^getpid());if(!cookie)cookie=1;snprintf(path,sizeof path,VP_RAM "/video-buffer-%u-%08x",(unsigned)getpid(),cookie);
 {struct statfs fs;if(statfs("/dev/shm",&fs)||((unsigned long)fs.f_type==0x01021994u&&(!fs.f_bsize||(uint64_t)fs.f_bavail*fs.f_bsize<VP_BUFFER_BYTES+1048576u))){last_reason="TRACE_RAM_CAPACITY_UNAVAILABLE";goto done;}}
 buffer=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);last_reason="TRACE_RAM_BUFFER_UNAVAILABLE";if(buffer<0)goto done;strcpy(created_path,path);if(ftruncate(buffer,VP_BUFFER_BYTES)||!regular_fd(buffer,VP_BUFFER_BYTES))goto done;
 map=mmap(NULL,VP_BUFFER_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,buffer,0);if(map==MAP_FAILED)goto done;memset(map,0,VP_BUFFER_BYTES);h=map;
 memcpy(h->magic,"NXLVCTR1",8);h->schema=8;h->header_bytes=VP_HEADER_BYTES;h->record_bytes=sizeof(struct vp_record);h->capacity=VP_MAX_SAMPLES;h->phase=VP_RUNNING;h->pid=(unsigned)getpid();h->camera_pid=(unsigned)c.pid;h->worker_ticks=ticks(getpid());h->camera_ticks=c.start;h->cookie=cookie;h->modules_base=bases[0];h->core_base=bases[1];h->osal_base=bases[2];h->period_ms=VF_PERIOD_MS;strcpy(h->report,report);strcpy(h->buffer,path);strcpy(h->revision,"R11f HEVC_ABA");
 struct vp_reader reader={&c,physical,process,bases[0],bases[1]};struct vp_record warm={0};(void)vp_capture(&reader,&warm);struct vp_path_ring*paths=(struct vp_path_ring*)((unsigned char*)map+VP_PATH_OFFSET);vp_path_init(paths);struct vp_clock_ring*clocks=(struct vp_clock_ring*)((unsigned char*)map+VP_CLOCK_OFFSET);vp_clock_init(clocks);struct vp_recording_watch watch={0};struct vp_qos_ring*qos=(struct vp_qos_ring*)((unsigned char*)map+VP_QOS_OFFSET);vp_qos_init(qos);struct vp_bus_ring*bus=(struct vp_bus_ring*)((unsigned char*)map+VP_BUS_OFFSET);vp_bus_init(bus);uint64_t qos_due=0;uint64_t path_due=0,clock_due=0;
 comparison=(struct vp_compare*)((unsigned char*)map+VP_COMPARE_OFFSET);vpc_init(&compare,comparison);struct vp_phase_ring*phases=(struct vp_phase_ring*)((unsigned char*)map+VP_PHASE_OFFSET);vp_phase_init(phases);uint64_t flow_since=0;unsigned flow_generation=0;uint32_t flow_key[8]={0};
 /* READY is published after the private-memory test admission check. */
 if(retired[0]&&buffer_path(retired))unlink(retired);
 /* No report-directory fd, SD stream, formatter, full-frame scan, camera
  * suspension, per-frame ioctl or background dlog process in the loop.
  * Only the original readback clock selectors are sampled, at most 0.2 Hz. */
 h->started_ns=clock_ns(CLOCK_MONOTONIC);h->cpu_started_ns=clock_ns(CLOCK_PROCESS_CPUTIME_ID);
 /* Keep the export ABI; HEVC instruction instrumentation remains disabled.
  * No fork, ptrace, thread stop, breakpoint or kernel single-step exists here. */
 struct pl_header*pipeline=(struct pl_header*)((unsigned char*)map+PL_OFFSET);pl_init(pipeline);struct pl_record*pipeline_records=(struct pl_record*)((unsigned char*)map+PL_DATA);
 struct bv_header*buffers=(struct bv_header*)((unsigned char*)map+BV_OFFSET);
 struct bv_record*buffer_records=(struct bv_record*)((unsigned char*)map+BV_DATA);
 struct bm_slot*memory_slots=(struct bm_slot*)((unsigned char*)map+BM_OFFSET);
 struct bv_state buffer_state;bv_init(buffers,&buffer_state);bv_bind(&buffer_state,&warm,h->started_ns);
 struct vz_state stage_state;vz_init(buffers,&stage_state);vz_async_init(buffers,&stage_state);
 buffers->w[23]=h->cookie;buffers->w[24]=h->pid;buffers->w[25]=(uint32_t)h->started_ns;buffers->w[26]=(uint32_t)(h->started_ns>>32);buffers->w[27]=h->camera_pid;buffers->w[28]=(uint32_t)h->camera_ticks;buffers->w[29]=(uint32_t)(h->camera_ticks>>32);
 buffers->w[30]=h->modules_base;
 buffers->w[1]=8u;
 if(!hg_open(&gate,&c,bases[1],cookie))goto done;
 hg_publish(&gate,buffers);(void)hg_log(&gate,buffers,buffer_records,clock_ns(CLOCK_MONOTONIC),1u);
 last_reason="TRACE_REGISTRY_OR_READY_FAILED";if(!ready_file(h)||!make_current(h))goto done;armed=1;
 struct vf_header*fast=(struct vf_header*)((unsigned char*)map+VF_META_OFFSET);vf_init(fast);struct vr_header*routes=(struct vr_header*)((unsigned char*)map+VR_OFFSET);vr_init(routes);struct vf_record*fast_records=(struct vf_record*)((unsigned char*)map+VF_DATA_OFFSET);struct vo_budget budget;vo_init(&budget,h->started_ns);budget.heavy_ms=500u;budget.target_ms=5000u;budget.dense_ms=10000u;uint64_t heavy_due=h->started_ns,target_due=h->started_ns;
 uint64_t next=h->started_ns,identity_due=next+1000000000u,idle_since=0,cpu_due=0;uint32_t previous[8]={0},epoch=0,last_mark=0,cpu[2]={0};struct vp_record*records=(struct vp_record*)((unsigned char*)map+VP_HEADER_BYTES);
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
  if(h->phase==VP_STOPPING){hg_end(&gate,buffers,buffer_records,stamp,2u);vz_async_finish(&reader,buffers,buffer_records,&stage_state,&stage_io,vc_now,NULL,11u);int busy=camera_busy(c.pid);if(!busy){if(!idle_since)idle_since=stamp;if(stamp-idle_since>=1000000000u){if(!vpc_restore(&compare,comparison)){h->phase=VP_FAILED;h->reason|=512u;last_reason="TRACE_COMPARE_RESTORE_FAILED";break;}runtime_close(&c);if(export_buffer(h)){result=0;break;}h->phase=VP_FAILED;h->reason|=8u;last_reason="TRACE_EXPORT_FAILED_RAM_RETAINED";break;}}else idle_since=0;}
  else{
   uint64_t class_cpu=pl_cpu(NULL);
   if(!vf_capture(&reader,fast,fast_records,vc_now,NULL)){h->phase=VP_FULL;h->reason|=0x800u;}
   uint64_t fast_cost=vo_cost(class_cpu,pl_cpu(NULL));budget.fast+=fast_cost;fast->cpu_ns+=fast_cost;
   if(stamp<heavy_due||h->count>=VP_MAX_SAMPLES)goto next_drain;heavy_due=stamp+(uint64_t)budget.heavy_ms*1000000u;class_cpu=pl_cpu(NULL);
   struct vp_record*s=records+h->count;memset(s,0,sizeof*s);s->begin_ns=stamp;s->sequence=h->count;s->period_ms=budget.heavy_ms;c.reads=c.bytes=0;(void)vp_capture(&reader,s);bv_bind(&buffer_state,s,stamp);(void)vp_latency_capture(&reader,s);vp_flow_capture(&reader,s);if(stamp>=path_due){vp_completion_capture(&reader,s,vc_now,vc_wait,NULL,compare.phase,compare.result,compare.current);vp_ipc_link_capture(&reader,s,vc_now,NULL);}
   if(stamp>=path_due&&vp_video_flow_mode(s)&&(s->valid&(VP_POLICY|VP_BACKEND))==(VP_POLICY|VP_BACKEND)){
    int active=vp_recording_active(&watch,s);unsigned phase=active?1u:0u;
    uint32_t elapsed=(uint32_t)((stamp-h->started_ns)/1000000u);
    vp_path_capture(&reader,paths,s,elapsed);
    if(stamp>=clock_due){uint32_t ddr[2];ddr_report(ddr);vp_clock_capture(&reader,clocks,elapsed,ddr[0],ddr[1]);clock_due=stamp+5000000000u;}
    if(stamp>=qos_due){vp_qos_capture(&reader,qos,s,elapsed,phase);vp_bus_capture(&reader,bus,s,elapsed,phase);qos_due=stamp+5000000000u;}
    uint32_t key[8]={s->rtos[0],s->rtos[2],s->hevc[9],s->source[0],s->source[5],s->flow[2],s->flow[3],phase};
    if(!flow_since||memcmp(flow_key,key,sizeof key)){memcpy(flow_key,key,sizeof key);flow_generation++;flow_since=stamp;}
    if(stamp-flow_since>=2000000000u&&vr_needed(routes,flow_generation))vr_capture(&reader,routes,s,elapsed,flow_generation,vc_now,NULL);
    if(stamp-flow_since>=2000000000u&&!(s->valid&VP_CHANGED)&&vp_phase_needed(phases,flow_generation)){uint32_t ddr[2];ddr_report(ddr);vp_phase_capture(&reader,phases,s,elapsed,flow_generation,phase,last_mark,ddr);}
    path_due=stamp+1000000000u;
   }
   s->read_calls=c.reads;s->read_bytes=c.bytes;
   if(stamp>=cpu_due){cpu_report(cpu);cpu_due=stamp+1000000000u;}s->reserved[4]=cpu[0];s->reserved[5]=cpu[1];
   uint32_t keys[8]={(s->valid&VP_CAMERA)?s->camera[0]:0,(s->valid&VP_SOURCE)?s->source[0]:0,(s->valid&VP_HEVC)?s->hevc[0]:0,(s->valid&VP_POLICY)?s->rtos[0]:0,(s->valid&VP_ENCODER)?s->encoder[0]:0,s->rtos[2],s->flow[2],s->flow[3]};if(h->count&&memcmp(keys,previous,sizeof keys))epoch++;memcpy(previous,keys,sizeof keys);s->epoch=epoch;
   s->mark=last_mark;s->end_ns=clock_ns(CLOCK_MONOTONIC);s->cpu_ns=clock_ns(CLOCK_PROCESS_CPUTIME_ID);uint64_t duration=s->end_ns-s->begin_ns;if(duration>h->max_sample_ns)h->max_sample_ns=duration;h->total_sample_ns+=duration;
   s->commit=vp_hash(s,offsetof(struct vp_record,commit))^0x91d6037bu;__sync_synchronize();h->count++;
   if(h->count==VP_MAX_SAMPLES){h->phase=VP_FULL;h->reason|=16u;}
   budget.heavy+=vo_cost(class_cpu,pl_cpu(NULL));
  }
 next_drain:
  next+=(uint64_t)VF_PERIOD_MS*1000000u;stamp=clock_ns(CLOCK_MONOTONIC);if(next<=stamp){h->deadline_misses++;next=stamp+(uint64_t)h->period_ms*1000000u;}
  /* Heavy/fast capacities do not silently disable the independent windows. */
  if(h->phase==VP_RUNNING||h->phase==VP_FULL){
   const struct vf_record*latest=fast->count?fast_records+fast->count-1:NULL;
   int native_ok=latest&&(latest->valid&3u)==3u&&latest->driver==0x1717u&&latest->preset==0xe005u&&vp_timer_schema(latest->latency)==11u;
   hg_tick(&gate,&reader,buffers,buffer_records,&buffer_state,native_ok,clock_ns(CLOCK_MONOTONIC));
   if(gate.status==DM_ABORTED)h->reason|=0x2000u;
   /* Disable dense windows in every A/B/A condition, including the warmup.
    * Preserve 100ms RTOS drains, light heavy snapshots and clock readbacks. */
   int matched_experiment=1;
   uint64_t class_cpu=pl_cpu(NULL);
   if(!matched_experiment&&stamp>=target_due&&next>stamp+8000000u&&!(pipeline->w[6]&PL_FULL)){
    pl_sample(&reader,pipeline,pipeline_records,vc_now,NULL,0,0,PL_TARGET,NULL);
    target_due=stamp+(uint64_t)budget.target_ms*1000000u;
   }
   /* Cold routing, bounded stage windows and allocator work have separate
    * CPU ledgers. Each window yields before the next100ms ring drain. */
   budget.dense+=vo_cost(class_cpu,pl_cpu(NULL));
   class_cpu=pl_cpu(NULL);uint64_t metadata_before=stage_state.metadata_cpu;buffers->w[10]=budget.stage_ms;
   if(!matched_experiment)vz_async_tick(&reader,buffers,buffer_records,&buffer_state,&stage_state,&stage_io,vc_now,pl_cpu,pl_wait,NULL,next);
   else vz_async_finish(&reader,buffers,buffer_records,&stage_state,&stage_io,vc_now,NULL,11u);
   uint64_t stage_cost=vo_cost(class_cpu,pl_cpu(NULL)),metadata_cost=stage_state.metadata_cpu-metadata_before;
   budget.metadata+=metadata_cost;budget.stage+=stage_cost>=metadata_cost?stage_cost-metadata_cost:0;
   stage_state.total_cpu+=stage_cost;buffers->w[15]=(uint32_t)stage_state.total_cpu;buffers->w[16]=(uint32_t)(stage_state.total_cpu>>32);
   class_cpu=pl_cpu(NULL);/* R11d omits CAP allocator scans in every condition, including baseline.
    * Existing R11c recording tables are retained as evidence; do not introduce
    * a 22-second route-table scan as a condition-dependent confounder. */
   (void)memory_slots;
   budget.memory+=vo_cost(class_cpu,pl_cpu(NULL));
   if(vo_check(&budget,clock_ns(CLOCK_MONOTONIC))){
    pipeline->w[10]=budget.dense_ms;pipeline->w[14]=budget.target_ms;
    if(budget.heavy_over)h->reason|=32u;
    pl_budget(pipeline,pipeline_records,clock_ns(CLOCK_MONOTONIC),&budget);vz_ledger(buffers,buffer_records,clock_ns(CLOCK_MONOTONIC),&budget);vo_reset_costs(&budget);
   }
  }
  struct timespec until={(time_t)(next/1000000000u),(long)(next%1000000000u)};while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&until,NULL)==EINTR&&!interrupted){}
 }
 hg_end(&gate,buffers,buffer_records,clock_ns(CLOCK_MONOTONIC),2u);
 vz_async_finish(&reader,buffers,buffer_records,&stage_state,&stage_io,vc_now,NULL,11u);
 if(h->phase!=VP_SAVED){h->finished_ns=clock_ns(CLOCK_MONOTONIC);h->cpu_finished_ns=clock_ns(CLOCK_PROCESS_CPUTIME_ID);}
done:hg_close(&gate);if(comparison&&compare.owned&&!vpc_restore(&compare,comparison)){if(h)h->reason|=512u;last_reason="TRACE_COMPARE_RESTORE_FAILED";result=1;}vz_runtime_close(&stage_runtime);runtime_close(&c);if(h&&result&&h->phase==VP_RUNNING){h->phase=VP_FAILED;h->reason|=64u;}if(map!=MAP_FAILED)munmap(map,VP_BUFFER_BYTES);if(buffer>=0)close(buffer);if(!armed&&created_path[0])unlink(created_path);if(lease>=0)close(lease);if(result)fprintf(stderr,"%s\n",last_reason);return result;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_VIDEO_PROBE=R11f; HEVC_ABA_BUFFER8_FAST4_RTOS11; NORMAL_QUARTER_NORMAL_30S; LEASED_COMMAND_MAILBOX_ONLY; NO_PRIVATE_MEMORY_STRESS; PASSIVE_START_NO_RTOS_ARM; OPTIONAL_GUARDED_FRAME_TIMER; FAST_RING_DRAIN_100MS; HEAVY_SNAPSHOTS_ADAPTIVE; HEVC_BREAKPOINTS_DISABLED; READ_ONLY_NATIVE_OBSERVATIONS_OWN_MAILBOX_WRITES; NO_FORK_NO_PTRACE_NO_SIGNALS_TO_CAMERA; NO_DEVICE_WRITES; NO_IOCTL_NO_SD_HOTPATH; IDLE_EXPORT_FOUR_FILES");return 0;}
 if(argc==3&&!strcmp(argv[1],"--run"))return run(argv[2]);
 if(argc==2&&!strcmp(argv[1],"--ready")){int pid;if(ready(&pid)){protocol(NULL,0,"TRACE_READY_TO_ARM",0);return 0;}protocol(NULL,0,last_reason,0);return 1;}
 if(argc!=2||(strcmp(argv[1],"--status")&&strcmp(argv[1],"--stop")&&strcmp(argv[1],"--mark")))return 2;
 int command=strcmp(argv[1],"--status");struct control c;if(!load_control(&c,command)){struct stat st;int corrupt=!lstat(VP_CURRENT,&st)||errno!=ENOENT;protocol(NULL,0,corrupt?"TRACE_REGISTRY_INVALID":"TRACE_OFF",0);return corrupt?1:!strcmp(argv[1],"--mark")?3:0;}
 int result=0;const char*reason=c.h->phase==VP_SAVED?"TRACE_SAVED":c.live?"TRACE_ACTIVE":"TRACE_RAM_PENDING";
 if(c.live&&c.h->phase==VP_RUNNING){if(c.h->reason&0x1000u)reason="TRACE_MEMORY_TEST_UNAVAILABLE";else if(c.h->reason&0x2000u)reason="TRACE_MEMORY_TEST_ABORTED";}
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
 protocol(c.h,c.live,reason,dm_control_status(&c));unload(&c);return result;
}
