/* SPDX-License-Identifier: AGPL-3.0-only */
#include <sys/statvfs.h>
static int web_small_read(const char*path,char*out,size_t cap){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);if(f<0)return 0;ssize_t n=read(f,out,cap-1);int ok=n>0&&(size_t)n<cap-1;if(close(f))ok=0;if(!ok)return 0;out[n]=0;out[strcspn(out,"\r\n")]=0;return 1;
}
static void camera_overview(struct client*c){
 char mac[64]="",uptime[80]="",battery[32]="",ip[32]="",b[1800];double up=-1;int percent=-1;
 if(web_small_read("/sys/class/net/uap0/address",mac,sizeof mac)){if(strlen(mac)!=17||strspn(mac,"0123456789abcdefABCDEF:")!=17)mac[0]=0;}
 if(web_small_read("/proc/uptime",uptime,sizeof uptime)){char*end;up=strtod(uptime,&end);if(end==uptime||!(up>=0&&up<=315360000))up=-1;}
 if(web_small_read("/sys/class/power_supply/battery/capacity",battery,sizeof battery)){char*end;long v=strtol(battery,&end,10);if(!*end&&end!=battery&&v>=0&&v<=100)percent=(int)v;}
 struct statvfs sd;unsigned long long total=0,available=0;int sd_ok=!statvfs("/mnt/mmc",&sd)&&sd.f_frsize&&sd.f_blocks<=ULLONG_MAX/sd.f_frsize&&sd.f_bavail<=sd.f_blocks;
 if(sd_ok){total=(unsigned long long)sd.f_blocks*sd.f_frsize;available=(unsigned long long)sd.f_bavail*sd.f_frsize;}
 (void)interface_ip("uap0",ip);struct rvf_record r;int have=rvf_record_read(&r);int freq=wifi_frequency_mhz("uap0");
 int n=snprintf(b,sizeof b,"{\"model\":\"NX500\",\"fw_ver\":\"1.12\",\"version\":\"" WIFI_VERSION "\",\"transport\":\"Original RVF / read-only gallery\",\"max_fps\":10,\"frequency_mhz\":%d,\"ip\":\"%s\",\"mac\":\"%s\",\"battery_percent\":%d,\"battery_source\":\"sysfs capacity or unknown\",\"uptime_seconds\":%.2f,\"camera_epoch_seconds\":%lld,\"sd_valid\":%s,\"sd_total_bytes\":%llu,\"sd_available_bytes\":%llu,\"rvf_state\":%d,\"rvf_dirty\":%d,\"mode\":null,\"drive\":null,\"notes\":\"Read only. No clock sync, telnet, MCB or camera setting queries. RAM uptime is not camera capture time.\"}",freq,ip,mac,percent,up,(long long)time(NULL),sd_ok?"true":"false",total,available,have?r.state:0,have?r.dirty:0);
 if(n<=0||(size_t)n>=sizeof b){text_response(c,503,"OVERVIEW_UNAVAILABLE");return;}response(c,200,"application/json; charset=utf-8",b,(size_t)n);
}

static int rvf_token_valid(struct client *c){
 char token[40];return native_token[0]&&
     rvf_header(c->request,"X-NX-Session",token,sizeof token)&&!strcmp(native_token,token);
}
static int rvf_viewer(struct client *c){
 if(native_close_at||!rvf_token_valid(c)){text_response(c,403,"RVF_SESSION_REQUIRED");return 0;}
 native_viewer_seen=monotime();return 1;
}
static int rvf_current(struct rvf_record *r){
 return rvf_record_read(r)&&r->server==native.pid&&r->server_tick==native.tick&&r->pid==receiver;
}
static void rvf_status_response(struct client*c){
 if(!rvf_viewer(c))return;
 struct rvf_record r={0};int valid=rvf_current(&r);
 unsigned long long now=monotime();
 int fresh=valid&&r.updated<=now&&now-r.updated<4000&&r.state==RVF_STREAMING;
 double fps=rvf_rate(&r);
 char b[1024];
 int n=snprintf(b,sizeof b,"{\"kind\":\"NXRVF2\",\"state\":%d,\"fresh\":%s,\"reason\":\"%s\",\"frames\":%llu,\"bytes\":%llu,\"width\":%u,\"height\":%u,\"codec\":\"%s\",\"nominal_fps\":%.3f,\"native_received_fps\":%.3f,\"frequency_mhz\":%d,\"web_sent_frames\":%llu}",
     valid?r.state:RVF_STARTING,fresh?"true":"false",valid?r.reason:"STOCK_WAITING",r.frames,r.bytes,r.width,r.height,r.codec,r.usec?1000000.0/r.usec:0,fps,
     rvf_frequency_mhz,rvf_push_frames);
 if(n<0||(size_t)n>=sizeof b){text_response(c,503,"RVF_STATE_UNAVAILABLE");return;}
 response(c,200,"application/json",b,(size_t)n);
}
static void rvf_frame_response(struct client*c,const char*q){
 if(!rvf_viewer(c))return;
 unsigned long long after=0;char value[25];
 if(query(q,"after",value,sizeof value)){
     char *end;errno=0;after=strtoull(value,&end,10);
     if(errno||!*value||*end||strspn(value,"0123456789")!=strlen(value)){
         text_response(c,400,"BAD_FRAME_SEQUENCE");return;
     }
 }
 struct rvf_record r;unsigned long long now=monotime();
 if(!rvf_current(&r)||r.state!=RVF_STREAMING||r.updated>now||now-r.updated>4000){text_response(c,503,"NATIVE_RVF_NOT_STREAMING");return;}
 int fd=open(RVF_FRAME,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
 struct stat st;struct rvf_picture h;
 int ok=fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&read(fd,&h,sizeof h)==sizeof h&&h.magic==RVF_PICTURE_MAGIC&&h.size<=RVF_FRAME_CAP&&h.size>=16&&st.st_size==(off_t)(sizeof h+h.size)&&h.when>=r.started&&h.when<=now&&now-h.when<=3000&&h.width==r.width&&h.height==r.height;
 if(!ok){if(fd>=0)close(fd);text_response(c,503,"NATIVE_FRAME_UNAVAILABLE");return;}
 if(h.id<=after){close(fd);response(c,204,"image/jpeg",NULL,0);return;}
 c->data=malloc(h.size);
 if(!c->data){close(fd);text_response(c,503,"MEMORY_UNAVAILABLE");return;}
 ok=read(fd,c->data,h.size)==(ssize_t)h.size;
 close(fd);
 if(!ok){free(c->data);c->data=NULL;text_response(c,503,"NATIVE_FRAME_INVALID");return;}
 c->length=h.size;c->bulk=1;c->is_frame=1;c->frame_ready=now;
 c->head_len=(size_t)snprintf(c->header,sizeof c->header,"HTTP/1.1 200 OK\r\nContent-Length: %u\r\nContent-Type: image/jpeg\r\nConnection: close\r\nCache-Control: no-store\r\nX-NX-Width: %u\r\nX-NX-Height: %u\r\nX-NX-Native-Frame-Id: %llu\r\nX-NX-Native-Age-Ms: %llu\r\n\r\n",h.size,h.width,h.height,h.id,now-h.when);
}
static int rvf_post_allowed(struct client*c){
 char value[100],host[64],expected[96];
 snprintf(host,sizeof host,"%s:%d",bind_ip,WIFI_PORT);
 snprintf(expected,sizeof expected,"http://%s",host);
 return rvf_header(c->request,"Host",value,sizeof value)&&!strcmp(value,host)&&
     rvf_header(c->request,"X-NX-Request",value,sizeof value)&&!strcmp(value,"rvf")&&
     rvf_header(c->request,"Origin",value,sizeof value)&&!strcmp(value,expected);
}
static void rvf_start_response(struct client*c){
 if(native_close_at||native_token[0]){text_response(c,429,"RVF_BROWSER_ALREADY_OPEN");return;}
 unsigned char bytes[16];static const char hex[]="0123456789abcdef";
 int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 int ok=fd>=0&&read(fd,bytes,sizeof bytes)==sizeof bytes;
 if(fd>=0)close(fd);
 if(!ok){text_response(c,503,"RVF_SESSION_UNAVAILABLE");return;}
 for(unsigned i=0;i<sizeof bytes;i++){native_token[2*i]=hex[bytes[i]>>4];native_token[2*i+1]=hex[bytes[i]&15u];}
 native_viewer_seen=monotime();
 char b[72];int n=snprintf(b,sizeof b,"{\"session\":\"%s\"}",native_token);
 response(c,202,"application/json",b,(size_t)n);
}
static void rvf_stop_response(struct client*c){
 if(!rvf_token_valid(c)){text_response(c,403,"RVF_SESSION_REQUIRED");return;}
 if(!native_close_at)native_close_at=monotime()+200;
 text_response(c,202,"NATIVE_RVF_RESTORE_REQUESTED");
}
static void bulk_response(struct client*c){
 for(unsigned i=0;i<WIFI_CLIENTS;i++)if(clients+i!=c&&clients[i].fd>=0&&clients[i].bulk==2){text_response(c,429,"BULK_TEST_BUSY");return;}
 struct rvf_record r;if(rvf_record_read(&r)&&r.dirty){text_response(c,503,"STOP_NATIVE_RVF_FIRST");return;}
 c->data=malloc(32768);if(!c->data){text_response(c,503,"MEMORY_UNAVAILABLE");return;}for(unsigned i=0;i<32768;i++)c->data[i]=(unsigned char)(i*73u+19u);
 c->bulk=2;c->length=32768;c->remaining=8*1024*1024-32768;c->head_len=(size_t)snprintf(c->header,sizeof c->header,"HTTP/1.1 200 OK\r\nContent-Length: 8388608\r\nContent-Type: application/octet-stream\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n");
}
