/* SPDX-License-Identifier: AGPL-3.0-only. Passive physical USB detection.
 * NX5001.12 OEM_sys_get_jack_usb_online reads this kernel attribute through
 * sys_get_int. Software VCONF events are diagnostic only: a missing/stale
 * event must not prevent video changes when the physical jack is unplugged.
 * No sysfs or VCONF writes. No file-size assumptions for sysfs attributes.
 */
#ifndef NXL_OBSERVER_USB_H
#define NXL_OBSERVER_USB_H
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define NXL_OBSERVER_USB_PATH "/var/run/memory/sysman/usb_status"
#define NXL_USB_PHYSICAL_PATH "/sys/class/extcon/USB/state"
static int nxl_usb_last_physical=-1,nxl_usb_physical_errno,nxl_usb_last_event=-1,nxl_usb_event_errno;
static uint32_t nxl_usb_event_type,nxl_usb_event_raw;
static long long nxl_usb_event_bytes=-1;
static int nxl_usb_event_state(void){
 nxl_usb_event_errno=0;nxl_usb_event_type=0;nxl_usb_event_raw=0;nxl_usb_event_bytes=-1;
 int f=open(NXL_OBSERVER_USB_PATH,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(f<0){nxl_usb_event_errno=errno;return -1;}errno=0;
 struct stat a={0},b={0};uint32_t first[2]={0},second[2]={0};int ok=!fstat(f,&a)&&S_ISREG(a.st_mode);nxl_usb_event_bytes=(long long)a.st_size;
 if(ok)ok=pread(f,first,8,0)==8&&pread(f,second,8,0)==8&&!memcmp(first,second,8)&&!fstat(f,&b)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&a.st_mtime==b.st_mtime&&a.st_mtim.tv_nsec==b.st_mtim.tv_nsec;
 int saved=errno;nxl_usb_event_type=first[0];nxl_usb_event_raw=first[1];if(close(f)){ok=0;saved=errno;}
 int value=ok&&first[0]==0x29u&&first[1]<=2u?(int)first[1]:-1;nxl_usb_event_errno=value<0?(saved?saved:EINVAL):0;return value;
}
static int nxl_usb_physical_once(void){
 char data[16];int f=open(NXL_USB_PHYSICAL_PATH,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(f<0){nxl_usb_physical_errno=errno;return-1;}
 ssize_t n;unsigned tries=0;do{n=read(f,data,sizeof data-1);}while(n<0&&errno==EINTR&&++tries<4);
 int saved=errno,closed=close(f);if(n<=0||n==(ssize_t)sizeof(data)-1||closed){nxl_usb_physical_errno=closed?errno:n<0?saved:n>0?EOVERFLOW:EIO;return-1;}
 data[n]=0;unsigned i=0;while(data[i]==' '||data[i]=='\t'||data[i]=='\r'||data[i]=='\n')i++;
 int v=data[i]>='0'&&data[i]<='2'?data[i++]-'0':-1;
 while(data[i]==' '||data[i]=='\t'||data[i]=='\r'||data[i]=='\n')i++;
 if(v<0||data[i]){nxl_usb_physical_errno=EINVAL;return-1;}return v;
}
static int nxl_usb_physical_state(void){
 nxl_usb_physical_errno=0;int a=nxl_usb_physical_once(),b=a<0?-1:nxl_usb_physical_once();
 if(a<0||b<0)return-1;if(a!=b){nxl_usb_physical_errno=EAGAIN;return-1;}return a;
}
static int nxl_observer_usb_state(void){
 nxl_usb_last_physical=nxl_usb_physical_state();
 nxl_usb_last_event=nxl_usb_event_state();
 if(nxl_usb_last_event<0&&!nxl_usb_event_errno)nxl_usb_event_errno=errno?errno:EINVAL;
 return nxl_usb_last_physical<0?-1:nxl_usb_last_physical?1:0;
}
static int nxl_observer_usb_tick(uint64_t stamp,uint64_t*next,int*exit,const char**reason){
 if(stamp<*next)return 1;
 int state=nxl_observer_usb_state();
 if(state!=0){*exit=1;*reason=state>0?"USB_CONNECTED":"USB_STATE_UNAVAILABLE";return 0;}
 *next=stamp+100000000u;return 1;
}
#endif
