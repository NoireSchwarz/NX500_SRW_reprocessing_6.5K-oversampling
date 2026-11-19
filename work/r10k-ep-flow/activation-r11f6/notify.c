/* Reuse the pinned display-only toast; record silent display failures.
 * The alarm affects only this notifier, never the camera or installer. */
#include "common.h"
#include <X11/Xlib.h>
static int notice_opened,notice_mapped;
static Display *notice_open(const char *name){
 fprintf(stderr,"UI_XOPEN_BEGIN\n");errno=0;Display *d=XOpenDisplay(name);int e=errno;
 fprintf(stderr,"UI_XOPEN_END connected=%d errno=%d\n",d!=NULL,e);
 notice_opened=d!=NULL;return d;
}
static int notice_map(Display *d,Window w){
 fprintf(stderr,"UI_XMAP_BEGIN\n");int rc=XMapRaised(d,w);notice_mapped=1;
 fprintf(stderr,"UI_XMAP_END rc=%d\n",rc);return rc;
}
static int notice_sync(Display *d,Bool discard){
 fprintf(stderr,"UI_XSYNC_BEGIN\n");int rc=XSync(d,discard);
 fprintf(stderr,"UI_XSYNC_END rc=%d\n",rc);return rc;
}
#define XOpenDisplay notice_open
#define XMapRaised notice_map
#define XSync notice_sync
#define main nxl_legacy_menu_main
#include "legacy_menu.inc"
#undef main
#undef XOpenDisplay
#undef XMapRaised
#undef XSync
int main(int argc,char**argv){
 setvbuf(stderr,NULL,_IONBF,0);
 if(argc!=2)return 2;
 if(!strcmp(argv[1],"--version")){puts("NXL_R11F6_ACTIVATION_NOTICE1");return 0;}
 fprintf(stderr,"UI_BEGIN option=%s pid=%ld timeout_seconds=40\n",argv[1],(long)getpid());
 signal(SIGALRM,SIG_DFL);alarm(40);
 if(!nxl_supported()){fprintf(stderr,"UI_UNSUPPORTED_CAMERA\n");return 1;}
 if(!getenv("DISPLAY"))setenv("DISPLAY",":0",0);
 if(!strcmp(argv[1],"--working"))toast_timed("R11f6 BACKUP / ACTIVATION - KEEP POWER ON",1,12000);
 else if(!strcmp(argv[1],"--backup-ok"))toast_timed("R11f6 BACKUP VERIFIED - ACTIVATING",1,5000);
 else if(!strcmp(argv[1],"--backup-failed"))toast_timed("R11f6 BACKUP FAILED - STOP / KEEP LOGS",0,30000);
 else if(!strcmp(argv[1],"--active"))toast_timed("R11f6 CORE VERIFIED - HEVC A/B/A READY",1,12000);
 else if(!strcmp(argv[1],"--restored"))toast_timed("R11f6 OEM RESTORED - DO NOT RUN A/B/A",0,12000);
 else if(!strcmp(argv[1],"--failed"))toast_timed("R11f6 ACTIVATION FAILED - STOP / KEEP LOGS",0,30000);
 else return 2;
 alarm(0);
 fprintf(stderr,"UI_END connected=%d mapped=%d xerror=%d\n",notice_opened,notice_mapped,xerror);
 return notice_opened&&notice_mapped&&!xerror?0:3;
}
