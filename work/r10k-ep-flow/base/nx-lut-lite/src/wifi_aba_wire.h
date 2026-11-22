/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_ABA_WIRE_H
#define NXL_ABA_WIRE_H
#define ABA_TRANSFER WIFI_TMP "/wifi-transfer-rc611"
#define ABA_PUBLIC WIFI_TMP "/wifi-aba-rc611"
enum aba_state{ABA_OFF,ABA_WAIT_AP,ABA_WAIT_DATA,ABA_A1,ABA_B,ABA_A2,ABA_DONE,ABA_ABORT,ABA_HOLD,ABA_SKIP};
struct transfer_wire{uint32_t magic,checksum;int32_t pid,reserved;unsigned long long tick,updated,body,frames,files;};
struct aba_public{uint32_t magic,checksum;int32_t pid,state;unsigned long long tick,updated,elapsed,body[3],net[3];uint32_t restored,changed,stage_valid[3],reason;};
static uint32_t wire_hash(const void*p,size_t n){const unsigned char*b=p;uint32_t h=2166136261u;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;return h;}
static int wire_write(const char*path,void*p,size_t n,uint32_t magic){
 uint32_t*w=p;w[0]=magic;w[1]=0;w[1]=wire_hash(p,n);char tmp[240];static unsigned seq;snprintf(tmp,sizeof tmp,"%s.%ld.%u",path,(long)getpid(),++seq);int f=open(tmp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 0;
 int ok=write(f,p,n)==(ssize_t)n&&!fsync(f);if(close(f))ok=0;if(ok)ok=!rename(tmp,path);if(!ok)unlink(tmp);return ok;
}
static int wire_read(const char*path,void*p,size_t n,uint32_t magic){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;if(f<0)return 0;int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)n&&read(f,p,n)==(ssize_t)n;close(f);if(!ok)return 0;uint32_t*w=p,expected=w[1];if(w[0]!=magic)return 0;w[1]=0;ok=wire_hash(p,n)==expected;w[1]=expected;return ok;
}
static int wire_process(pid_t pid,unsigned long long tick,const char*exe){
 if(pid<=1||!tick||proc_ticks(pid)!=tick)return 0;char path[80],name[PATH_MAX];snprintf(path,sizeof path,"/proc/%ld/exe",(long)pid);ssize_t n=readlink(path,name,sizeof name-1);if(n<=0)return 0;name[n]=0;return !strcmp(name,exe);
}
static struct transfer_wire transfer;
static unsigned long long transfer_next;
static void transfer_publish(int force){unsigned long long now=monotime();if(!force&&now<transfer_next)return;transfer.pid=getpid();transfer.tick=proc_ticks(getpid());transfer.updated=now;transfer_next=now+1000;(void)wire_write(ABA_TRANSFER,&transfer,sizeof transfer,0x54523611u);}
struct client;
static void aba_web_status(struct client*c);
#endif
