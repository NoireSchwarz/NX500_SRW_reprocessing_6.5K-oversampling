/* SPDX-License-Identifier: AGPL-3.0-only
 * NX5001.12: preserve a verified native USB selection and repair ONLY an
 * existing truncated key, on the same inode, from that captured selection.
 * No guessed defaults, daemon restart, USB sysfs writes or firmware patch.
 * No persistent file descriptors or polling process. Invoked at boundaries.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "sha256.h"
#include "../../common/observer_usb.h"
#define USB_KEY_DIR "/opt/var/kdb/db/usb"
#define USB_STATE_DIR "/opt/usr/nx-lut-lite/state"
#define USB_VCONF_LIB "/usr/lib/libvconf.so.0.2.45"
#define USB_SUPPORTED "/usr/share/deviced/usb-configurations/conf-supported.xml"
#define GOOD_FILE "last-good.bin"
struct saved_key {uint32_t w[12];unsigned char digest[32];};
struct key_view {struct stat s;unsigned char raw[8];unsigned n;};
static const char *reason="USB_CONFIG_NOT_CHECKED",*phase="CHECK";
static int own=-1,keydir=-1,own_lock=-1,changed,repaired,profile_verified;
static uint64_t stamp(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
static int valid_mode(uint32_t m){return m==0||m==1||m==2||m==10;}
static void digest(const void*p,size_t n,unsigned char out[32]){struct sha256 s;sha256_init(&s);sha256_update(&s,p,n);sha256_final(&s,out);}
static int same_identity(const struct stat*a,const struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink;}
static int same_stat(const struct stat*a,const struct stat*b){return same_identity(a,b)&&a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctime==b->st_ctime&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int private_file(int f,size_t bytes){struct stat s;return!fstat(f,&s)&&S_ISREG(s.st_mode)&&s.st_uid==0&&s.st_nlink==1&&!(s.st_mode&0077)&&s.st_size==(off_t)bytes;}
static int write_all(int f,const void*data,size_t n){const unsigned char*p=data;unsigned tries=0;while(n&&tries++<20){ssize_t got=write(f,p,n);if(got<0&&errno==EINTR)continue;if(got<=0)return 0;p+=got;n-=(size_t)got;}return!n;}
static int hash_file(const char*path,const char*expected){
 int f=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;struct stat a={0},b={0};int ok=!fstat(f,&a)&&S_ISREG(a.st_mode)&&a.st_size>0&&a.st_size<1048576;
 struct sha256 s;sha256_init(&s);unsigned char buf[4096],hash[32];ssize_t n=0;
 if(ok)while((n=read(f,buf,sizeof buf))>0)sha256_update(&s,buf,(size_t)n);
 if(n<0||fstat(f,&b)||!same_stat(&a,&b))ok=0;if(close(f))ok=0;
 char text[65];sha256_final(&s,hash);for(unsigned i=0;i<32;i++)snprintf(text+2*i,3,"%02x",hash[i]);return ok&&!strcmp(text,expected);
}
static int firmware(void){
 char b[100]={0},v[20],m[20];int f=open("/etc/version.info",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;ssize_t n=read(f,b,sizeof b-1);int ok=n>0&&n<(ssize_t)sizeof b-1;if(close(f))ok=0;
 return ok&&sscanf(b,"%19[^\r\n]%*[\r\n]%19[^\r\n]",v,m)==2&&!strcmp(v,"1.12")&&!strcmp(m,"NX500")&&hash_file(USB_VCONF_LIB,"5397ce18fe3ebee5baad85089b9a103f51dc5d5d12f0a42c9e7e813014d11722")&&hash_file(USB_SUPPORTED,"7e4991e6acad08d163ee0e13ec7829dbeca40f0d075e30ccf62cedd392287bba")&&hash_file("/usr/lib/libmmutil_movie.so.0.0.0","269f7c69ad4448bfe01f71789132996df703b6ccecdff2a0089fd2bc9503ef3d")&&hash_file("/usr/lib/libuser-spec.so","2d04ab487cc9980cb87e513b25ff09011839203756d28857eedfd50a9cc730ec");
}
static int open_owned(void){
 int root=open(USB_STATE_DIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)return 0;struct stat s;int ok=!fstat(root,&s)&&s.st_uid==0&&!(s.st_mode&0022);
 if(ok&&mkdirat(root,"usb-config",0700)&&errno!=EEXIST)ok=0;
 if(ok){own=openat(root,"usb-config",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);ok=own>=0&&!fstat(own,&s)&&s.st_uid==0&&!(s.st_mode&0077);}
 if(close(root))ok=0;if(!ok)return 0;
 own_lock=openat(own,"guard.fd",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 return own_lock>=0&&private_file(own_lock,0)&&!flock(own_lock,LOCK_EX|LOCK_NB);
}
static int atom(const char*name,const void*data,size_t n){
 char temp[100];snprintf(temp,sizeof temp,".write-%d-%llu",getpid(),(unsigned long long)stamp());
 int f=openat(own,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;
 int ok=write_all(f,data,n)&&!fdatasync(f);if(close(f))ok=0;
 if(ok&&renameat(own,temp,own,name))ok=0;if(ok&&fsync(own))ok=0;
 if(!ok)(void)unlinkat(own,temp,0);return ok;
}
static void save_digest(struct saved_key*b){digest(b->w,sizeof b->w,b->digest);}
static int load_saved(struct saved_key*b){
 memset(b,0,sizeof*b);
 int f=openat(own,GOOD_FILE,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;
 int ok=private_file(f,sizeof*b)&&pread(f,b,sizeof*b,0)==sizeof*b;if(close(f))ok=0;
 unsigned char d[32];digest(b->w,sizeof b->w,d);return ok&&b->w[0]==0x31534255u&&b->w[1]==1u&&b->w[10]==0x29u&&valid_mode(b->w[11])&&!memcmp(d,b->digest,32);
}
static void make_saved(struct saved_key*b,const struct key_view*v){
 memset(b,0,sizeof*b);b->w[0]=0x31534255u;b->w[1]=1;
 uint64_t dev=v->s.st_dev,ino=v->s.st_ino;b->w[2]=(uint32_t)dev;b->w[3]=(uint32_t)(dev>>32);b->w[4]=(uint32_t)ino;b->w[5]=(uint32_t)(ino>>32);
 b->w[6]=v->s.st_uid;b->w[7]=v->s.st_gid;b->w[8]=v->s.st_mode;b->w[9]=v->s.st_nlink;memcpy(b->w+10,v->raw,8);save_digest(b);
}
static int saved_identity(const struct saved_key*b,const struct key_view*v){
 struct key_view full=*v;memcpy(full.raw,b->w+10,8);full.n=8;struct saved_key expected;make_saved(&expected,&full);return!memcmp(expected.w,b->w,10*4);
}
static int lock_key(int writable){
 int f=openat(keydir,"sel_mode",(writable?O_RDWR:O_RDONLY)|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(f<0)return-1;
 struct flock l={0};l.l_type=writable?F_WRLCK:F_RDLCK;l.l_whence=SEEK_SET;
 if(fcntl(f,F_SETLK,&l)){close(f);return-1;}return f;
}
static int view(int f,struct key_view*v){
 struct stat after,path;memset(v,0,sizeof*v);
 if(fstat(f,&v->s)||!S_ISREG(v->s.st_mode)||v->s.st_uid!=0||v->s.st_nlink!=1||v->s.st_size<0||v->s.st_size>8)return 0;
 v->n=(unsigned)v->s.st_size;unsigned char again[8]={0};
 if(pread(f,v->raw,v->n,0)!=(ssize_t)v->n||pread(f,again,v->n,0)!=(ssize_t)v->n||memcmp(v->raw,again,v->n)||fstat(f,&after)||!same_stat(&v->s,&after))return 0;
 return!fstatat(keydir,"sel_mode",&path,AT_SYMLINK_NOFOLLOW)&&same_stat(&v->s,&path);
}
static int valid(const struct key_view*v){uint32_t a[2];memcpy(a,v->raw,8);return v->n==8&&a[0]==0x29u&&valid_mode(a[1]);}
static int truncated(const struct key_view*v){static const unsigned char tag[4]={0x29,0,0,0};return v->n<8&&!memcmp(v->raw,tag,v->n<4?v->n:4);}
static void publish(const char*line,size_t n){
 /* Optional one-line evidence on the card. Never keep an SD descriptor or
  * force a card sync, and do not export during USB connection/unknown state. */
 if(nxl_observer_usb_state()!=0)return;
 int d=open("/mnt/mmc/NXDEMO51/logs",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)return;
 int f=openat(d,"usb-state.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);close(d);
 if(f>=0){struct stat s;if(!fstat(f,&s)&&S_ISREG(s.st_mode)&&s.st_nlink==1)(void)write_all(f,line,n);close(f);}
}
static int log_event(const char*event,const struct key_view*v,int durable){
 char raw[17]={0};if(v)for(unsigned i=0;i<v->n;i++)snprintf(raw+2*i,3,"%02x",v->raw[i]);
 char line[700];int n=snprintf(line,sizeof line,"USBKEY1 mono_ns=%llu pid=%d phase=%s event=%s reason=%s bytes=%u raw=%s recovered=%d device=%llu inode=%llu uid=%u mode=%o usb_physical=%d physical_errno=%d usb_event=%d event_errno=%d event_tag=%u event_raw=%u event_bytes=%lld\n",(unsigned long long)stamp(),getpid(),phase,event,reason,v?v->n:0,raw,repaired,v?(unsigned long long)v->s.st_dev:0,v?(unsigned long long)v->s.st_ino:0,v?(unsigned)v->s.st_uid:0,v?(unsigned)v->s.st_mode:0,nxl_usb_last_physical,nxl_usb_physical_errno,nxl_usb_last_event,nxl_usb_event_errno,nxl_usb_event_type,nxl_usb_event_raw,nxl_usb_event_bytes);
 if(n<0||n>=(int)sizeof line)return 0;
 int f=openat(own,"events.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;struct stat s;
 int ok=!fstat(f,&s)&&S_ISREG(s.st_mode)&&s.st_uid==0&&s.st_nlink==1&&!(s.st_mode&0077)&&write_all(f,line,(size_t)n);
 if(ok&&durable&&fdatasync(f))ok=0;if(close(f))ok=0;if(ok)publish(line,(size_t)n);return ok;
}
static int capture(const struct key_view*v){
 struct saved_key b,old;make_saved(&b,v);if(load_saved(&old)&&!memcmp(&old,&b,sizeof b))return 1;
 if(!atom(GOOD_FILE,&b,sizeof b)){reason="USB_CONFIG_BACKUP_WRITE_FAILED";return 0;}changed=1;return 1;
}
static int recover(int f,struct key_view*v,const struct saved_key*good){
 if(!truncated(v)||!saved_identity(good,v)){reason="USB_CONFIG_NO_MATCHING_BASELINE";return 0;}
 if(nxl_observer_usb_state()!=0){reason="USB_CONFIG_RECOVERY_REQUIRES_DISCONNECTED";return 0;}
 /* Preserve the original damaged bytes AND metadata before the only native
  * key write. atom() acts only in this module's own private state directory. */
 struct {uint32_t magic,n;struct saved_key known;unsigned char raw[8];} evidence;
 memset(&evidence,0,sizeof evidence);evidence.magic=0x31444255u;evidence.n=v->n;evidence.known=*good;memcpy(evidence.raw,v->raw,8);
 char name[100];snprintf(name,sizeof name,"damage-%d-%llu.bin",getpid(),(unsigned long long)stamp());
 if(!atom(name,&evidence,sizeof evidence)){reason="USB_CONFIG_EVIDENCE_WRITE_FAILED";return 0;}
 reason="USB_CONFIG_TRUNCATED_KEY_CONFIRMED";if(!log_event("BEFORE_RECOVERY",v,1)){reason="USB_CONFIG_EVIDENCE_LOG_FAILED";return 0;}
 if(nxl_observer_usb_state()!=0){reason="USB_CONFIG_RECOVERY_REQUIRES_DISCONNECTED";return 0;}
 /* Write through the SAME locked inode. Never truncate/replace the native
  * key: this preserves native record locks, watchers, ownership and modes. */
 const unsigned char*p=(const unsigned char*)(good->w+10);unsigned off=0,tries=0;
 while(off<8&&tries++<20){ssize_t n=pwrite(f,p+off,8-off,(off_t)off);if(n<0&&errno==EINTR)continue;if(n<=0){reason="USB_CONFIG_RECOVERY_WRITE_FAILED";return 0;}off+=(unsigned)n;}
 if(off!=8||fdatasync(f)){reason="USB_CONFIG_RECOVERY_SYNC_FAILED";return 0;}
 struct key_view after;if(!view(f,&after)||!same_identity(&v->s,&after.s)||!valid(&after)||memcmp(after.raw,p,8)){reason="USB_CONFIG_RECOVERY_READBACK_FAILED";return 0;}
 *v=after;repaired=1;reason="USB_CONFIG_RESTORED_VERIFIED";if(!log_event("RECOVERY_VERIFIED",v,1)){reason="USB_CONFIG_RECOVERY_LOG_FAILED";return 0;}return 1;
}
static int check(int preflight){
 reason="USB_CONFIG_FIRMWARE_IDENTITY_FAILED";if(!firmware())return 0;
 profile_verified=1;
 reason="USB_CONFIG_PRIVATE_STATE_UNAVAILABLE";if(!open_owned())return 0;
 reason="USB_CONFIG_KEY_DIRECTORY_UNAVAILABLE";keydir=open(USB_KEY_DIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(keydir<0)return 0;
 reason="USB_CONFIG_KEY_BUSY_OR_UNREADABLE";int f=lock_key(0);if(f<0)return 0;struct key_view v;int ok=view(f,&v);
 if(!ok){reason="USB_CONFIG_KEY_UNSTABLE_OR_UNSUPPORTED";close(f);return 0;}
 if(!valid(&v)){
  struct saved_key good;int known=load_saved(&good);if(close(f)){reason="USB_CONFIG_KEY_CLOSE_FAILED";return 0;}
  reason="USB_CONFIG_NO_MATCHING_BASELINE";if(!known||!truncated(&v)||!saved_identity(&good,&v)){(void)log_event("REFUSED",&v,1);return 0;}
  reason="USB_CONFIG_KEY_BUSY_OR_UNREADABLE";f=lock_key(1);if(f<0)return 0;
  struct key_view latest;ok=view(f,&latest);
  if(ok&&valid(&latest)){v=latest;ok=capture(&v);}
  else if(ok&&same_stat(&v.s,&latest.s)&&v.n==latest.n&&!memcmp(v.raw,latest.raw,v.n))ok=recover(f,&latest,&good),v=latest;
  else{reason="USB_CONFIG_KEY_CHANGED_BEFORE_RECOVERY";ok=0;}
 }else ok=capture(&v);
 if(close(f)){reason="USB_CONFIG_KEY_CLOSE_FAILED";ok=0;}if(!ok)return 0;
 int state=nxl_observer_usb_state();
 if(preflight&&state!=0){reason=state>0?"USB_CONNECTED_DISCONNECT_FIRST":"USB_PHYSICAL_STATE_UNAVAILABLE";(void)log_event("VIDEO_REFUSED",&v,0);return 0;}
 reason=repaired?"USB_CONFIG_RESTORED_VERIFIED":changed?"USB_CONFIG_VALID_BASELINE_SAVED":"USB_CONFIG_VALID_UNCHANGED";
 if(!log_event("CHECKPOINT",&v,0)){reason="USB_CONFIG_CHECKPOINT_WRITE_FAILED";return 0;}return 1;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_USB_GUARD=R9u; NX5001.12; NATIVE_PHYSICAL_JACK; EVENT_DIAGNOSTICS_ONLY; VERIFIED_LAST_GOOD; SAME_INODE_TRUNCATION_RECOVERY; NO_DEFAULTS; NO_USB_SYSFS_WRITES; NO_DAEMON_RESTART");return 0;}
 if(argc<2||argc>3||(strcmp(argv[1],"check")&&strcmp(argv[1],"preflight")))return 2;
 if(argc==3){size_t n=strlen(argv[2]);if(!n||n>48)return 2;for(unsigned i=0;i<n;i++)if((argv[2][i]<'A'||argv[2][i]>'Z')&&argv[2][i]!='_'&&(argv[2][i]<'0'||argv[2][i]>'9'))return 2;phase=argv[2];}
 umask(077);(void)nxl_observer_usb_state();int ok=check(!strcmp(argv[1],"preflight"));
 if(keydir>=0&&close(keydir)){ok=0;reason="USB_CONFIG_RESOURCE_CLOSE_FAILED";}if(own_lock>=0&&close(own_lock)){ok=0;reason="USB_CONFIG_RESOURCE_CLOSE_FAILED";}if(own>=0&&close(own)){ok=0;reason="USB_CONFIG_RESOURCE_CLOSE_FAILED";}
 if(!ok&&profile_verified){char line[500];int n=snprintf(line,sizeof line,"USBKEY1 mono_ns=%llu pid=%d phase=%s event=CHECK_FAILED reason=%s usb_physical=%d physical_errno=%d usb_event=%d event_errno=%d event_tag=%u event_raw=%u event_bytes=%lld\n",(unsigned long long)stamp(),getpid(),phase,reason,nxl_usb_last_physical,nxl_usb_physical_errno,nxl_usb_last_event,nxl_usb_event_errno,nxl_usb_event_type,nxl_usb_event_raw,nxl_usb_event_bytes);if(n>0&&n<(int)sizeof line)publish(line,(size_t)n);}
 printf("NXL_USB_GUARD1\nreason=%s\nrecovered=%d\nbaseline_changed=%d\nusb_physical=%d\nphysical_errno=%d\nusb_event=%d\nevent_errno=%d\nevent_tag=%u\nevent_raw=%u\nevent_bytes=%lld\nend=NXL_USB_GUARD_END1\n",reason,repaired,changed,nxl_usb_last_physical,nxl_usb_physical_errno,nxl_usb_last_event,nxl_usb_event_errno,nxl_usb_event_type,nxl_usb_event_raw,nxl_usb_event_bytes);return ok?0:1;
}
