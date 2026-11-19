/* Reuse the pinned menu's display-only toast implementation. */
#define main nxl_legacy_menu_main
#include "legacy_menu.inc"
#undef main
int main(int argc,char**argv){
 if(argc!=2)return 2;
 if(!strcmp(argv[1],"--version")){puts("NXL_R11F5_ACTIVATION_NOTICE1");return 0;}
 if(!nxl_supported())return 1;if(!getenv("DISPLAY"))setenv("DISPLAY",":0",0);
 if(!strcmp(argv[1],"--working"))toast_timed("R11f5 BACKUP / ACTIVATION - KEEP POWER ON",1,12000);
 else if(!strcmp(argv[1],"--backup-ok"))toast_timed("R11f5 BACKUP VERIFIED - ACTIVATING",1,5000);
 else if(!strcmp(argv[1],"--backup-failed"))toast_timed("R11f5 BACKUP FAILED - STOP / KEEP LOGS",0,30000);
 else if(!strcmp(argv[1],"--active"))toast_timed("R11f5 CORE VERIFIED - HEVC A/B/A READY",1,12000);
 else if(!strcmp(argv[1],"--restored"))toast_timed("R11f5 OEM RESTORED - DO NOT RUN A/B/A",0,12000);
 else if(!strcmp(argv[1],"--failed"))toast_timed("R11f5 ACTIVATION FAILED - STOP / KEEP LOGS",0,30000);
 else return 2;return 0;
}
