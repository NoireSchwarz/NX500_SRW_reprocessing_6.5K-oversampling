#include "video_menu_state.h"
#include "video_probe_state.h"
static int video_fd=-1,video_row,video_timeout_sent;static size_t video_used;static char video_output[2048];static unsigned long video_started;static struct nxl_video_status video_status;
static struct nxl_probe_status probe_status;
static void video_menu_start(int command){
 if(video_child>0){strcpy(notice,"VIDEO UPDATE RUNNING - PLEASE WAIT");return;}
 if(command&&(rearm_child>0||lens_child>0)){strcpy(notice,"WAIT FOR CURRENT MENU ACTION");return;}
 const char*mode=command==1?"video-2k":command==2?"video-uhd":command==3?"video-full":command==4?"video-crop80":command==10?"video-probe-start":command==11?"video-probe-stop":command==12?"video-probe-mark":command==13?"video-probe-status":"video-status";int fd;pid_t p=backend(mode,"",&fd);if(p<0){strcpy(notice,"VIDEO CONTROL COULD NOT START");return;}
 video_child=p;video_fd=fd;video_command=command;video_used=0;video_output[0]=0;video_timeout_sent=0;video_started=ticks();int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);video_fd=-1;kill(p,SIGTERM);}
 strcpy(notice,command>=10?"UPDATING STAGE COUNTERS":command?"CHANGING VIDEO OUTPUT":"READING VIDEO OUTPUT");
}
static void video_menu_read(void){for(unsigned i=0;video_fd>=0&&i<16;i++){char b[256];ssize_t n=read(video_fd,b,sizeof b);if(n<0&&errno==EINTR)continue;if(n<=0)break;size_t take=(size_t)n;if(take>sizeof video_output-1-video_used)take=sizeof video_output-1-video_used;memcpy(video_output+video_used,b,take);video_used+=take;video_output[video_used]=0;}}
static int video_menu_poll(void){
 if(video_child<=0)return 0;video_menu_read();if(!video_timeout_sent&&ticks()-video_started>42000UL){kill(video_child,SIGTERM);video_timeout_sent=1;}
 int status;pid_t got=waitpid(video_child,&status,WNOHANG);if(!got||(got<0&&errno==EINTR))return 0;video_menu_read();if(video_fd>=0)close(video_fd);video_fd=-1;video_child=0;
 if(video_command>=10){
  int parsed=nxl_probe_parse(video_output,&probe_status),ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)&&parsed;
  if(!parsed){memset(&probe_status,0,sizeof probe_status);strcpy(notice,"COUNTER RESPONSE FAILED - RETRY STATUS");}
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_STOP_VIDEO_FIRST"))strcpy(notice,"STOP RECORDING BEFORE CHANGING BUS TEST");
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_ALREADY_QOS_B"))strcpy(notice,"BRIDGE ALREADY B / NO A-B CONTRAST");
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_UNAVAILABLE"))strcpy(notice,"BUS TEST UNAVAILABLE / COUNTERS STILL WORK");
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_EXTERNAL_CHANGE"))strcpy(notice,"BRIDGE SETTING CHANGED EXTERNALLY / LEFT AS IS");
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_RESTORE_FAILED"))strcpy(notice,"BRIDGE RESTORE FAILED / STOP+SAVE TO RETRY");
  else if(!ok)snprintf(notice,sizeof notice,"%s",probe_status.reason);
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_QOS_B"))strcpy(notice,"B: BRIDGE 0x14A / OK AGAIN RESTORES A");
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_BASELINE"))strcpy(notice,"A: ORIGINAL BRIDGE SETTING RESTORED");
  else if(!strcmp(probe_status.reason,"TRACE_COMPARE_REQUESTED"))strcpy(notice,"BUS TEST REQUESTED / RECHECK COUNTER STATUS");
  else if(probe_status.state==1)snprintf(notice,sizeof notice,"COUNTERS ON / %u SAMPLES / %u MS",probe_status.samples,probe_status.period_ms);
  else if(probe_status.state==2)strcpy(notice,"STOP VIDEO - COUNTERS WAIT FOR FILE SAVE");
  else if(probe_status.state==3)strcpy(notice,"RAM PENDING - SELECT COUNTERS TO SAVE");
  else if(probe_status.state==4)strcpy(notice,"RAM FULL - STOP VIDEO, THEN SAVE COUNTERS");
  else strcpy(notice,!strcmp(probe_status.reason,"TRACE_SAVED")?"COUNTERS SAVED - SAFE TO CONNECT USB":"COUNTERS OFF - SELECT COUNTERS TO START");
  return 1;
 }
 int ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)&&nxl_video_parse(video_output,&video_status);
 if(!ok){if(!nxl_video_parse(video_output,&video_status))memset(&video_status,0,sizeof video_status);
  if(strstr(video_status.reason,"USB_PHYSICAL_STATE_UNAVAILABLE"))strcpy(notice,"USB PORT READ FAILED - SEE STARTUP LOG");
  else if(strstr(video_status.reason,"USB_CONNECTED"))strcpy(notice,"UNPLUG USB BEFORE CHANGING VIDEO");
  else if(strstr(video_status.reason,"USB_CONFIG_")||strstr(video_status.reason,"USB_STATE_UNAVAILABLE"))strcpy(notice,"USB CONFIG CHECK FAILED - SEE USB STATE LOG");
  else if(strstr(video_status.reason,"RESTART_REQUIRED"))strcpy(notice,"VIDEO STATE UNCONFIRMED - RESTART CAMERA");
  else if(strstr(video_status.reason,"SELECT_NTSC_UHD30"))strcpy(notice,"CAMERA SETTINGS: NTSC / UHD 30P, THEN APPLY");
  else if(strstr(video_status.reason,"PHOTO_MODE_REQUIRED"))strcpy(notice,"RETURN TO PHOTO MODE THEN TRY AGAIN");
  else if(strstr(video_status.reason,"RECORDING")||strstr(video_status.reason,"BUSY"))strcpy(notice,"STOP RECORDING / WAIT FOR SRW TASK");
  else if(strstr(video_status.reason,"VIDEO_SETTINGS_"))strcpy(notice,"VIDEO SETTINGS READ FAILED - CHECK VIDEO LOG");
  else if(strstr(video_status.reason,"VIDEO_NATIVE_"))strcpy(notice,"VIDEO LIBRARY CHECK FAILED - CHECK VIDEO LOG");
  else if(strstr(video_status.reason,"VIDEO_BITRATE_CONTEXT"))strcpy(notice,"VIDEO BITRATE CHECK FAILED - CHECK VIDEO LOG");
  else if(strstr(video_status.reason,"NX500_FIRMWARE_IDENTITY"))strcpy(notice,"CAMERA VERSION NOT SUPPORTED");
  else if(strstr(video_status.reason,"IDENTITY"))strcpy(notice,"VIDEO COMPONENT CHECK FAILED - CHECK VIDEO LOG");
  else strcpy(notice,"VIDEO CHANGE FAILED - CHECK VIDEO LOG");
 }else if(video_command==3&&video_status.mode==3){strcpy(notice,"4K30P / 80M - RESELECT UHD30P / PRO TO LOAD");}
 else if(video_command==4)strcpy(notice,"CROP 4K30P / 80M - RESELECT UHD30P / PRO");
 else if(video_command)strcpy(notice,video_status.mode==2?"2.5K SELECTED - RESELECT UHD ON CAMERA":"NATIVE UHD / PRO 70M RESTORED");
 else {strcpy(notice,video_status.busy==1?"CAMERA IS RECORDING / SAVING A PHOTO":"VIDEO OUTPUT STATUS UPDATED");if(ok)video_menu_start(13);}return 1;
}
static void video_navigate(int row){video_row=(row+7)%7;notice[0]=0;help.visible=0;nxl_help_reset(&help,ticks());}
static void video_confirm(void){
 if(video_child>0){strcpy(notice,"VIDEO UPDATE RUNNING - PLEASE WAIT");return;}
 if(video_row<5)video_menu_start(video_row==1?3:video_row==2?4:video_row==3?1:video_row==4?2:0);
 else video_menu_start(video_row==5?(probe_status.state?11:10):12);
}
static void draw_video(void){
 help.visible=0;help.suppressed=1;fill(0,0,720,480,bg);XSetForeground(dpy,gc,ink);text(24,20,"VIDEO");text(552,20,"MENU 返回");XSetForeground(dpy,gc,muted);text(24,60,"R10j HW PROGRESS / BUS A-B TEST");frame(18,96,684,298,line_color);draw_sidebar();
 static const char*titles[]={"OUTPUT MAPPING","FULL WIDTH 4K 30P","CROP 4K 30P / 80M","UHD TO 2.5K","RESTORE NATIVE UHD","STAGE COUNTERS","BUS QOS A/B"};
 for(int i=0;i<7;i++){int y=104+i*40;if(i==video_row)for(int k=0;k<6;k++)fill(96,y+k*6,588,6,selection_colors[k]);else fill(108,y+39,568,1,line_color);XSetForeground(dpy,gc,ink);text(122,y+9,titles[i]);const char*value=i==0?!video_status.valid?"READ STATUS":video_status.mode==4?"FULL / REAPPLY":video_status.mode==3?"FULL / CONFIGURED":video_status.mode==2?"2560 x 1440":video_status.mode==1?"NATIVE / 4K":"OTHER / UNKNOWN":i<4?"OK / APPLY":i==4?"OK / RESTORE":i==5?probe_status.state==1?"ON / STOP+SAVE":probe_status.state?"PENDING / SAVE":"OFF / START":"OK / TOGGLE BUS";limited_text(438,y+9,value,240);}
 XSetForeground(dpy,gc,muted);limited_text(24,400,notice[0]?notice:"SET NTSC / UHD 30P / PRO, THEN APPLY FULL",670);text(24,444,"拨轮 切栏");text(310,444,"OK 执行");text(530,444,"MENU 返回");publish_ui();
}
