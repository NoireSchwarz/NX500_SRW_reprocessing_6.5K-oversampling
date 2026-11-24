/* SPDX-License-Identifier: AGPL-3.0-only. Pick a service before starting Wi-Fi. */
#include "wifi_status.h"
#include "qrcodegen.c"
enum { WIFI_ENTRY_ALBUM_5G, WIFI_ENTRY_ALBUM_24G, WIFI_ENTRY_RVF, WIFI_ENTRY_COUNT, WIFI_NO_ENTRY=-1 };
static const struct {
 const char *title, *page, *start;
} wifi_entries[WIFI_ENTRY_COUNT]={
 [WIFI_ENTRY_ALBUM_5G]={"5 GHz 相簿","?view=media","hotspot"},
 [WIFI_ENTRY_ALBUM_24G]={"2.4 GHz 相簿","?view=media","hotspot24"},
 [WIFI_ENTRY_RVF]={"2.4 GHz 圖傳","?view=rvf","rvf"},
};
static struct nxl_wifi_status wifi_status;
static pid_t wifi_child;static int wifi_fd=-1,wifi_command,wifi_exited,wifi_exit;
static unsigned long wifi_started,wifi_next_status;static char wifi_output[2048];static size_t wifi_used;
static int wifi_close_requested,wifi_close_failed,wifi_exit_unconfirmed,wifi_qr_valid,wifi_flow_ready;
static int wifi_entry=WIFI_NO_ENTRY,wifi_choice,wifi_press_row=-1;
static int wifi_native_focus_shared,wifi_native_ready;
static int wifi_native_released,wifi_native_start_failed;
static unsigned long wifi_native_release_tick;
static char wifi_page_url[96];
static int wifi_is_rvf(void){return wifi_entry==WIFI_ENTRY_RVF;}
static uint8_t wifi_join_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(11)];
static uint8_t wifi_page_qr[qrcodegen_BUFFER_LEN_FOR_VERSION(11)];
static void wifi_native_focus(int share){
 if(!share&&!wifi_native_focus_shared)return;
 wifi_native_focus_shared=share;
 if(share){
  /* Keep our progress panel visible. Synthetic original keys go through X
   * and do not need the physical shutter/button lease to be released. */
  Window focus;int revert;XGetInputFocus(dpy,&focus,&revert);
  if(focus==win&&previous>PointerRoot)XSetInputFocus(dpy,previous,previous_revert,CurrentTime);
 }else{
  XRaiseWindow(dpy,win);XSetInputFocus(dpy,win,RevertToPointerRoot,CurrentTime);
 }
 XSync(dpy,False);
}
static void wifi_release_to_camera(void){
 if(wifi_native_released)return;
 wifi_native_released=1;wifi_native_release_tick=ticks();
 wifi_native_focus(1);
 XUnmapWindow(dpy,win);XSync(dpy,False);
 menu_modal_input_release();
}
static void wifi_start(int command){
 if(wifi_child){strcpy(notice,"處理中，請稍候");return;}
 int p[2];if(pipe(p)){strcpy(notice,"無法啟動連線服務");return;}
 pid_t child=fork();if(!child){close(p[0]);dup2(p[1],1);dup2(p[1],2);close(p[1]);menu_input_child_close();if(menu_lock_fd>=0)close(menu_lock_fd);
  char script[1200],window[24];const char*base=getenv("NX_LUT_ROOT");snprintf(script,sizeof script,"%s/wifi.sh",base?base:"");
  if(wifi_is_rvf()){
   snprintf(window,sizeof window,"%lu",(unsigned long)previous);
   execl("/bin/sh","sh",script,command==4?wifi_entries[wifi_entry].start:command==3?"rvf-stop":"rvf-status",window,(char*)0);
  }else{
   execl("/bin/sh","sh",script,command==4?wifi_entries[wifi_entry].start:command==3?"stop":"status",(char*)0);
  }
  _exit(127);
 }
 close(p[1]);if(child<0){close(p[0]);strcpy(notice,"無法啟動連線服務");return;}
 wifi_fd=p[0];fcntl(wifi_fd,F_SETFD,FD_CLOEXEC);int flags=fcntl(wifi_fd,F_GETFL);if(flags<0||fcntl(wifi_fd,F_SETFL,flags|O_NONBLOCK)<0){kill(child,SIGTERM);close(wifi_fd);wifi_fd=-1;strcpy(notice,"無法讀取連線狀態");return;}
 wifi_child=child;wifi_command=command;wifi_started=ticks();wifi_exited=wifi_exit=0;wifi_used=0;wifi_output[0]=0;
 if(command)strcpy(notice,command==4?(wifi_is_rvf()?"正在啟動原廠圖傳，請稍候…":"正在建立相機熱點，請稍候…"):"正在停止圖傳與相機熱點…");
}
static int wifi_escape(const char*in,char*out,size_t cap){
 size_t n=0;for(;*in;in++){if(strchr("\\;,:\"",*in)){if(n+1>=cap)return 0;out[n++]='\\';}if(n+1>=cap)return 0;out[n++]=*in;}out[n]=0;return 1;
}
static int wifi_qr_prepare(void){
 wifi_qr_valid=0;wifi_page_url[0]=0;
 if(wifi_entry==WIFI_NO_ENTRY||!wifi_status.valid||!wifi_status.running||!wifi_status.connected||!wifi_status.owned||strcmp(wifi_status.mode,"hotspot"))return 0;
 char ssid[66],password[128],join[240];
 if(!wifi_status.ssid[0]||!wifi_escape(wifi_status.ssid,ssid,sizeof ssid))return 0;
 if(wifi_status.security==0){
  if(wifi_status.password[0])return 0;
  snprintf(join,sizeof join,"WIFI:T:nopass;S:%s;;",ssid);
 }else if(wifi_status.security==1){
  if(strlen(wifi_status.password)<8||!wifi_escape(wifi_status.password,password,sizeof password))return 0;
  snprintf(join,sizeof join,"WIFI:T:WPA;S:%s;P:%s;;",ssid,password);
 }else return 0;
 int length=snprintf(wifi_page_url,sizeof wifi_page_url,"%s%s",wifi_status.url,wifi_entries[wifi_entry].page);
 if(length<0||(size_t)length>=sizeof wifi_page_url)return 0;
 uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(11)];
 wifi_qr_valid=qrcodegen_encodeText(join,scratch,wifi_join_qr,qrcodegen_Ecc_MEDIUM,1,11,qrcodegen_Mask_0,false)
  &&qrcodegen_encodeText(wifi_page_url,scratch,wifi_page_qr,qrcodegen_Ecc_MEDIUM,1,11,qrcodegen_Mask_0,false);
 return wifi_qr_valid;
}
static void wifi_stop_start(void){
 wifi_start(3);
 if(!wifi_child){
  if(wifi_is_rvf()){wifi_exit_unconfirmed=1;action=0;quitting=1;}
  else{wifi_close_requested=0;wifi_close_failed=1;strcpy(notice,"停止未確認，再按 Wi-Fi 關閉畫面");}
 }
}
static void wifi_close(void){
 if(wifi_entry==WIFI_NO_ENTRY||wifi_close_failed){
  wifi_exit_unconfirmed=wifi_close_failed;action=0;quitting=1;return;
 }
 if(wifi_close_requested)return;
 wifi_close_requested=1;wifi_qr_valid=0;strcpy(notice,"正在停止圖傳與相機熱點…");
 if(wifi_is_rvf())wifi_release_to_camera();
 if(!wifi_child)wifi_stop_start();
}
static int wifi_open(void){
 if(detail_visible||lut_picker_open||video_picker_open||video_child||rearm_child){strcpy(notice,"請先完成目前設定，再開啟 Wi-Fi");return 0;}
 if(!menu_modal_input_take())return 0;
 wifi_visible=1;wifi_close_requested=wifi_close_failed=wifi_exit_unconfirmed=wifi_qr_valid=wifi_flow_ready=0;
 wifi_native_ready=wifi_native_focus_shared=0;
 wifi_native_released=wifi_native_start_failed=0;wifi_native_release_tick=0;
 wifi_choice=WIFI_ENTRY_ALBUM_5G;wifi_entry=WIFI_NO_ENTRY;wifi_press_row=-1;notice[0]=0;
 memset(&wifi_status,0,sizeof wifi_status);
 return 1;
}
static void wifi_choose(void){
 if(wifi_entry!=WIFI_NO_ENTRY||wifi_close_requested||quitting)return;
 wifi_entry=wifi_choice;wifi_press_row=-1;
 if(wifi_is_rvf())wifi_native_focus(1);
 wifi_start(4);
 if(!wifi_child&&wifi_is_rvf())wifi_native_focus(0);
}
static int wifi_poll(void){
 if(!wifi_visible)return 0;
 if(wifi_native_released&&ticks()-wifi_native_release_tick>=132000UL){
  wifi_exit_unconfirmed=1;action=0;quitting=1;return 1;
 }
 if(!wifi_child){if(wifi_close_failed||!wifi_flow_ready)return 0;if(ticks()>=wifi_next_status){wifi_next_status=ticks()+500UL;wifi_start(0);}return 0;}
 if(wifi_fd>=0){ssize_t n;while(wifi_used<sizeof wifi_output-1&&(n=read(wifi_fd,wifi_output+wifi_used,sizeof wifi_output-1-wifi_used))>0)wifi_used+=(size_t)n;wifi_output[wifi_used]=0;}
 int status=0;pid_t p=waitpid(wifi_child,&status,WNOHANG);if(p==wifi_child){wifi_exited=1;wifi_exit=WIFEXITED(status)?WEXITSTATUS(status):128;}
 unsigned long budget=wifi_command==4?90000UL:wifi_command==3?
     (wifi_is_rvf()?130000UL:52000UL):5000UL;
 if(!wifi_exited&&ticks()-wifi_started>budget){kill(wifi_child,SIGTERM);wifi_exited=1;wifi_exit=124;}
 if(!wifi_exited)return 0;
 if(wifi_fd>=0){ssize_t n;while(wifi_used<sizeof wifi_output-1&&(n=read(wifi_fd,wifi_output+wifi_used,sizeof wifi_output-1-wifi_used))>0)wifi_used+=(size_t)n;wifi_output[wifi_used]=0;close(wifi_fd);wifi_fd=-1;}
 int command=wifi_command,result_valid=0;wifi_child=0;wifi_next_status=ticks()+(wifi_is_rvf()?850UL:2000UL);
 struct nxl_wifi_status parsed={0};if(nxl_wifi_status_parse(wifi_output,&parsed)){result_valid=1;wifi_status=parsed;
  if(wifi_is_rvf()&&!wifi_close_requested){
   wifi_qr_prepare();
   if(wifi_qr_valid&&!wifi_native_released){wifi_native_ready=wifi_flow_ready=1;wifi_native_focus(0);strcpy(notice,"先掃左碼連上 Wi-Fi，再掃右碼開啟網頁");}
   else if(!strcmp(wifi_status.reason,"RVF_RESTORE_UNCONFIRMED")){
    wifi_release_to_camera();wifi_exit_unconfirmed=1;action=0;quitting=1;
   }else if(!wifi_status.owned){
    wifi_native_start_failed=!wifi_native_ready;
    wifi_release_to_camera();action=0;quitting=1;
   }else{
    wifi_flow_ready=1;
    if(!strcmp(wifi_status.reason,"RVF_RESTORING"))wifi_release_to_camera();
   }
  }
  else if(command==4&&!wifi_close_requested){wifi_flow_ready=!wifi_exit&&wifi_qr_prepare();strcpy(notice,wifi_flow_ready?"先掃左碼連上 Wi-Fi，再掃右碼開啟網頁":"熱點尚未就緒，請按 Wi-Fi 停止");}
  else if(command==0&&wifi_flow_ready&&!wifi_close_requested){if(!wifi_qr_prepare()){wifi_flow_ready=0;strcpy(notice,"連線已中斷，請按 Wi-Fi 停止");}}
 }else{
  if(wifi_is_rvf()&&!wifi_close_requested&&!wifi_native_released)wifi_native_focus(0);
  wifi_qr_valid=0;wifi_flow_ready=wifi_native_released;strcpy(notice,strstr(wifi_output,"SRW_BUSY")?"顯影中，請稍後再連線":strstr(wifi_output,"USB_")?"請先安全斷開 USB":strstr(wifi_output,"CHECKSUM_TOOL_")?"圖傳校驗工具無法使用":strstr(wifi_output,"MISMATCH")?"圖傳檔案校驗失敗，請重新安裝":strstr(wifi_output,"HOTSPOT_ORIGINAL_NETWORK_BUSY")?"原廠無線功能使用中，請先關閉":"熱點未啟動，請按 Wi-Fi 停止");
 }
 if(wifi_close_requested){if(command==3){if(!wifi_exit&&result_valid&&!wifi_status.running&&!wifi_status.owned){action=0;quitting=1;}else if(wifi_is_rvf()){wifi_exit_unconfirmed=1;action=0;quitting=1;}else{wifi_close_requested=0;wifi_close_failed=1;strcpy(notice,"停止未確認，再按 Wi-Fi 關閉畫面");}}else wifi_stop_start();}
 return 1;
}
static void wifi_cleanup(void){if(wifi_child){kill(wifi_child,SIGTERM);waitpid(wifi_child,NULL,WNOHANG);wifi_child=0;}if(wifi_fd>=0){close(wifi_fd);wifi_fd=-1;}}
static void draw_wifi_qr(const uint8_t *code,int center){
 int n=qrcodegen_getSize(code),scale=284/(n+8);
 int size=(n+8)*scale,left=center-size/2,top=101+(284-size)/2;
 fill(left,top,size,size,0xffffff);
 for(int y=0;y<n;y++)for(int x=0;x<n;x++)if(qrcodegen_getModule(code,x,y))fill(left+(x+4)*scale,top+(y+4)*scale,scale,scale,0x000000);
}
enum { WIFI_ROW_LEFT=52, WIFI_ROW_WIDTH=616, WIFI_ROW_TOP=114, WIFI_ROW_STEP=96, WIFI_ROW_HEIGHT=80 };
static void draw_wifi(void){
 if(wifi_native_released)return;
 fill(0,0,720,480,0xfafaf8);fill(0,0,720,64,0x127b8c);XSetForeground(dpy,gc,0xffffff);
 text(24,18,wifi_entry==WIFI_NO_ENTRY?"無線連線":wifi_entries[wifi_entry].title);
 if(wifi_entry==WIFI_NO_ENTRY){
  for(int i=0;i<WIFI_ENTRY_COUNT;i++){
   int y=WIFI_ROW_TOP+i*WIFI_ROW_STEP;
   fill(WIFI_ROW_LEFT,y,WIFI_ROW_WIDTH,WIFI_ROW_HEIGHT,i==wifi_choice?0xcceff1:0xeeefed);
   if(i==wifi_choice)fill(WIFI_ROW_LEFT,y,6,WIFI_ROW_HEIGHT,0x127b8c);
   XSetForeground(dpy,gc,0x233036);text(WIFI_ROW_LEFT+25,y+26,wifi_entries[i].title);
  }
 }else if(wifi_qr_valid){
  XSetForeground(dpy,gc,0x233036);text(43,72,"加入相機 Wi-Fi");text(389,72,"在手機開啟網頁");
  draw_wifi_qr(wifi_join_qr,185);draw_wifi_qr(wifi_page_qr,535);
  XSetForeground(dpy,gc,0x087e91);
  char line[114];snprintf(line,sizeof line,"Wi-Fi  %s",wifi_status.ssid);limited_text(43,391,line,300);
  limited_text(389,391,wifi_page_url,306);
  if(wifi_status.security==1)limited_text(43,418,wifi_status.password,300);
 }
 XSetForeground(dpy,gc,0x34545d);
 const char*hint=notice[0]?notice:"OK 選擇   Wi-Fi 關閉";
 limited_text(24,447,hint,672);publish_ui();
}
static int wifi_key(unsigned code,int value){
 if(!wifi_visible)return 0;
 if(value==1)down[code]=1;
 if(wifi_entry==WIFI_NO_ENTRY&&(value==1||value==2)){
  int step=nxl_item_step(code);
  wifi_choice=(wifi_choice+step+WIFI_ENTRY_COUNT)%WIFI_ENTRY_COUNT;
 }
 if(value==0&&down[code]){down[code]=0;if(code==96)wifi_choose();}
 return 1;
}
static int wifi_pointer(XEvent*e){
 if(!wifi_visible||(e->type!=ButtonPress&&e->type!=ButtonRelease&&e->type!=MotionNotify))return 0;
 if(wifi_entry!=WIFI_NO_ENTRY)return 1;
 if(e->type==MotionNotify||e->xbutton.button!=1)return 1;
 int x=e->xbutton.x,y=e->xbutton.y,row=-1;
 if(x>=WIFI_ROW_LEFT&&x<WIFI_ROW_LEFT+WIFI_ROW_WIDTH){
  for(int i=0;i<WIFI_ENTRY_COUNT;i++){
   int top=WIFI_ROW_TOP+i*WIFI_ROW_STEP;
   if(y>=top&&y<top+WIFI_ROW_HEIGHT)row=i;
  }
 }
 if(e->type==ButtonPress){wifi_press_row=row;if(row>=0)wifi_choice=row;}
 else{if(row>=0&&row==wifi_press_row)wifi_choose();wifi_press_row=-1;}
 return 1;
}
