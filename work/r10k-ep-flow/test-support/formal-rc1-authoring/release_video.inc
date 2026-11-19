static const unsigned char release_video_rows[]={0,1,2,3,4,6,8};
static int release_video_count(void){return sizeof release_video_rows/sizeof*release_video_rows;}
static int release_video_index(void){for(int i=0;i<release_video_count();i++)if(video_row==release_video_rows[i])return i;return 0;}
static void video_navigate(int value){int n=release_video_count();video_row=release_video_rows[(value%n+n)%n];notice[0]=0;}
static void video_confirm(void){
 if(video_row==8){detail_open_log();return;}
 if(video_row==6){video_menu_start(nxl_gamma_next_command(&gamma_status,gamma_command_failed));return;}
 if(video_row<5)video_menu_start(video_row==1?3:video_row==2?4:video_row==3?1:video_row==4?2:0);
}
static void draw_video(void){
 help.visible=0;help.suppressed=1;fill(0,0,720,480,bg);XSetForeground(dpy,gc,ink);text(24,20,"影片設定");text(552,20,"MENU 返回");
 XSetForeground(dpy,gc,muted);text(24,60,"NX LUT Lite 0.4.0 RC1");frame(18,96,684,298,line_color);draw_sidebar();
 int cursor=release_video_index(),first=cursor/5*5;
 for(int row=0;row<5&&first+row<release_video_count();row++){
  int index=first+row,id=release_video_rows[index],y=104+row*56;
  if(index==cursor)for(int k=0;k<8;k++)fill(96,y+k*7,588,7,sidebar_focus?button_bg:selection_colors[k]);else fill(108,y+55,568,1,line_color);
  const char*title=id==0?"輸出狀態":id==1?"全寬UHD 24p":id==2?"原生裁切UHD 30p":id==3?"UHD轉2.5K":id==4?"還原原生UHD":id==6?"V‑Log開關":"Log LUT";
  const char*value=id==0?(!video_status.valid?"讀取狀態":video_status.mode==3||video_status.mode==4?"全寬":video_status.mode==2?"2560×1440":video_status.mode==1?"原生UHD":"其他輸出"):id==6?(gamma_command_failed||gamma_status.phase==5?"失敗／還原":gamma_status.phase==1||gamma_status.phase==2?"等待原廠更新":access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0?"開啟／還原":"關閉／啟用"):id==8?"進入Log專用選擇器":"OK 套用";
  XSetForeground(dpy,gc,ink);limited_text(122,y+14,title,260);limited_text(390,y+14,value,285);
 }
 XSetForeground(dpy,gc,muted);limited_text(24,400,notice[0]?notice:"切換前停止錄影並回到照片待機",650);
 XSetForeground(dpy,gc,ink);text(24,442,"左右 切換功能／內容");text(320,442,"OK 選擇");text(540,442,"MENU 返回");publish_ui();
}
