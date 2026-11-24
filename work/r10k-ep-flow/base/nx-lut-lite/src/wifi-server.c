/* SPDX-License-Identifier: AGPL-3.0-only
 * NX500 1.12 read-only Metol/nx-remote-controller transport integration.
 * LCD NV12 buffer addresses and the change detector derive from mewlips'
 * liveview.c/nx_model.c (AGPL-3.0). No MMIO writes or capture-setting endpoint; bounded stock RVF session API.
 */
#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <sys/syscall.h>
#include "wifi_media.h"
#define WIFI_VERSION "0.4.0-RC6.14.11"
#include "wifi_channel.h"
#define WIFI_TMP "/tmp/nx-lut-lite"
#define WIFI_PID WIFI_TMP "/wifi-service.pid"
#define WIFI_USB "/sys/class/extcon/USB/state"
#define WIFI_PORT 8181
#define WIFI_CLIENTS 8
#define WIFI_BACKLOG 16
#define WIFI_MAX_TRANSFERS 4
static volatile sig_atomic_t stopping;
static const char*why="STOPPED";static char self_exe[PATH_MAX],bind_ip[32];
static unsigned long long monotime(void){
#if defined(__arm__)
 struct{int32_t sec,nsec;}ts;if(syscall(263,CLOCK_MONOTONIC,&ts))return 0;return(unsigned long long)(uint32_t)ts.sec*1000+(unsigned)ts.nsec/1000000;
#else
 struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return 0;return(unsigned long long)ts.tv_sec*1000+(unsigned)ts.tv_nsec/1000000;
#endif
}
static void stop_signal(int s){(void)s;stopping=1;}
static int physical_usb(void){char b[16];int f=open(WIFI_USB,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(f<0)return-1;ssize_t n=read(f,b,15);int rc=close(f);if(n<=0||n>=15||rc)return-1;b[n]=0;char*p=b;while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;if(*p!='0'&&*p!='1'&&*p!='2')return-1;int v=*p++-'0';while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;return *p?-1:v;}
static int supported(void){char a[32],b[32];FILE*f=fopen("/etc/version.info","r");if(!f)return 0;int ok=fgets(a,32,f)&&fgets(b,32,f);fclose(f);if(!ok)return 0;a[strcspn(a,"\r\n")]=0;b[strcspn(b,"\r\n")]=0;return !strcmp(a,"1.12")&&!strcmp(b,"NX500");}
static int interface_ip(const char*name,char out[32]){
 out[0]=0;int f=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(f<0)return 0;
 struct ifreq r;memset(&r,0,sizeof r);snprintf(r.ifr_name,sizeof r.ifr_name,"%s",name);int ok=!ioctl(f,SIOCGIFFLAGS,&r)&&(r.ifr_flags&IFF_UP);
 if(ok){snprintf(r.ifr_name,sizeof r.ifr_name,"%s",name);r.ifr_addr.sa_family=AF_INET;ok=!ioctl(f,SIOCGIFADDR,&r);}
 if(ok){uint32_t ip=((struct sockaddr_in*)&r.ifr_addr)->sin_addr.s_addr;ok=ip!=0&&ip!=INADDR_NONE&&inet_ntop(AF_INET,&ip,out,32)!=NULL;}
 close(f);return ok;
}
static unsigned long long proc_ticks(pid_t pid){char path[64],b[1024];snprintf(path,sizeof path,"/proc/%ld/stat",(long)pid);int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0)return 0;ssize_t n=read(f,b,sizeof b-1);close(f);if(n<=0)return 0;b[n]=0;char*p=strrchr(b,')');if(!p)return 0;p++;for(unsigned i=3;i<22;i++){while(*p==' ')p++;while(*p&&*p!=' ')p++;}return strtoull(p,NULL,10);}
static pid_t service_pid(void){long p=0;unsigned long long tick=0;char extra;FILE*f=fopen(WIFI_PID,"r");if(!f)return 0;int n=fscanf(f,"%ld %llu %c",&p,&tick,&extra);fclose(f);if(n!=2||p<=1||p>INT_MAX||!tick||proc_ticks((pid_t)p)!=tick)return 0;
 char file[64],exe[PATH_MAX];snprintf(file,sizeof file,"/proc/%ld/exe",p);ssize_t got=readlink(file,exe,sizeof exe-1);if(got<=0)return 0;exe[got]=0;return !strcmp(exe,self_exe)?(pid_t)p:0;}
static int run_guard(void){int f=open(WIFI_TMP "/run.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return-1;if(flock(f,LOCK_EX|LOCK_NB)){close(f);return-1;}return f;}
#include "wifi_aba_wire.h"
#include "wifi_hotspot.h"
#include "wifi_rvf.h"
static int wifi_ip(char out[32]){
 struct hot_state ap;int valid=hot_snapshot(&ap,0);
 if(valid&&ap.ready){strcpy(out,ap.ip);return 1;}
 struct stat st;if(!lstat(HOT_OWNER,&st)){out[0]=0;return 0;}
 return interface_ip("mlan0",out);
}
static void status(void){
 struct hot_state ap;int valid=hot_snapshot(&ap,1);struct stat st;int record=!lstat(HOT_OWNER,&st);
 char ip[32]="",url[80]="",ssid[66]="",password[128]="";const char*mode=(record||(valid&&ap.ready))?"hotspot":"station";
 int connected=0;if(!strcmp(mode,"hotspot")){if(valid&&ap.ready&&ap.ssid[0]&&(ap.security==0||(ap.security==1&&ap.password[0]))){strcpy(ip,ap.ip);connected=1;hot_hex(ap.ssid,ssid);hot_hex(ap.password,password);}}
 else connected=interface_ip("mlan0",ip);
 pid_t pid=service_pid();if(connected)snprintf(url,sizeof url,"http://%s:%d/",ip,WIFI_PORT);
 const char*reason=strcmp(why,"STOPPED")?why:pid&&connected?"RUNNING":connected?"CONNECTED_NOT_STARTED":"NO_WIFI";
 printf("NXL_WIFI3\nrunning=%d\nconnected=%d\nip=%s\nurl=%s\nreason=%s\nmode=%s\nap_valid=%d\nap_state=%d\nowned=%d\nssid_hex=%s\npassword_hex=%s\nsecurity=%s\nend=NXL_WIFI_END3\n",!!pid&&connected,connected,ip,url,reason,mode,valid,valid?ap.state:-1,record,ssid,password,valid&&ap.ready&&ap.security==0?"none":valid&&ap.ready&&ap.security==1?"wpa2":"unknown");
}
static int web_stop(void){
 if(rvf_blocks_hotspot()){why="STOP_ORIGINAL_RVF_FIRST";return 1;}
 pid_t pid=service_pid();if(pid){kill(pid,SIGTERM);unsigned long long until=monotime()+3000;while(monotime()<until&&service_pid()==pid)poll(NULL,0,25);if(service_pid()==pid){why="WEB_STOP_PENDING";return 1;}}return 0;
}
/* The upstream NX500 Wi-Fi selector uses exactly this original VCONF request.
 * It opens the AP-setting UI, not MOBILE_HOME. Preserve its integer tag and
 * reject unexpected layouts instead of creating a fake event file. */
static int connection_settings(void){
 if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";return 1;}int lock=run_guard();if(lock<0){why="SRW_BUSY";return 1;}
 const char*path="/tmp/var/run/memory/ap_setting/request_type";int f=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;uint32_t a[2],b[2];
 int ok=f>=0&&!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size==8&&pread(f,a,8,0)==8&&pread(f,b,8,0)==8&&!memcmp(a,b,8)&&a[0]==0x29&&a[1]<=16;
 if(ok){uint32_t request[2]={0x29,1};ok=pwrite(f,request,8,0)==8;}
 if(f>=0)close(f);close(lock);why=ok?"AP_SETTINGS_REQUESTED":"AP_REQUEST_LAYOUT_UNAVAILABLE";return !ok;
}
#include "wifi_download.h"
struct client{
 int fd,file,bulk,rvf_push;
 char request[8192],header[640];
 size_t used,head_len,head_sent;
 unsigned char*data;
 size_t length,sent;
 off_t remaining;
 unsigned long long accepted,activity;
 unsigned long long push_event,push_id;
};
static struct client clients[WIFI_CLIENTS];
static int media_root=-1,web_root=-1;

static void client_close(struct client *c){
 if(c->fd>=0)close(c->fd);
 if(c->file>=0)close(c->file);
 free(c->data);
 memset(c,0,sizeof *c);
 c->fd=c->file=-1;
}

static void response(struct client*c,int code,const char*type,const void*data,size_t size){
 const char*label=code==200?"OK":code==202?"Accepted":code==204?"No Content":code==400?"Bad Request":code==404?"Not Found":code==405?"Method Not Allowed":code==429?"Too Many Requests":code==431?"Request Header Fields Too Large":"Service Unavailable";
 c->head_len=(size_t)snprintf(c->header,sizeof c->header,"HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nContent-Type: %s\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",code,label,size,type);
 if(size&&data){c->data=malloc(size);if(!c->data){c->head_len=0;return;}memcpy(c->data,data,size);c->length=size;}
}
static void text_response(struct client*c,int code,const char*s){response(c,code,"text/plain; charset=utf-8",s,strlen(s));}
static int query(const char*q,const char*key,char*out,size_t cap){size_t key_n=strlen(key);while(q&&*q){const char*end=strchr(q,'&');size_t n=end?(size_t)(end-q):strlen(q);if(n>key_n&&q[key_n]=='='&&!memcmp(q,key,key_n))return wifi_decode(q+key_n+1,n-key_n-1,out,cap);q=end?end+1:NULL;}return 0;}
static void list_media(struct client*c,const char*q){
 char folder[96]="",cursor[24]="0";if(q&&*q&&!query(q,"dir",folder,sizeof folder)){text_response(c,400,"INVALID_DIRECTORY");return;}
 if(q)query(q,"offset",cursor,sizeof cursor);char*end;unsigned long offset=strtoul(cursor,&end,10);if(*end||offset>100000){text_response(c,400,"INVALID_OFFSET");return;}
 int base=media_root;if(*folder){if(!wifi_component(folder,1)){text_response(c,400,"INVALID_DIRECTORY");return;}base=openat(media_root,folder,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(base<0){text_response(c,404,"DIRECTORY_NOT_FOUND");return;}}
 int copy=openat(base,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(*folder)close(base);if(copy<0){text_response(c,503,"CARD_UNAVAILABLE");return;}DIR*d=fdopendir(copy);if(!d){close(copy);text_response(c,503,"CARD_UNAVAILABLE");return;}
 char output[32768];size_t used=(size_t)snprintf(output,sizeof output,"{\"dir\":\"%s\",\"items\":[",folder);unsigned long seen=0,count=0;int more=0;struct dirent*entry;
 while((entry=readdir(d))){if(!wifi_component(entry->d_name,!*folder))continue;struct stat st;if(fstatat(dirfd(d),entry->d_name,&st,AT_SYMLINK_NOFOLLOW)||(*folder?!S_ISREG(st.st_mode):!S_ISDIR(st.st_mode)))continue;
  if(seen++<offset)continue;if(count>=100){more=1;break;}size_t n=(size_t)snprintf(output+used,sizeof output-used,"%s{\"name\":\"%s\",\"bytes\":%llu}",count?",":"",entry->d_name,(unsigned long long)st.st_size);if(n>=sizeof output-used-100){more=1;break;}used+=n;count++;
 }closedir(d);used+=(size_t)snprintf(output+used,sizeof output-used,"],\"next\":%ld}",more?(long)(offset+count):-1L);response(c,200,"application/json; charset=utf-8",output,used);
}
static void file_response(struct client*c,int f,const char*type){struct stat st;if(f<0||fstat(f,&st)||!S_ISREG(st.st_mode)||st.st_size<0){if(f>=0)close(f);text_response(c,404,"FILE_NOT_FOUND");return;}
 c->file=f;c->remaining=st.st_size;c->head_len=(size_t)snprintf(c->header,sizeof c->header,"HTTP/1.1 200 OK\r\nContent-Length: %llu\r\nContent-Type: %s\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Disposition: inline\r\n\r\n",(unsigned long long)st.st_size,type);
}
static int wifi_frequency_mhz(const char*name){
 struct{char name[16];union{struct{int32_t m;int16_t e;unsigned char i,flags;}f;unsigned char pad[16];}u;}r;memset(&r,0,sizeof r);snprintf(r.name,sizeof r.name,"%s",name);int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return 0;int ok=!ioctl(fd,0x8b05,&r);close(fd);if(!ok||r.u.f.m<=0||r.u.f.e<0||r.u.f.e>9)return 0;int64_t hz=r.u.f.m;for(int i=0;i<r.u.f.e;i++)hz*=10;return hz>=1000000000ll&&hz<=7000000000ll&&hz%1000000==0?(int)(hz/1000000):0;
}
static void route(struct client*c);
static const char *rvf_web_end(unsigned long long now);
#include "wifi_web_extra.h"
#include "wifi_rvf_push.inc"
static int transfer_slot_available(void){
    unsigned count=0;
    for(unsigned i=0;i<WIFI_CLIENTS;i++)
        if(clients[i].fd>=0 && clients[i].bulk) count++;
    return count<WIFI_MAX_TRANSFERS;
}

static void landing_page(struct client *c,int head_only){
    file_response(c,openat(web_root,"index.html",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),"text/html; charset=utf-8");
    if(c->file<0){
        if(head_only){free(c->data);c->data=NULL;c->length=0;}
        return;
    }
    if(head_only){
        close(c->file);c->file=-1;c->remaining=0;
    }
}
static void route(struct client*c){
 int post_allowed=rvf_post_allowed(c);char method[12],uri[1024],protocol[16],extra;char*eol=strstr(c->request,"\r\n");if(!eol||eol-c->request>1100){text_response(c,400,"BAD_REQUEST");return;}char first_line[1104];size_t first_size=(size_t)(eol-c->request);memcpy(first_line,c->request,first_size);first_line[first_size]=0;
 if(sscanf(first_line,"%11s %1023s %15s %c",method,uri,protocol,&extra)!=3||(strcmp(protocol,"HTTP/1.1")&&strcmp(protocol,"HTTP/1.0"))){text_response(c,400,"BAD_REQUEST");return;}
 char*q=strchr(uri,'?');if(q)*q++=0;
 if(!strcmp(method,"POST")&&post_allowed&&native.pid==getpid()){
  if(!strcmp(uri,"/api/v1/rvf/start")){rvf_start_response(c);return;}
  if(!strcmp(uri,"/api/v1/rvf/stop")){rvf_stop_response(c);return;}
 }
 if(!strcmp(method,"HEAD")&&(!strcmp(uri,"/")||!strcmp(uri,"/index.html"))){landing_page(c,1);return;}
 if(strcmp(method,"GET")){text_response(c,405,"READ_ONLY_SERVICE");return;}
 if(!strcmp(uri,"/")||!strcmp(uri,"/index.html")){landing_page(c,0);return;}
 if(!strcmp(uri,"/api/v1/camera/hello")){
     char body[165];
     int n=snprintf(body,sizeof body,"{\"model\":\"NX500\",\"fw_ver\":\"1.12\",\"version\":\"%s\",\"frequency_mhz\":%d,\"service\":\"%s\"}",WIFI_VERSION,wifi_frequency_mhz("uap0"),native.pid==getpid()?"rvf":"media");
     if(n>0&&(size_t)n<sizeof body)response(c,200,"application/json",body,(size_t)n);
     return;
 }
 if(!strcmp(uri,"/file")&&!transfer_slot_available()){
  text_response(c,503,"TRANSFER_BUSY");return;
 }
 if(native.pid==getpid()){
  if(!strcmp(uri,"/api/v1/rvf/stream")){rvf_push_response(c);return;}
  if(!strcmp(uri,"/api/v1/gallery/list")||!strcmp(uri,"/thumb")||!strcmp(uri,"/file")){
   text_response(c,404,"RVF_ONLY");return;
  }
 }
 if(!strcmp(uri,"/api/v1/camera/info")){camera_overview(c);return;}
 if(!strcmp(uri,"/api/v1/gallery/list")){list_media(c,q);return;}
 if(!strcmp(uri,"/file")||!strcmp(uri,"/thumb")){char path[170];if(!query(q,"f",path,sizeof path)){text_response(c,400,"INVALID_PATH");return;}int f=wifi_media_open(media_root,path);if(f<0){text_response(c,404,"FILE_NOT_FOUND");return;}
  if(!strcmp(uri,"/thumb")){unsigned char jpg[65536];size_t n=0;int ok=wifi_thumbnail(f,jpg,sizeof jpg,&n);close(f);if(ok)response(c,200,"image/jpeg",jpg,n);else response(c,204,"image/jpeg",NULL,0);return;}
  const char*ext=strrchr(path,'.');file_response(c,f,ext&&(!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg"))?"image/jpeg":ext&&!strcasecmp(ext,".mp4")?"video/mp4":"application/octet-stream");if(c->file>=0)c->bulk=1;
  return;
 }
 if(!strcmp(uri,"/app.js")){file_response(c,openat(web_root,"app.js",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),"text/javascript; charset=utf-8");return;}
 if(!strcmp(uri,"/extra.js")){file_response(c,openat(web_root,"extra.js",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),"text/javascript; charset=utf-8");return;}
 text_response(c,404,"NOT_FOUND");
}
static void client_io(struct client*c,short events){
 if (events & (POLLERR | POLLHUP | POLLNVAL)) {
  client_close(c);return;
 }
 if(c->rvf_push){
  if(events&POLLIN){
   char input;
   ssize_t n=recv(c->fd,&input,1,0);
   if(n>=0){
    client_close(c);return;
   }
   if(errno!=EAGAIN&&errno!=EINTR){
    client_close(c);return;
   }
  }
  if(events&POLLOUT)rvf_push_send(c);
  return;
 }
 if(!c->head_len){if(!(events&POLLIN))return;ssize_t n=recv(c->fd,c->request+c->used,sizeof c->request-1-c->used,0);if(n<=0){if(n==0||(errno!=EAGAIN&&errno!=EINTR))client_close(c);return;}c->activity=monotime();c->used+=(size_t)n;c->request[c->used]=0;
  if(memchr(c->request,0,c->used)){client_close(c);return;}if(!strstr(c->request,"\r\n\r\n")){if(c->used==sizeof c->request-1){text_response(c,431,"HEADER_TOO_LARGE");}return;}route(c);if(!c->head_len){client_close(c);return;}
 }
 if(!(events&POLLOUT))return;
 const void*p=NULL;size_t n=0;if(c->head_sent<c->head_len){p=c->header+c->head_sent;n=c->head_len-c->head_sent;}
 else {
  if (c->sent == c->length && c->file >= 0 && c->remaining > 0) {
   if (!c->data) {
    c->data = malloc(FILE_CHUNK);
    if (!c->data) {client_close(c);return;}
   }
   size_t need = c->remaining > FILE_CHUNK ? FILE_CHUNK : (size_t)c->remaining;
   ssize_t got = read(c->file, c->data, need);
   int error = got < 0 ? errno : 0;
   if (got <= 0) {
    if (error == EINTR || error == EAGAIN) return;
    client_close(c);
    return;
   }
   c->length = (size_t)got;
   c->sent = 0;
   c->remaining -= got;
  }
  if (c->sent == c->length) {client_close(c);return;}
  p = c->data + c->sent;
  n = c->length - c->sent;
 }
 ssize_t got = send(c->fd, p, n, MSG_NOSIGNAL);
 if (got > 0) {
  c->activity = monotime();
  if (c->head_sent < c->head_len) c->head_sent += (size_t)got;
  else c->sent += (size_t)got;
 } else if (got == 0) client_close(c);
 else if (errno != EAGAIN && errno != EINTR) client_close(c);
}
static unsigned active_clients(void){
    unsigned active=0;
    for(unsigned i=0;i<WIFI_CLIENTS;i++) if(clients[i].fd>=0) active++;
    return active;
}

static int expired_client(const struct client *c,unsigned long long now){
    if(!c->head_len){
        if(now<c->accepted) return 0;
        if(!c->used && now-c->accepted>=2500)return 1;
        if(c->used && now-c->accepted>=8000)return 1;
        return 0;
    }
    return now>=c->activity && now-c->activity>=15000;
}

static void accept_clients(int server){
    for(unsigned i=0;i<WIFI_CLIENTS;i++){
        struct client *c=&clients[i];
        if(c->fd>=0) continue;
        int fd=accept4(server,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(fd<0) break;
        c->fd=fd;c->accepted=c->activity=monotime();
    }
}

static void service_clients(int server){
    struct pollfd p[WIFI_CLIENTS+2];
    p[0]=(struct pollfd){active_clients()<WIFI_CLIENTS?server:-1,POLLIN,0};
    unsigned long long now=monotime();
    for(unsigned i=0;i<WIFI_CLIENTS;i++){
        struct client *c=&clients[i];
        short events=c->head_len?POLLOUT:POLLIN;
        if(c->rvf_push){
            rvf_push_fill(c,now);
            events=POLLIN;
            if(c->head_sent<c->head_len||c->sent<c->length)events|=POLLOUT;
        }
        p[i+1]=(struct pollfd){c->fd,events,0};
    }
    p[WIFI_CLIENTS+1]=(struct pollfd){rvf_update_read,POLLIN,0};
    int result=poll(p,WIFI_CLIENTS+2,100);
    if(result<0){if(errno!=EINTR){why="POLL_FAILED";stopping=1;}return;}
    now=monotime();
    if(p[WIFI_CLIENTS+1].revents)rvf_push_updates(p[WIFI_CLIENTS+1].revents);
    for(unsigned i=0;i<WIFI_CLIENTS;i++){
        struct client *c=&clients[i];
        if(c->fd<0)continue;
        if(expired_client(c,now))client_close(c);
        else if(p[i+1].revents)client_io(c,p[i+1].revents);
    }
    /* Let pending connections stay in the kernel backlog until a slot is free. */
    if(p[0].revents&POLLIN)accept_clients(server);
}

static int serve(const char*root){
 int original=native.pid==getpid();
 if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";return 1;}
 if(!(original?interface_ip("uap0",bind_ip):wifi_ip(bind_ip))){why="NO_WIFI";return 1;}
 int lock=open(WIFI_TMP "/wifi-service.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);why="ALREADY_RUNNING";return service_pid()?0:1;}
 web_root=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(!original)media_root=open("/mnt/mmc/DCIM",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(web_root<0||(!original&&media_root<0)){why="ROOT_UNAVAILABLE";return 1;}
 int server=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(server<0){why="SOCKET_FAILED";return 1;}int reuse=1;if(setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)){close(server);why="SOCKET_FAILED";return 1;}struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(WIFI_PORT)};inet_pton(AF_INET,bind_ip,&address.sin_addr);
 if(bind(server,(struct sockaddr*)&address,sizeof address)||listen(server,WIFI_BACKLOG)){why="PORT_UNAVAILABLE";close(server);return 1;}
 char temporary[96];snprintf(temporary,sizeof temporary,WIFI_TMP "/wifi-pid-%ld",(long)getpid());int pf=open(temporary,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(pf<0){why="PID_PUBLICATION_FAILED";close(server);return 1;}char id[80];int len=snprintf(id,sizeof id,"%ld %llu\n",(long)getpid(),proc_ticks(getpid()));int ok=write(pf,id,(size_t)len)==len&&!fsync(pf);if(close(pf))ok=0;if(ok)ok=!rename(temporary,WIFI_PID);if(!ok){unlink(temporary);close(server);why="PID_PUBLICATION_FAILED";return 1;}
 for(unsigned i=0;i<WIFI_CLIENTS;i++)clients[i].fd=clients[i].file=-1;
 unsigned long long check=0,frequency_check=0;
 unsigned missing_frequency=0;
 why="STOPPED";
 if(original)native_ready_at=monotime();
 while(!stopping){unsigned long long now=monotime();if(now>=check){char ip[32];if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";break;}if(!(original?interface_ip("uap0",ip):wifi_ip(ip))||strcmp(ip,bind_ip)){why="WIFI_LINK_CHANGED";break;}check=now+500;}
  if(original){
   if(now>=frequency_check){
    int mhz=wifi_frequency_mhz("uap0");
    frequency_check=now+1000;
    missing_frequency=mhz?0:missing_frequency+1;
    if((mhz&&mhz!=rvf_verified_frequency)||missing_frequency>=3){
     why="RVF_FREQUENCY_LOST";break;
    }
    rvf_frequency_mhz=mhz;
   }
   const char *end=rvf_web_end(now);
   if(end){why=end;break;}
  }
  service_clients(server);
 }
 for(unsigned i=0;i<WIFI_CLIENTS;i++){
  rvf_push_finish(clients+i);
  client_close(clients+i);
 }
 close(server);
 close(web_root);
 if(media_root>=0)close(media_root);
 if(service_pid()==getpid())unlink(WIFI_PID);
 close(lock);
 return 0;
}
#include "wifi_rvf_entry.inc"
static int start_stock_album(void){
 int rc=hot_start();
 if(rc)return rc;
 unsigned long long deadline=monotime()+1800;
 do{
  if(stopping||physical_usb()!=0){why="HOTSPOT_INTERRUPTED";return 1;}
  if(wifi_frequency_is_24ghz(wifi_frequency_mhz("uap0")))return 0;
  poll(NULL,0,120);
 }while(monotime()<deadline);
 why="HOTSPOT_STOCK_24_UNCONFIRMED";
 return 1;
}

int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_WIFI_VERSION=" WIFI_VERSION "; STOCK_RVF_SAME_DEVICE; READ_ONLY_GALLERY; NO_ISP_WRITES");return 0;}
 if(!realpath(argv[0],self_exe)||!supported()){fputs("UNSUPPORTED_CAMERA\n",stderr);return 1;}mkdir(WIFI_TMP,0700);signal(SIGPIPE,SIG_IGN);signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);sigset_t term;sigemptyset(&term);sigaddset(&term,SIGTERM);if(sigprocmask(SIG_UNBLOCK,&term,NULL))return 1;
 if(argc==4&&!strcmp(argv[1],"--rvf-serve"))return rvf_serve(argv[2],argv[3]);
 if(argc==2&&!strcmp(argv[1],"--rvf-status")){rvf_menu_status(0);return 0;}
 if(argc==3&&!strcmp(argv[1],"--rvf-status")){return rvf_menu_status(atoi(argv[2]));}
 if(argc==2&&!strcmp(argv[1],"--rvf-stop")){int rc=rvf_menu_stop();rvf_menu_status(0);return rc;}
 if(argc==2&&!strcmp(argv[1],"--status")){status();return 0;}
 if(argc==2&&!strcmp(argv[1],"--stop")){int rc=web_stop();if(!rc)rc=hot_stop();status();return rc;}
 if(argc==2&&!strcmp(argv[1],"--web-stop")){int rc=web_stop();status();return rc;}
 if(argc==2&&!strcmp(argv[1],"--hotspot-start")){int rc=hot_start();status();return rc;}
 if(argc==2&&!strcmp(argv[1],"--hotspot-start-24")){int rc=start_stock_album();status();return rc;}
 int rc=1;if(argc==2&&!strcmp(argv[1],"--connect"))rc=connection_settings();else if(argc==3&&!strcmp(argv[1],"--serve"))rc=serve(argv[2]);else why="INVALID_ARGUMENT";
 fprintf(stderr,"WIFI_RESULT reason=%s exit=%d\n",why,rc);return rc;
}
