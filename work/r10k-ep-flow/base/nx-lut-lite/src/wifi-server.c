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
#define WIFI_VERSION "0.4.0-RC6.14.10"
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
static int album_hotspot;
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
static int wifi_ip(char out[32],int *hotspot){
 struct hot_state ap;int valid=hot_snapshot(&ap,0);
 if(valid&&ap.ready){strcpy(out,ap.ip);if(hotspot)*hotspot=1;return 1;}
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
 struct wifi_download download;
 int fd,file,is_frame,bulk,page,rvf_push;
 char request[8192],header[640];
 size_t used,head_len,head_sent;
 unsigned char*data;
 size_t length,sent;
 off_t remaining;
 unsigned long long accepted,activity,frame_ready,frame_done;
 unsigned long long push_event,push_id;
 unsigned push_payload;
};
static struct {unsigned long long requested,served,send_complete,aborted,limited,busy,bytes,first_ms,last_ms,copy_ms,max_copy_ms,capture_ms,max_capture_ms,select_ms,max_select_ms,send_ms,max_send_ms;} frame_stats;
static struct client clients[WIFI_CLIENTS];static int media_root=-1,web_root=-1;
static struct {
    unsigned long long ready, first_page, accepted, pages, queued, heads;
    unsigned long long idle, incomplete, large_headers;
    unsigned peak;
} web_stats;

static void record_page_end(const struct client *c){
    if(!c->page || !c->head_len || c->head_sent!=c->head_len ||
       c->sent!=c->length || c->remaining) return;
    if(++web_stats.queued==1)
        fprintf(stderr,"WIFI_HTTP_FIRST_PAGE_QUEUED since_listen_ms=%llu\n",monotime()-web_stats.ready);
}
static int wifi_tcp_info(int fd, struct tcp_info *info)
{
    *info = (struct tcp_info){0};
    socklen_t length = sizeof *info;
    return !getsockopt(fd, IPPROTO_TCP, TCP_INFO, info, &length) &&
        length >= offsetof(struct tcp_info, tcpi_total_retrans) + sizeof info->tcpi_total_retrans;
}
#include "wifi_rvf_tcp.inc"
static void record_file_end(const struct client *c, const char *reason, int error)
{
    const struct wifi_download *file = &c->download;
    if (!file->name[0])
        return;

    const unsigned long long now = monotime();
    struct tcp_info tcp;
    int have_tcp = wifi_tcp_info(c->fd, &tcp);
    int out_queue = -1;
    if (ioctl(c->fd, TIOCOUTQ, &out_queue))
        out_queue = -1;

    fprintf(stderr, "WIFI_FILE_END name=%s reason=%s errno=%d expected=%llu queued=%llu "
            "first_body_ms=%lld elapsed_ms=%llu idle_ms=%llu max_sd_read_ms=%llu sd_read_retries=%u "
            "tcp_info=%d tcp_unacked=%u tcp_retrans=%u tcp_probes=%u tcp_rtt_us=%u tcp_outq_bytes=%d\n",
            file->name, reason, error, file->expected, file->queued,
            file->queued ? (long long)(file->first_body - file->started) : -1LL,
            now - file->started, now - c->activity, file->max_read_ms, file->read_retries,
            have_tcp, have_tcp ? tcp.tcpi_unacked : 0, have_tcp ? tcp.tcpi_total_retrans : 0,
            have_tcp ? (unsigned)tcp.tcpi_probes : 0, have_tcp ? tcp.tcpi_rtt : 0, out_queue);
}
static void client_close_with_status(struct client*c,const char *reason,int error){
 if(c->rvf_push&&c->fd>=0)rvf_tcp_capture(c->fd,monotime(),reason,error);
 record_file_end(c,reason,error);if(c->bulk==1&&c->head_sent==c->head_len&&c->sent==c->length&&c->remaining==0){if(c->is_frame)transfer.frames++;else transfer.files++;}if(c->is_frame){if(c->head_sent==c->head_len&&c->sent==c->length){unsigned long long elapsed=(c->frame_done?c->frame_done:monotime())-c->frame_ready;frame_stats.send_complete++;frame_stats.send_ms+=elapsed;if(elapsed>frame_stats.max_send_ms)frame_stats.max_send_ms=elapsed;}else frame_stats.aborted++;}if(c->fd>=0)close(c->fd);if(c->file>=0)close(c->file);free(c->data);memset(c,0,sizeof*c);c->fd=c->file=-1;}
static void client_close(struct client *c){client_close_with_status(c,"SERVICE_STOP",0);}
static void response(struct client*c,int code,const char*type,const void*data,size_t size){
 const char*label=code==200?"OK":code==202?"Accepted":code==204?"No Content":code==400?"Bad Request":code==404?"Not Found":code==405?"Method Not Allowed":code==429?"Too Many Requests":code==431?"Request Header Fields Too Large":"Service Unavailable";
 c->head_len=(size_t)snprintf(c->header,sizeof c->header,"HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nContent-Type: %s\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",code,label,size,type);
 if(size&&data){c->data=malloc(size);if(!c->data){c->head_len=0;return;}memcpy(c->data,data,size);c->length=size;}
}
static void text_response(struct client*c,int code,const char*s){response(c,code,"text/plain; charset=utf-8",s,strlen(s));}
static void aba_web_status(struct client*c){
 struct aba_public p;unsigned long long now=monotime();
 if(!wire_read(ABA_PUBLIC,&p,sizeof p,0x41423611u)||p.state<0||p.state>ABA_SKIP||p.updated>now||now-p.updated>(p.state>=ABA_DONE?300000u:5000u)||p.restored>1||p.changed>1){text_response(c,200,"{\"kind\":\"NXABA1\",\"valid\":false}");return;}
 if(p.state<ABA_DONE&&!wire_process(p.pid,p.tick,"/opt/usr/nx-lut-lite/bin/nxl-wifi-radio")){text_response(c,200,"{\"kind\":\"NXABA1\",\"valid\":false}");return;}
 char b[800];int n=snprintf(b,sizeof b,"{\"kind\":\"NXABA1\",\"valid\":true,\"state\":%d,\"updated_ms\":%llu,\"elapsed_ms\":%llu,\"restored\":%u,\"changed\":%u,\"reason\":%u,\"body_bytes\":[%llu,%llu,%llu],\"net_bytes\":[%llu,%llu,%llu],\"stage_valid\":[%u,%u,%u]}",p.state,p.updated,p.elapsed,p.restored,p.changed,p.reason,p.body[0],p.body[1],p.body[2],p.net[0],p.net[1],p.net[2],p.stage_valid[0],p.stage_valid[1],p.stage_valid[2]);
 if(n<0||(size_t)n>=sizeof b){text_response(c,503,"STATUS_UNAVAILABLE");return;}response(c,200,"application/json",b,(size_t)n);
}

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
/* Read-only LCD DRAM maps from NX remote controller. Nothing maps ISP MMIO. */
static const uint32_t lcd_addresses[4]={0xbbaea500,0xbbb68e00,0xbbbe7700,0xbba6bc00};
static unsigned char*lcd[4];static void*mapped[4];static size_t mapped_size[4];static uint32_t hashes[4];static int last_lcd=-1,frame_height=480,frame_width=720,layout_state=-2;
static int map_lcd(void){if(lcd[0])return 1;int f=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(f<0)return 0;
 for(unsigned i=0;i<4;i++){uint32_t page=lcd_addresses[i]&~4095u;size_t delta=lcd_addresses[i]-page;size_t n=(delta+720*480*3/2+4095)&~4095u;void*p=mmap(NULL,n,PROT_READ,MAP_SHARED,f,(off_t)page);if(p==MAP_FAILED){for(unsigned j=0;j<i;j++){munmap(mapped[j],mapped_size[j]);mapped[j]=NULL;lcd[j]=NULL;}close(f);return 0;}mapped[i]=p;mapped_size[i]=n;lcd[i]=(unsigned char*)p+delta;}close(f);return 1;
}
static int movie_layout(void){char b[16];int f=open("/sys/kernel/debug/pmu/hevc/state",O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(f<0)return 0;ssize_t n=read(f,b,15);close(f);if(n<=0)return 0;b[n]=0;int state=!strncmp(b,"off",3)?0:!strncmp(b,"on",2)?1:-1;if(state<0)return 0;if(state==layout_state)return 1;
 frame_width=720;frame_height=480;last_lcd=-1;memset(hashes,0,sizeof hashes);
 if(state){int fd[2];if(pipe2(fd,O_CLOEXEC|O_NONBLOCK))return 0;pid_t p=fork();if(!p){close(fd[0]);dup2(fd[1],1);close(fd[1]);execl("/usr/bin/prefman","prefman","get","0","0x0000a360","l",(char*)0);_exit(127);}close(fd[1]);if(p<0){close(fd[0]);return 0;}
  char out[512];size_t used=0;int complete=0,ws=0;unsigned long long until=monotime()+250;
  while(monotime()<until){ssize_t k=read(fd[0],out+used,sizeof out-1-used);if(k>0)used+=(size_t)k;pid_t done=waitpid(p,&ws,WNOHANG);if(done==p){complete=1;while(used<sizeof out-1&&(k=read(fd[0],out+used,sizeof out-1-used))>0)used+=(size_t)k;break;}if(used>=sizeof out-1)break;poll(NULL,0,5);}
  close(fd[0]);if(!complete){kill(p,SIGTERM);waitpid(p,&ws,WNOHANG);return 0;}if(!WIFEXITED(ws)||WEXITSTATUS(ws))return 0;out[used]=0;char*v=strstr(out,"value = ");if(!v)return 0;char*end;long value=strtol(v+8,&end,10);if(end==v+8||value<0||value>30)return 0;
  if(value==0)frame_height=380;else if(value>=9&&value<=11)frame_width=640;else frame_height=404;
 }layout_state=state;return 1;
}
/* Shared 10fps capture budget. It counts copied frames, not sensor frame IDs. */
static uint32_t lcd_sample_hash(const unsigned char*p,size_t bytes){
 uint32_t h=2166136261u;for(unsigned k=0;k<64;k++){size_t at=(size_t)k*(bytes-4u)/63u;for(unsigned j=0;j<4;j++)h=(h^p[at+j])*16777619u;}return h;
}
static unsigned long long last_frame;
static void frame_response(struct client*c){
 unsigned long long now=monotime();frame_stats.requested++;
 if(last_frame&&now-last_frame<100){unsigned wait=(unsigned)(100-(now-last_frame));frame_stats.limited++;text_response(c,429,"FRAME_RATE_LIMIT");
  size_t at=c->head_len-2;int n=snprintf(c->header+at,sizeof c->header-at,"X-NX-Retry-Ms: %u\r\n\r\n",wait);if(n<0||(size_t)n>=sizeof c->header-at)c->head_len=0;else c->head_len=at+(size_t)n;return;
 }
 int lock=run_guard();if(lock<0){frame_stats.busy++;text_response(c,503,"SRW_BUSY");return;}
 if(!movie_layout()||!map_lcd()){close(lock);text_response(c,503,"LIVEVIEW_LAYOUT_UNAVAILABLE");return;}
 size_t bytes=(size_t)frame_width*(size_t)frame_height*3/2;
 unsigned long long selecting=monotime();
 for(unsigned i=0;i<4;i++){uint32_t h=lcd_sample_hash(lcd[i],bytes);if(hashes[i]&&h!=hashes[i])last_lcd=(int)i;hashes[i]=h;}
 unsigned long long selected=monotime()-selecting;
 if(last_lcd<0)last_lcd=0; /* Static scenes must still have a first displayed image. */
 c->data=malloc(bytes);if(!c->data){close(lock);text_response(c,503,"MEMORY_UNAVAILABLE");return;}
 unsigned long long copy_start=monotime();memcpy(c->data,lcd[last_lcd],bytes);unsigned long long copied=monotime()-copy_start;close(lock);
 c->frame_ready=monotime();unsigned long long prepared=c->frame_ready-now;frame_stats.capture_ms+=prepared;if(prepared>frame_stats.max_capture_ms)frame_stats.max_capture_ms=prepared;frame_stats.select_ms+=selected;if(selected>frame_stats.max_select_ms)frame_stats.max_select_ms=selected;
 c->length=bytes;c->is_frame=1;c->bulk=1;last_frame=now;if(!frame_stats.served)frame_stats.first_ms=now;frame_stats.last_ms=now;frame_stats.served++;frame_stats.bytes+=bytes;frame_stats.copy_ms+=copied;if(copied>frame_stats.max_copy_ms)frame_stats.max_copy_ms=copied;
 c->head_len=(size_t)snprintf(c->header,sizeof c->header,"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nContent-Type: application/octet-stream\r\nConnection: close\r\nCache-Control: no-store\r\nX-NX-Width: %d\r\nX-NX-Height: %d\r\nX-NX-Frame-Id: %llu\r\nX-NX-Copy-Ms: %llu\r\nX-NX-Buffer: %d\r\nX-NX-Capture-Ms: %llu\r\nX-NX-Select-Ms: %llu\r\n\r\n",bytes,frame_width,frame_height,frame_stats.served,copied,last_lcd,prepared,selected);
 if(c->head_len>=sizeof c->header)c->head_len=0;
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
        web_stats.heads++;
        close(c->file);c->file=-1;c->remaining=0;
        return;
    }
    c->page=1;
    if(++web_stats.pages==1){
        web_stats.first_page=monotime();
        fprintf(stderr,"WIFI_HTTP_FIRST_GET since_listen_ms=%llu since_accept_ms=%llu\n",
                web_stats.first_page-web_stats.ready,web_stats.first_page-c->accepted);
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
 if((!strcmp(uri,"/file")||!strcmp(uri,"/api/v1/liveview/get")||
     !strcmp(uri,"/api/v1/rvf/frame")||!strcmp(uri,"/api/v1/network/bulk"))&&!transfer_slot_available()){
     text_response(c,503,"TRANSFER_BUSY");return;
 }
 if(native.pid==getpid()){
  if(!strcmp(uri,"/api/v1/rvf/status")){rvf_status_response(c);return;}
  if(!strcmp(uri,"/api/v1/rvf/stream")){rvf_push_response(c);return;}
  if(!strcmp(uri,"/api/v1/rvf/frame")){rvf_frame_response(c,q);return;}
  if(!strcmp(uri,"/api/v1/gallery/list")||!strcmp(uri,"/thumb")||!strcmp(uri,"/file")||
     !strcmp(uri,"/api/v1/liveview/get")||!strcmp(uri,"/api/v1/network/bulk")){text_response(c,404,"RVF_ONLY");return;}
 }
 if(!strcmp(uri,"/api/v1/linktest/status")){aba_web_status(c);return;}
 if(!strcmp(uri,"/api/v1/liveview/get")){
  if(album_hotspot){text_response(c,404,"ALBUM_ONLY");return;}
  frame_response(c);return;
 }
 if(!strcmp(uri,"/api/v1/camera/info")){camera_overview(c);return;}
 if(!strcmp(uri,"/api/v1/network/bulk")){bulk_response(c);return;}
 if(!strcmp(uri,"/api/v1/gallery/list")){list_media(c,q);return;}
 if(!strcmp(uri,"/file")||!strcmp(uri,"/thumb")){char path[170];if(!query(q,"f",path,sizeof path)){text_response(c,400,"INVALID_PATH");return;}unsigned long long started=monotime();int f=wifi_media_open(media_root,path);if(f<0){text_response(c,404,"FILE_NOT_FOUND");return;}
  if(!strcmp(uri,"/thumb")){unsigned char jpg[65536];size_t n=0;int ok=wifi_thumbnail(f,jpg,sizeof jpg,&n);close(f);if(ok)response(c,200,"image/jpeg",jpg,n);else response(c,204,"image/jpeg",NULL,0);return;}
  const char*ext=strrchr(path,'.');file_response(c,f,ext&&(!strcasecmp(ext,".jpg")||!strcasecmp(ext,".jpeg"))?"image/jpeg":ext&&!strcasecmp(ext,".mp4")?"video/mp4":"application/octet-stream");if(c->file>=0){
   c->bulk=1;
   strcpy(c->download.name,strrchr(path,'/')+1);
   c->download.started=started;
   c->download.expected=(unsigned long long)c->remaining;
  }
  return;
 }
 if(!strcmp(uri,"/aba.js")){file_response(c,openat(web_root,"aba.js",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),"text/javascript; charset=utf-8");return;}
 if(!strcmp(uri,"/app.js")){file_response(c,openat(web_root,"app.js",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),"text/javascript; charset=utf-8");return;}
 if(!strcmp(uri,"/extra.js")){file_response(c,openat(web_root,"extra.js",O_RDONLY|O_NOFOLLOW|O_CLOEXEC),"text/javascript; charset=utf-8");return;}
 text_response(c,404,"NOT_FOUND");
}
static void client_io(struct client*c,short events){
 if (events & (POLLERR | POLLHUP | POLLNVAL)) {
  int error = 0;
  if (c->download.name[0]) {
   socklen_t length = sizeof error;
   if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &length)) error = errno;
  }
  client_close_with_status(c, error || (events & (POLLERR | POLLNVAL)) ? "SOCKET_POLL_ERROR" : "PEER_HANGUP", error);
  return;
 }
 if(c->rvf_push){
  if(events&POLLIN){
   char input;
   ssize_t n=recv(c->fd,&input,1,0);
   if(n>=0){
    client_close_with_status(c,n==0?"PEER_EOF":"UNEXPECTED_STREAM_INPUT",0);return;
   }
   if(errno!=EAGAIN&&errno!=EINTR){
    client_close_with_status(c,"STREAM_RECV_ERROR",errno);return;
   }
  }
  if(events&POLLOUT)rvf_push_send(c);
  return;
 }
 if(!c->head_len){if(!(events&POLLIN))return;ssize_t n=recv(c->fd,c->request+c->used,sizeof c->request-1-c->used,0);if(n<=0){if(n==0||(errno!=EAGAIN&&errno!=EINTR))client_close(c);return;}c->activity=monotime();c->used+=(size_t)n;c->request[c->used]=0;
  if(memchr(c->request,0,c->used)){client_close(c);return;}if(!strstr(c->request,"\r\n\r\n")){if(c->used==sizeof c->request-1){web_stats.large_headers++;text_response(c,431,"HEADER_TOO_LARGE");}return;}route(c);if(!c->head_len){client_close(c);return;}
 }
 if(!(events&POLLOUT))return;
 const void*p=NULL;size_t n=0;if(c->head_sent<c->head_len){p=c->header+c->head_sent;n=c->head_len-c->head_sent;}
 else {if(c->sent==c->length&&c->bulk==2&&c->remaining>0){c->length=c->remaining>32768?32768:(size_t)c->remaining;c->remaining-=(off_t)c->length;c->sent=0;}
  if (c->sent == c->length && c->file >= 0 && c->remaining > 0) {
   if (!c->data) {
    c->data = malloc(FILE_CHUNK);
    if (!c->data) {client_close_with_status(c, "OUT_OF_MEMORY", ENOMEM);return;}
   }
   size_t need = c->remaining > FILE_CHUNK ? FILE_CHUNK : (size_t)c->remaining;
   const int measured = c->download.name[0] != 0;
   unsigned long long before = measured ? monotime() : 0;
   ssize_t got = read(c->file, c->data, need);
   int error = got < 0 ? errno : 0;
   if (measured) {
    unsigned long long elapsed = monotime() - before;
    if (elapsed > c->download.max_read_ms) c->download.max_read_ms = elapsed;
   }
   if (got <= 0) {
    if (error == EINTR || error == EAGAIN) {
     if (measured) c->download.read_retries++;
     return;
    }
    client_close_with_status(c, got == 0 ? "SD_EARLY_EOF" : "SD_READ_ERROR", error);
    return;
   }
   c->length = (size_t)got;
   c->sent = 0;
   c->remaining -= got;
  }
  if (c->sent == c->length) {client_close_with_status(c, "BODY_QUEUED", 0);return;}
  p = c->data + c->sent;
  n = c->length - c->sent;
 }
 ssize_t got = send(c->fd, p, n, MSG_NOSIGNAL);
 if (got > 0) {
  c->activity = monotime();
  if (c->head_sent < c->head_len) c->head_sent += (size_t)got;
  else {
   if (c->bulk) transfer.body += (unsigned long long)got;
   c->sent += (size_t)got;
   if (c->download.name[0]) {
    if (!c->download.queued) c->download.first_body = c->activity;
    c->download.queued += (unsigned long long)got;
   }
   if (c->is_frame && c->sent == c->length) c->frame_done = c->activity;
  }
  record_page_end(c);
 } else if (got == 0) client_close_with_status(c, "SOCKET_CLOSED", 0);
 else if (errno != EAGAIN && errno != EINTR) client_close_with_status(c, "SOCKET_SEND_ERROR", errno);
}
static unsigned active_clients(void){
    unsigned active=0;
    for(unsigned i=0;i<WIFI_CLIENTS;i++) if(clients[i].fd>=0) active++;
    return active;
}

static int expired_client(const struct client *c,unsigned long long now){
    if(!c->head_len){
        if(now<c->accepted) return 0;
        if(!c->used && now-c->accepted>=2500){web_stats.idle++;return 1;}
        if(c->used && now-c->accepted>=8000){web_stats.incomplete++;return 1;}
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
        if(++web_stats.accepted==1)
            fprintf(stderr,"WIFI_HTTP_FIRST_ACCEPT since_listen_ms=%llu\n",c->accepted-web_stats.ready);
    }
    unsigned active=active_clients();
    if(active>web_stats.peak)web_stats.peak=active;
}

static void service_clients(int server){
    struct pollfd p[WIFI_CLIENTS+2];
    p[0]=(struct pollfd){active_clients()<WIFI_CLIENTS?server:-1,POLLIN,0};
    unsigned long long now=monotime();
    for(unsigned i=0;i<WIFI_CLIENTS;i++){
        struct client *c=&clients[i];
        short events=c->head_len?POLLOUT:POLLIN;
        if(c->rvf_push){
            rvf_tcp_tick(c->fd,now);
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
        if(c->rvf_push && c->sent<c->length &&
           !(p[i+1].revents&(POLLOUT|POLLERR|POLLHUP|POLLNVAL)))
            rvf_tcp.window.no_pollout++;
        if(expired_client(c,now))client_close_with_status(c,"NO_PROGRESS_TIMEOUT",0);
        else if(p[i+1].revents)client_io(c,p[i+1].revents);
    }
    /* Let pending connections stay in the kernel backlog until a slot is free. */
    if(p[0].revents&POLLIN)accept_clients(server);
}

static int serve(const char*root){
 int original=native.pid==getpid();
 if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";return 1;}
 if(!(original?interface_ip("uap0",bind_ip):wifi_ip(bind_ip,&album_hotspot))){why="NO_WIFI";return 1;}
 int lock=open(WIFI_TMP "/wifi-service.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB)){if(lock>=0)close(lock);why="ALREADY_RUNNING";return service_pid()?0:1;}
 web_root=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(!original)media_root=open("/mnt/mmc/DCIM",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(web_root<0||(!original&&media_root<0)){why="ROOT_UNAVAILABLE";return 1;}
 int server=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(server<0){why="SOCKET_FAILED";return 1;}int reuse=1;if(setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse)){close(server);why="SOCKET_FAILED";return 1;}struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(WIFI_PORT)};inet_pton(AF_INET,bind_ip,&address.sin_addr);
 if(bind(server,(struct sockaddr*)&address,sizeof address)||listen(server,WIFI_BACKLOG)){why="PORT_UNAVAILABLE";close(server);return 1;}
 char temporary[96];snprintf(temporary,sizeof temporary,WIFI_TMP "/wifi-pid-%ld",(long)getpid());int pf=open(temporary,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(pf<0){why="PID_PUBLICATION_FAILED";close(server);return 1;}char id[80];int len=snprintf(id,sizeof id,"%ld %llu\n",(long)getpid(),proc_ticks(getpid()));int ok=write(pf,id,(size_t)len)==len&&!fsync(pf);if(close(pf))ok=0;if(ok)ok=!rename(temporary,WIFI_PID);if(!ok){unlink(temporary);close(server);why="PID_PUBLICATION_FAILED";return 1;}
 memset(&frame_stats,0,sizeof frame_stats);memset(&transfer,0,sizeof transfer);transfer_next=0;transfer_publish(1);last_frame=0;for(unsigned i=0;i<WIFI_CLIENTS;i++)clients[i].fd=clients[i].file=-1;
 memset(&web_stats,0,sizeof web_stats);web_stats.ready=monotime();
 unsigned long long check=0,frequency_check=0;unsigned missing_frequency=0;
 why="STOPPED";fprintf(stderr,"WIFI_READY version=%s address=%s port=%d mode=%s frequency_mhz=%d\n",
  WIFI_VERSION,bind_ip,WIFI_PORT,original?"stock_rvf":"read_only_gallery",
  wifi_frequency_mhz(original||album_hotspot?"uap0":"mlan0"));fflush(stderr);
 if(original)native_ready_at=monotime();
 while(!stopping){unsigned long long now=monotime();if(now>=check){char ip[32];if(physical_usb()!=0){why="USB_CONNECTED_OR_UNKNOWN";break;}if(!(original?interface_ip("uap0",ip):wifi_ip(ip,NULL))||strcmp(ip,bind_ip)){why="WIFI_LINK_CHANGED";break;}check=now+500;}
  if(original){
   if(now>=frequency_check){
    int mhz=wifi_frequency_mhz("uap0");
    frequency_check=now+1000;
    missing_frequency=mhz?0:missing_frequency+1;
    if((mhz&&mhz!=rvf_verified_frequency)||missing_frequency>=3){
     rvf_stage("FREQUENCY_LOST",mhz,rvf_verified_frequency,0);
     why="RVF_FREQUENCY_LOST";break;
    }
    rvf_frequency_mhz=mhz;
   }
   const char *end=rvf_web_end(now);
   if(end){why=end;break;}
  }
  service_clients(server);
  if(!original){transfer_publish(0);while(waitpid(-1,NULL,WNOHANG)>0){}}
 }
 fprintf(stderr,"WIFI_HTTP_TOTAL accepted=%llu root_get=%llu root_queued=%llu root_head=%llu idle_expired=%llu partial_expired=%llu header_too_large=%llu peak=%u\n",
         web_stats.accepted,web_stats.pages,web_stats.queued,web_stats.heads,web_stats.idle,web_stats.incomplete,web_stats.large_headers,web_stats.peak);
 for(unsigned i=0;i<WIFI_CLIENTS;i++){
  rvf_push_finish(clients+i);
  client_close(clients+i);
 }
 transfer_publish(1);fprintf(stderr,"WIFI_STATS requested=%llu captured=%llu send_complete=%llu aborted=%llu rate_limited=%llu srw_busy=%llu payload_bytes=%llu first_capture_ms=%llu last_capture_ms=%llu sum_copy_ms=%llu max_copy_ms=%llu sum_capture_ms=%llu max_capture_ms=%llu sum_select_ms=%llu max_select_ms=%llu sum_send_ms=%llu max_send_ms=%llu\n",frame_stats.requested,frame_stats.served,frame_stats.send_complete,frame_stats.aborted,frame_stats.limited,frame_stats.busy,frame_stats.bytes,frame_stats.first_ms,frame_stats.last_ms,frame_stats.copy_ms,frame_stats.max_copy_ms,frame_stats.capture_ms,frame_stats.max_capture_ms,frame_stats.select_ms,frame_stats.max_select_ms,frame_stats.send_ms,frame_stats.max_send_ms);fflush(stderr);for(unsigned i=0;i<4;i++)if(mapped[i])munmap(mapped[i],mapped_size[i]);close(server);close(web_root);if(media_root>=0)close(media_root);if(service_pid()==getpid())unlink(WIFI_PID);close(lock);return 0;
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
