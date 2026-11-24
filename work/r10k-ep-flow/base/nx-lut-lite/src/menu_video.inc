#include "video_menu_state.h"
#include "video_probe_state.h"
#include "gamma_reload_state.h"
#include "color_observe_state.h"
static struct nxl_color_status color_status;
static struct nxl_gamma_status gamma_status;static int gamma_command_failed;
static int video_fd=-1,video_row,video_timeout_sent;static size_t video_used;static char video_output[2048];static unsigned long video_started;static struct nxl_video_status video_status;
static struct nxl_probe_status probe_status;
static void video_menu_start(int command){
 if(video_child>0){strcpy(notice,"VIDEO UPDATE RUNNING - PLEASE WAIT");return;}
 if(command&&(rearm_child>0||lens_child>0)){strcpy(notice,"WAIT FOR CURRENT MENU ACTION");return;}
 const char*mode=command==30?"color-observe-toggle":command==31?"color-observe-status":command==20?"gamma-lift":command==21?"gamma-log":command==22?"gamma-restore":command==23?"gamma-status":command==24?"gamma-toggle":command==1?"video-2k":command==2?"video-uhd":command==3?"video-full":command==4?"video-crop80":command==10?"video-probe-start":command==11?"video-probe-stop":command==12?"video-probe-mark":command==13?"video-probe-status":"video-status";int fd;pid_t p=backend(mode,"",&fd);if(p<0){strcpy(notice,"VIDEO CONTROL COULD NOT START");return;}
 video_child=p;video_fd=fd;video_command=command;video_used=0;video_output[0]=0;video_timeout_sent=0;video_started=ticks();int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);video_fd=-1;kill(p,SIGTERM);}
 strcpy(notice,command>=30?"READING COLOR PARAMETERS":command>=20?"UPDATING NATIVE ISP GAMMA":command>=10?"UPDATING STAGE COUNTERS":command?"CHANGING VIDEO OUTPUT":"READING VIDEO OUTPUT");
}
static void video_menu_read(void){for(unsigned i=0;video_fd>=0&&i<16;i++){char b[256];ssize_t n=read(video_fd,b,sizeof b);if(n<0&&errno==EINTR)continue;if(n<=0)break;size_t take=(size_t)n;if(take>sizeof video_output-1-video_used)take=sizeof video_output-1-video_used;memcpy(video_output+video_used,b,take);video_used+=take;video_output[video_used]=0;}}
static int video_menu_poll(void){
 if(video_child<=0)return 0;video_menu_read();if(!video_timeout_sent&&ticks()-video_started>42000UL){kill(video_child,SIGTERM);video_timeout_sent=1;}
 int status;pid_t got=waitpid(video_child,&status,WNOHANG);if(!got||(got<0&&errno==EINTR))return 0;video_menu_read();if(video_fd>=0)close(video_fd);video_fd=-1;video_child=0;
 if(video_command>=30){
  struct nxl_color_status parsed_status;int parsed=nxl_color_parse(video_output,&parsed_status),ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)&&parsed;
  if(!parsed){strcpy(notice,"COLOR RESPONSE FAILED - CHECK STATUS");return 1;}
  color_status=parsed_status;
  if(!ok)snprintf(notice,sizeof notice,"%s",color_status.reason);
  else if(color_status.state==1)strcpy(notice,"COLOR ON - RECORD FACTORY / LOG / FACTORY");
  else if(color_status.state==2)strcpy(notice,"STOP VIDEO / WAIT FOR COLOR SAVE");
  else if(color_status.state==3)strcpy(notice,"COLOR IN RAM - UNPLUG USB, THEN SAVE");
  else if(color_status.state==4)strcpy(notice,"COLOR FULL - STOP VIDEO THEN SAVE");
  else if(color_status.state==5)strcpy(notice,color_status.valid?"COLOR SAVED - SAFE TO CONNECT USB":"SAVED / NO VALID BANK0 / USB SAFE");
  else if(color_status.state==6)strcpy(notice,"COLOR ERROR - SELECT TO SAVE RAM");
  else strcpy(notice,"COLOR OFF - SELECT TO START");
  return 1;
 }
 if(video_command>=20){
  int parsed=nxl_gamma_parse(video_output,&gamma_status),ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)&&parsed;
  gamma_command_failed=!ok;
  if(video_command==21||video_command==22||video_command==24)refresh_lut_runtime();
  if(ok&&detail_visible&&(video_command==21||video_command==22||video_command==24))detail_open_current();
  if(!ok&&strstr(video_output,"NXL_GAMMA_CONFIGURED1\n"))strcpy(notice,
     strstr(video_output,"ERR LUT_RELOAD_RESTART_REQUIRED\n")?"曲線已切換，請重新開機":"曲線已切換，LUT重載未確認");
  else if(!parsed)strcpy(notice,"GAMMA RESPONSE INVALID - CHECK GAMMA LOG");
  else if(!ok)snprintf(notice,sizeof notice,"%s",gamma_status.reason);
  else if(gamma_status.phase==5)strcpy(notice,"GAMMA NATIVE FAILURE - SELECT TO RESTORE");
  else if(gamma_status.phase==2||gamma_status.phase==1)strcpy(notice,"GAMMA WAIT: ENTER VIDEO; OK REQUESTS FACTORY");
  else if(gamma_status.phase==4)strcpy(notice,"原廠曲線已要求，LUT已還原");
  else if(gamma_status.phase==3)strcpy(notice,gamma_status.mode==2?"V‑Log已裝載，原LUT已清除":"LIFT GAMMA QUEUED - RECORD TO VERIFY");
  else strcpy(notice,"FACTORY / SELECT: VLOG8");
  /* The release menu does not poll the hidden colour experiment. */
  return 1;
 }
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
  else if(probe_status.state==1){
   if(probe_status.experiment==1)strcpy(notice,"A/B/A READY / RECORD 120s / STOP+SAVE");
   else if(probe_status.experiment==2)snprintf(notice,sizeof notice,"A/B/A PHASE %u/3 / STOP+SAVE",probe_status.experiment_epoch);
   else if(probe_status.experiment==3)strcpy(notice,"A/B/A DONE 3/3 / STOP+SAVE");
   else if(probe_status.experiment==5)snprintf(notice,sizeof notice,"HEVC A/B/A UNAVAILABLE (%u) - STOP+SAVE SHORT LOG",probe_status.experiment_reason);
   else if(probe_status.experiment==4)snprintf(notice,sizeof notice,"HEVC A/B/A ABORTED (%u) - STOP+SAVE",probe_status.experiment_reason);
   else strcpy(notice,"HEVC A/B/A UNKNOWN - STOP+SAVE SHORT LOG");
  }
  else if(probe_status.state==2)strcpy(notice,"STOP VIDEO - COUNTERS WAIT FOR FILE SAVE");
  else if(probe_status.state==3)strcpy(notice,"RAM PENDING - SELECT COUNTERS TO SAVE");
  else if(probe_status.state==4)strcpy(notice,"RAM FULL - STOP VIDEO, THEN SAVE COUNTERS");
  else if(!strcmp(probe_status.reason,"TRACE_SAVED")){
   if(probe_status.experiment==3)strcpy(notice,"SAVED / TEST COMPLETE / SAFE TO CONNECT USB");
   else if(probe_status.experiment==5)strcpy(notice,"SAVED / TEST UNAVAILABLE / SAFE TO CONNECT USB");
   else strcpy(notice,"SAVED / TEST INCOMPLETE / SAFE TO CONNECT USB");
  }else strcpy(notice,"COUNTERS OFF - SELECT COUNTERS TO START");
  return 1;
 }
 int ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)&&nxl_video_parse(video_output,&video_status);
 if(!ok){if(!nxl_video_parse(video_output,&video_status))memset(&video_status,0,sizeof video_status);
  if(strstr(video_status.reason,"USB_PHYSICAL_STATE_UNAVAILABLE"))strcpy(notice,"USB PORT READ FAILED - SEE STARTUP LOG");
  else if(strstr(video_status.reason,"USB_CONNECTED"))strcpy(notice,"UNPLUG USB BEFORE CHANGING VIDEO");
  else if(strstr(video_status.reason,"USB_CONFIG_")||strstr(video_status.reason,"USB_STATE_UNAVAILABLE"))strcpy(notice,"USB CONFIG CHECK FAILED - SEE USB STATE LOG");
  else if(strstr(video_status.reason,"RESTART_REQUIRED"))strcpy(notice,"VIDEO STATE UNCONFIRMED - RESTART CAMERA");
  else if(strstr(video_status.reason,"SELECT_NTSC_DCI24"))strcpy(notice,"CAMERA SETTINGS: NTSC / 4K24 / PRO, THEN APPLY");
  else if(strstr(video_status.reason,"SELECT_NTSC_UHD30"))strcpy(notice,"CAMERA SETTINGS: NTSC / UHD 30P, THEN APPLY");
  else if(strstr(video_status.reason,"PHOTO_MODE_REQUIRED"))strcpy(notice,"RETURN TO PHOTO MODE THEN TRY AGAIN");
  else if(strstr(video_status.reason,"RECORDING")||strstr(video_status.reason,"BUSY"))strcpy(notice,"STOP RECORDING / WAIT FOR SRW TASK");
  else if(strstr(video_status.reason,"VIDEO_SETTINGS_"))strcpy(notice,"VIDEO SETTINGS READ FAILED - CHECK VIDEO LOG");
  else if(strstr(video_status.reason,"VIDEO_NATIVE_"))strcpy(notice,"VIDEO LIBRARY CHECK FAILED - CHECK VIDEO LOG");
  else if(strstr(video_status.reason,"VIDEO_BITRATE_CONTEXT"))strcpy(notice,"VIDEO BITRATE CHECK FAILED - CHECK VIDEO LOG");
  else if(strstr(video_status.reason,"NX500_FIRMWARE_IDENTITY"))strcpy(notice,"CAMERA VERSION NOT SUPPORTED");
  else if(strstr(video_status.reason,"IDENTITY"))strcpy(notice,"VIDEO COMPONENT CHECK FAILED - CHECK VIDEO LOG");
  else strcpy(notice,"VIDEO CHANGE FAILED - CHECK VIDEO LOG");
 }else if(video_command==3&&video_status.mode==3){strcpy(notice,"全寬UHD24規格已重新提交");}
 else if(video_command==4)strcpy(notice,"原生UHD30規格已重新提交");
 else if(video_command)strcpy(notice,video_status.mode==2?"2.5K規格已重新提交":"原生UHD規格已重新提交");
 else {strcpy(notice,video_status.busy==1?"CAMERA IS RECORDING / SAVING A PHOTO":"VIDEO OUTPUT STATUS UPDATED");if(ok)video_menu_start(23);}return 1;
}




#include "menu_video_picker.inc"
static const unsigned char release_video_rows[]={0,9,6,8};
static int release_video_count(void){return sizeof release_video_rows/sizeof*release_video_rows;}
static int release_video_index(void){for(int i=0;i<release_video_count();i++)if(video_row==release_video_rows[i])return i;return 0;}
static void video_navigate(int value){int n=release_video_count();video_row=release_video_rows[(value%n+n)%n];notice[0]=0;}
static void video_confirm(void){
 if(video_row==9){video_picker_begin();return;}
 if(video_row==8){detail_open_log();return;}
 if(video_row==6){video_menu_start(nxl_gamma_next_command(&gamma_status,gamma_command_failed));return;}
 if(video_row<5)video_menu_start(video_row==1?3:video_row==2?4:video_row==3?1:video_row==4?2:0);
}
static void draw_video(void){
 help.visible=0;help.suppressed=1;fill(0,0,720,480,bg);XSetForeground(dpy,gc,ink);text(24,20,"影片設定");text(552,20,"MENU 返回");
 XSetForeground(dpy,gc,muted);text(24,60,"NX LUT Lite 0.4.0 RC6.14.11");frame(18,96,684,298,line_color);draw_sidebar();
 int cursor=release_video_index(),first=cursor/5*5;
 for(int row=0;row<5&&first+row<release_video_count();row++){
  int index=first+row,id=release_video_rows[index],y=104+row*56;
  if(index==cursor)for(int k=0;k<8;k++)fill(96,y+k*7,588,7,sidebar_focus?button_bg:selection_colors[k]);else fill(108,y+55,568,1,line_color);
  const char*title=id==9?"畫面模式":id==0?"輸出狀態":id==1?"全寬UHD 24p":id==2?"原生裁切UHD 30p":id==3?"UHD轉2.5K":id==4?"還原原生UHD":id==6?"V‑Log開關":"Log LUT";
  const char*value=id==9?"開啟三項畫面設定":id==0?(!video_status.valid?"讀取狀態":video_status.mode==3||video_status.mode==4?"全寬":video_status.mode==2?"2560×1440":video_status.mode==1?"原生UHD":"其他輸出"):id==6?(gamma_command_failed||gamma_status.phase==5?"失敗／還原":gamma_status.phase==1||gamma_status.phase==2?"等待原廠更新":access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0?"開啟／還原":"關閉／啟用"):id==8?"進入Log專用選擇器":"OK 套用";
  XSetForeground(dpy,gc,ink);limited_text(122,y+14,title,260);limited_text(390,y+14,value,285);
 }
 XSetForeground(dpy,gc,muted);limited_text(24,400,notice[0]?notice:"切換前停止錄影並回到照片待機",650);
 XSetForeground(dpy,gc,ink);text(24,442,"左右 切換功能／內容");text(320,442,"OK 選擇");text(540,442,"MENU 返回");draw_video_picker();publish_ui();
}
