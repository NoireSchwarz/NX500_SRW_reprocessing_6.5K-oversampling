/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_RVF_H
#define NXL_WIFI_RVF_H
#include "wifi_rvf_protocol.h"
#include "wifi_rvf_contract.h"
#include <stddef.h>
#define RVF_STATE WIFI_TMP "/wifi-rvf-rc613"
#define RVF_FRAME WIFI_TMP "/wifi-rvf-frame-rc613"
#define RVF_EXE "/opt/usr/nx-lut-lite/bin/nxl-wifi-server"
#define RVF_MAGIC 0x52563612u
enum { RVF_IDLE,RVF_STARTING,RVF_STREAMING,RVF_RESTORING,RVF_DONE,RVF_FAILED,RVF_HOLD };
struct rvf_record {
 uint32_t magic,checksum;int32_t pid,server,camera,state,dirty,restored,ap_restored;uint32_t requests;
 unsigned long long tick,server_tick,camera_tick,updated,started,first_frame,last_frame,frames,bytes;
 uint32_t width,height,usec;char codec[8],ip[32],reason[80];
};
struct rvf_picture { uint32_t magic,size,width,height;unsigned long long id,when; };
static struct rvf_record rvf;
_Static_assert(sizeof(struct rvf_record)==248&&offsetof(struct rvf_record,tick)==40,"RVF journal prefix unchanged");
#include "wifi_rvf_ui.h"
static int rvf_publish(const char*reason){if(strcmp(rvf.reason,reason))fprintf(stderr,"RVF_STATE phase=%d dirty=%d frames=%llu reason=%s\n",rvf.state,rvf.dirty,rvf.frames,reason);rvf.updated=monotime();snprintf(rvf.reason,sizeof rvf.reason,"%s",reason);int ok=wire_write(RVF_STATE,&rvf,sizeof rvf,RVF_MAGIC);if(!ok)fprintf(stderr,"RVF_JOURNAL_FAILED\n");return ok;}
static int rvf_record_read(struct rvf_record*r){return wire_read(RVF_STATE,r,sizeof*r,RVF_MAGIC)&&r->state>=RVF_IDLE&&r->state<=RVF_HOLD&&r->dirty>=0&&r->dirty<=1&&r->requests<=3&&r->restored>=0&&r->restored<=1&&r->ip[31]==0&&r->reason[79]==0&&r->codec[7]==0&&strspn(r->ip,"0123456789.")==strlen(r->ip)&&strspn(r->reason,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==strlen(r->reason)&&strspn(r->codec,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==strlen(r->codec);}
static int rvf_native_state(pid_t pid,unsigned long long tick,uint32_t*out){
 if(proc_ticks(pid)!=tick)return 0;char path[80];snprintf(path,sizeof path,"/proc/%ld/mem",(long)pid);int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;
 unsigned char code[28];uint32_t a,b;int ok=hot_read(f,0x185288u,code,sizeof code)&&!memcmp(code,rvf_active_code,sizeof code)&&hot_read(f,0x4a4bccu,&a,4)&&hot_read(f,0x4a4bccu,&b,4)&&a==b&&a<=2&&proc_ticks(pid)==tick;close(f);if(ok)*out=a;return ok;
}
static int rvf_port_owned(pid_t pid,unsigned long long tick,unsigned port){
 if(proc_ticks(pid)!=tick)return 0;char path[96],line[512];unsigned long long inode=0;unsigned matches=0;
 FILE*f=fopen("/proc/net/tcp","r");if(!f)return 0;
 while(fgets(line,sizeof line,f)){unsigned local,lp,remote,rp,state;unsigned long long node;char tx[32],tim[32],ret[32];unsigned uid,timeout;
  if(sscanf(line," %*u: %x:%x %x:%x %x %31s %31s %31s %u %u %llu",&local,&lp,&remote,&rp,&state,tx,tim,ret,&uid,&timeout,&node)!=11)continue;
  if(state==10&&lp==port){inode=node;matches++;}
 }int ok=!ferror(f)&&matches==1&&inode;if(fclose(f))ok=0;if(!ok)return 0;
 snprintf(path,sizeof path,"/proc/%ld/fd",(long)pid);DIR*d=opendir(path);if(!d)return 0;struct dirent*e;int found=0;
 while((e=readdir(d))){if(strspn(e->d_name,"0123456789")!=strlen(e->d_name)||!e->d_name[0])continue;char file[128],link[80];snprintf(file,sizeof file,"%s/%s",path,e->d_name);ssize_t n=readlink(file,link,sizeof link-1);if(n<=0)continue;link[n]=0;unsigned long long node;char extra;if(sscanf(link,"socket:[%llu]%c",&node,&extra)==1&&node==inode){found=1;break;}}
 closedir(d);return found&&proc_ticks(pid)==tick;
}

#include "wifi_rvf_transport.inc"
#include "wifi_rvf_session.inc"
#endif
