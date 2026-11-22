/* SPDX-License-Identifier: AGPL-3.0-only
 * Original NX500 1.12 SoftAP shell entry. Process memory is read-only.
 * No tethering ABI guesses, injected callbacks, ISP or network daemon replacement.
 */
#ifndef NXL_WIFI_HOTSPOT_H
#define NXL_WIFI_HOTSPOT_H
#include "wifi_hotspot_contract.h"
#define HOT_OWNER WIFI_TMP "/wifi-hotspot.owner"
#define HOT_APP "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
struct hot_state{int valid,state,ready,owned,phase,security;pid_t pid;unsigned long long tick;uint32_t callback;char ssid[33],password[64],ip[32];};
static int hot_read(int fd,uint32_t at,void*out,size_t n){return pread(fd,out,n,(off_t)at)==(ssize_t)n;}
/* Native pref_get_addr(0) PFN mapping; observed and independently pinned.
 * A readable /proc/maps VMA does not make /proc/PID/mem work for /dev/mem.
 * Follow the existing native-refresh RO shared-page method, restricted to
 * the one preference page holding the current factory hotspot password.
 */
#define HOT_PREF_PHYSICAL 0x85480000u
#define HOT_PREF_BYTES 0x11000u
#define HOT_PASSWORD_OFFSET 0x106e3u
#define HOT_PASSWORD_PAGE 0x85490000u
static int hot_preference_mapping(pid_t pid,uint32_t pointer){
 char path[80],line[512];snprintf(path,sizeof path,"/proc/%ld/maps",(long)pid);FILE*f=fopen(path,"r");if(!f)return 0;
 unsigned found=0;while(fgets(line,sizeof line,f)){unsigned lo,hi,offset;char perms[8],dev[32],file[300];unsigned long inode;
  if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&offset,dev,&inode,file)!=7)continue;
  if(strcmp(file,"/dev/mem")||strcmp(perms,"rw-s")||offset!=HOT_PREF_PHYSICAL||lo<0x10000u||hi<=lo||(lo&4095u)||(hi&4095u))continue;
  if((uint64_t)hi-lo!=HOT_PREF_BYTES||pointer!=lo||(uint64_t)pointer+HOT_PASSWORD_OFFSET+64u>hi)continue;found++;
 }
 int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;return ok;
}
static int hot_password(pid_t pid,uint32_t pointer,char password[64]){
 if(!hot_preference_mapping(pid,pointer))return 0;
 int f=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;
 void*view=mmap(NULL,4096,PROT_READ,MAP_SHARED,f,(off_t)(uint64_t)HOT_PASSWORD_PAGE);int saved=errno;
 if(close(f)){if(view!=MAP_FAILED)(void)munmap(view,4096);return 0;}if(view==MAP_FAILED){errno=saved;return 0;}
 volatile const unsigned char*bytes=(volatile const unsigned char*)view+(HOT_PASSWORD_OFFSET&4095u);unsigned char a[64],b[64];
 __sync_synchronize();for(unsigned i=0;i<64;i++)a[i]=bytes[i];__sync_synchronize();for(unsigned i=0;i<64;i++)b[i]=bytes[i];__sync_synchronize();
 if(munmap(view,4096))return 0;if(memcmp(a,b,64)||!hot_preference_mapping(pid,pointer))return 0;memcpy(password,a,64);return 1;
}
static int hot_camera(pid_t*out,unsigned long long*tick){
 DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;pid_t found=0;
 while((e=readdir(d))){if(!e->d_name[0]||strspn(e->d_name,"0123456789")!=strlen(e->d_name))continue;char path[96],exe[PATH_MAX];
  long v=strtol(e->d_name,NULL,10);if(v<=1||v>INT_MAX)continue;snprintf(path,sizeof path,"/proc/%ld/exe",v);ssize_t n=readlink(path,exe,sizeof exe-1);if(n<=0)continue;exe[n]=0;if(strcmp(exe,HOT_APP))continue;if(found){found=0;break;}found=(pid_t)v;
 }closedir(d);if(!found||!(*tick=proc_ticks(found)))return 0;*out=found;return 1;
}
static int hot_owner(struct hot_state*s){
 int fd=open(HOT_OWNER,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return 0;struct stat st;char b[128],extra;long pid;unsigned long long tick;int phase;
 ssize_t n=read(fd,b,sizeof b-1);int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<sizeof b&&n==st.st_size;close(fd);if(!ok)return-1;b[n]=0;
 if(sscanf(b,"%ld %llu %d %c",&pid,&tick,&phase,&extra)!=3||pid<=1||pid>INT_MAX||!tick||phase<1||phase>3)return-1;
 if(pid!=s->pid||tick!=s->tick)return-1;s->phase=phase;return 1;
}
static int hot_publish(const struct hot_state*s,int phase){
 char tmp[128],b[128];snprintf(tmp,sizeof tmp,HOT_OWNER ".%ld",(long)getpid());int fd=open(tmp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;
 int n=snprintf(b,sizeof b,"%ld %llu %d\n",(long)s->pid,s->tick,phase);int ok=write(fd,b,(size_t)n)==n&&!fsync(fd);if(close(fd))ok=0;if(ok)ok=!rename(tmp,HOT_OWNER);if(!ok)unlink(tmp);return ok;
}
static int hot_ascii(const char*s,size_t max,int min){size_t n=strnlen(s,max);if(n>=max||n<(size_t)min)return 0;for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]>126)return 0;return 1;}
/* Follow the original tethering_wifi_ap_get_security_type getter (handle+0xfc).
 * Gate this small ordinary-RAM read with the actual loaded OEM getter bytes,
 * the native heap VMA and the enclosing controller/PID double snapshot.
 * A stored passphrase alone is not evidence that security is enabled.
 */
static int hot_security(int fd,pid_t pid,uint32_t handle,int*out){
 if(handle<0x10000u||handle>0xff000000u-0x100u||(handle&3u))return 0;
 char path[80],line[512];snprintf(path,sizeof path,"/proc/%ld/maps",(long)pid);FILE*f=fopen(path,"r");if(!f)return 0;
 unsigned heap=0,library=0;uint32_t base=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,offset;char perms[8],dev[32],file[300];unsigned long inode;
  if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&offset,dev,&inode,file)!=7)continue;
  if(!strcmp(file,"[heap]")&&!strcmp(perms,"rw-p")&&lo<=handle&&(uint64_t)handle+0x100u<=hi)heap++;
  if(!strcmp(file,"/usr/lib/libcapi-network-tethering.so.0.1.0")&&!strcmp(perms,"r-xp")&&!offset&&hi>lo&&(uint64_t)hi-lo>=0xecc4u){library++;base=lo;}
 }
 int ok=!ferror(f)&&heap==1&&library==1;if(fclose(f))ok=0;if(!ok||base>0xff000000u-0xecc4u)return 0;
 unsigned char code[sizeof hot_security_getter];uint32_t a,b;
 if(!hot_read(fd,base+HOT_SECURITY_GETTER,code,sizeof code)||memcmp(code,hot_security_getter,sizeof code)||!hot_read(fd,handle+0xfcu,&a,4)||!hot_read(fd,handle+0xfcu,&b,4)||a!=b||a>1u)return 0;
 *out=(int)a;return 1;
}
static int hot_snapshot(struct hot_state*s,int credentials){
 memset(s,0,sizeof*s);s->security=-1;if(!hot_camera(&s->pid,&s->tick))return 0;char path[96];snprintf(path,sizeof path,"/proc/%ld/mem",(long)s->pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;unsigned char code[40];int ok=1;
 for(unsigned i=0;i<sizeof hot_code_guards/sizeof*hot_code_guards;i++){const struct hot_code_guard*g=hot_code_guards+i;if(!hot_read(fd,g->address,code,g->size)||memcmp(code,g->bytes,g->size)){ok=0;break;}}
 uint32_t a[3],b[3],pref=0,pref_after=0;char ssid[33]={0},password[64]={0};int security=-1,security_after=-1;
 if(ok)ok=hot_read(fd,0x4a8e08,a,4)&&hot_read(fd,0x4a8e38,a+1,4)&&hot_read(fd,0x4a8e84,a+2,4);
 if(ok&&(a[1]==11||a[1]==1||a[1]==2)&&a[2]&&credentials){
  ok=hot_security(fd,s->pid,a[2],&security);
  if(!ok)why="HOTSPOT_SECURITY_UNAVAILABLE";
  if(ok)ok=hot_read(fd,0x4aeab8,ssid,sizeof ssid)&&hot_ascii(ssid,sizeof ssid,1);
  if(ok&&security==1)ok=hot_read(fd,0x4aaab8,&pref,4)&&pref>=0x10000&&pref<=0xff000000-0x10723&&hot_password(s->pid,pref,password)&&hot_read(fd,0x4aaab8,&pref_after,4)&&pref==pref_after&&hot_ascii(password,sizeof password,8);
  if(ok)ok=hot_security(fd,s->pid,a[2],&security_after)&&security==security_after;
 }
 if(ok)ok=hot_read(fd,0x4a8e08,b,4)&&hot_read(fd,0x4a8e38,b+1,4)&&hot_read(fd,0x4a8e84,b+2,4)&&!memcmp(a,b,sizeof a)&&proc_ticks(s->pid)==s->tick&&a[1]<=21;
 close(fd);if(!ok)return 0;s->state=(int)a[1];s->callback=a[0];s->valid=1;int own=hot_owner(s);if(own<0){s->phase=-1;s->owned=0;}else s->owned=own;
 s->ready=(s->state==11||s->state==1||s->state==2)&&a[2]&&interface_ip("uap0",s->ip);
 if(credentials&&s->ready){s->security=security;strcpy(s->ssid,ssid);if(security==1)strcpy(s->password,password);}return 1;
}static int hot_fingerprint(const char*path,size_t size,uint32_t expected){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;struct stat st;unsigned char b[4096];ssize_t n;size_t total=0;uint32_t h=2166136261u;
 int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)size;while(ok&&(n=read(f,b,sizeof b))>0){total+=(size_t)n;for(ssize_t i=0;i<n;i++)h=(h^b[i])*16777619u;}
 if(ok)ok=n==0&&total==size&&h==expected;close(f);return ok;
}
static int hot_original_command(int start,const struct hot_state*expected){
 struct hot_state s;if(!hot_snapshot(&s,0)||s.pid!=expected->pid||s.tick!=expected->tick){why="HOTSPOT_CAMERA_CHANGED";return 0;}
 if(!hot_fingerprint(HOT_APP,HOT_APP_BYTES,HOT_APP_FNV)||!hot_fingerprint("/usr/bin/st",HOT_ST_BYTES,HOT_ST_FNV)){why="HOTSPOT_ORIGINAL_BINARY_MISMATCH";return 0;}
 if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";return 0;}
 pid_t p=fork();if(p<0){why="HOTSPOT_COMMAND_FORK_FAILED";return 0;}if(!p){int null=open("/dev/null",O_RDWR);if(null>=0){dup2(null,0);dup2(null,1);dup2(null,2);if(null>2)close(null);}execl("/usr/bin/st","st","app","softap",start?"start":"stop",(char*)0);_exit(127);}
 unsigned long long until=monotime()+2500;int status=0;for(;;){pid_t r=waitpid(p,&status,WNOHANG);if(r==p){if(WIFEXITED(status)&&!WEXITSTATUS(status))return 1;why="HOTSPOT_COMMAND_FAILED";return 0;}if(r<0){why="HOTSPOT_COMMAND_WAIT_FAILED";return 0;}if(monotime()>=until){kill(p,SIGTERM);poll(NULL,0,100);if(waitpid(p,&status,WNOHANG)==0){kill(p,SIGKILL);waitpid(p,&status,0);}why="HOTSPOT_COMMAND_TIMEOUT";return 0;}poll(NULL,0,25);}
}
static int hot_stop(void){
 struct stat st;if(lstat(HOT_OWNER,&st)){if(errno==ENOENT)return 0;why="HOTSPOT_OWNER_UNREADABLE";return 1;}
 struct hot_state s;if(!hot_snapshot(&s,0)){why="HOTSPOT_STATE_UNAVAILABLE";return 1;}if(!s.owned){char ip[32];if(s.phase==-1&&!s.state&&!interface_ip("uap0",ip)&&!unlink(HOT_OWNER))return 0;why="HOTSPOT_NOT_OWNED";return 1;}
 if(!s.state){if(unlink(HOT_OWNER)){why="HOTSPOT_OWNER_RELEASE_FAILED";return 1;}return 0;}
 if(s.phase!=3||((s.state==1||s.state==2||s.state==10||s.state==11)&&s.callback==0xc9194)){if(s.callback!=0xc9194){why="HOTSPOT_OWNER_CALLBACK_CHANGED";return 1;}if(!hot_publish(&s,3)){why="HOTSPOT_OWNER_PUBLICATION_FAILED";return 1;}if(!hot_original_command(0,&s))return 1;}
 unsigned long long until=monotime()+12000;do{struct hot_state t;if(!hot_snapshot(&t,0)||t.pid!=s.pid||t.tick!=s.tick){why="HOTSPOT_STOP_STATE_UNAVAILABLE";return 1;}if(!t.state){if(unlink(HOT_OWNER)){why="HOTSPOT_OWNER_RELEASE_FAILED";return 1;}return 0;}if(stopping){why="HOTSPOT_INTERRUPTED";return 1;}poll(NULL,0,200);}while(monotime()<until);
 why="HOTSPOT_STOP_PENDING";return 1;
}
static int hot_start(void){
 if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";return 1;}int lock=run_guard();if(lock<0){why="SRW_BUSY";return 1;}
 struct hot_state s;int rc=1;if(!hot_snapshot(&s,0)){why="HOTSPOT_STATE_UNAVAILABLE";goto done;}
 if(s.phase==-1){char ip[32];if(s.state||interface_ip("uap0",ip)||unlink(HOT_OWNER)){why="HOTSPOT_STALE_OWNER_UNRESOLVED";goto done;}}
 if(s.owned){if(s.phase==3){why="HOTSPOT_STOP_PENDING";goto done;}if(s.ready&&s.callback==0xc9194){rc=0;goto done;}if(s.callback&&s.callback!=0xc9194){why="HOTSPOT_OWNER_CALLBACK_CHANGED";goto done;}}
 else{
  char ip[32];if(s.state||interface_ip("uap0",ip)){why="HOTSPOT_ORIGINAL_NETWORK_BUSY";goto done;}
  if(!hot_publish(&s,1)){why="HOTSPOT_OWNER_PUBLICATION_FAILED";goto done;}
  if(!hot_original_command(1,&s))goto failed;
 }
 {unsigned long long until=monotime()+20000;do{struct hot_state t;
   if(stopping){why="HOTSPOT_INTERRUPTED";goto failed;}if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";goto failed;}
   if(!hot_snapshot(&t,1)||t.pid!=s.pid||t.tick!=s.tick){why="HOTSPOT_START_STATE_UNAVAILABLE";goto failed;}
   if(t.ready&&t.callback==0xc9194&&t.ssid[0]&&(t.security==0||(t.security==1&&t.password[0]))){if(!hot_publish(&t,2)){why="HOTSPOT_OWNER_PUBLICATION_FAILED";goto failed;}rc=0;goto done;}
   poll(NULL,0,200);
  }while(monotime()<until);}
 why="HOTSPOT_START_PENDING";
failed:
 /* Preserve ownership across interruption; Wi-Fi stop may resume native cleanup. */
done:close(lock);return rc;
}
static void hot_hex(const char*s,char*out){static const char h[]="0123456789abcdef";while(*s){unsigned char v=(unsigned char)*s++;*out++=h[v>>4];*out++=h[v&15];}*out=0;}
#endif
