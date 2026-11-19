static void srw_confirm(void){
 if(video_child>0&&video_command){strcpy(notice,"影片設定更新中，請稍候");return;}
 if(srw_file_mode){
  if(srw_count&&srw_container_check(srw_files[srw_cursor])){strcpy(srw_source,srw_files[srw_cursor]);strcpy(srw_lut_id,"off");srw_file_mode=0;srw_row=0;strcpy(notice,"已選擇原檔，LUT已關閉");}
  else strcpy(notice,"原檔不可用，請重新選擇");return;
 }
 if(nxl_srw_ui_index(srw_row)<0){srw_row=3;return;}
 if(rearm_child>0){strcpy(notice,"工作階段更新中，請稍候");return;}
 if(srw_row==0){srw_scan();srw_file_mode=1;return;}
 if(srw_row==2){lut_picker_begin();return;}
 if(srw_row==3){load_raw_status();strcpy(notice,nxl_raw_status_hint(&raw_status));return;}
 if(srw_row==5){start_menu_new_session();return;}
 if(srw_row==7){if(raw_status.state==NXL_RAW_FRONT_RUNNING||raw_status.state==NXL_RAW_BACKEND_RUNNING)strcpy(notice,"重顯影執行中，請稍候");else{action=10;quitting=1;}return;}
 if(srw_row==4){
  if(access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0){strcpy(notice,"請先還原V‑Log，再執行SRW重顯影");return;}
  if(!srw_container_check(srw_source)){strcpy(notice,"請先選擇有效的SRW原檔");return;}
  load_raw_status();
  if(!raw_status.valid){strcpy(notice,"無法讀取工作階段狀態");return;}
  if(raw_status.state==NXL_RAW_FRONT_RUNNING||raw_status.state==NXL_RAW_BACKEND_RUNNING){strcpy(notice,"重顯影執行中，請稍候");return;}
  if(video_enabled){strcpy(notice,"請先關閉影片LUT");return;}
  srw_gamma_mode=1;action=12;quitting=1;
 }
}
static void draw_srw(void){
 help.visible=0;help.suppressed=1;fill(0,0,720,480,bg);XSetForeground(dpy,gc,ink);text(24,20,"SRW 重顯影");text(552,20,"MENU 返回");
 XSetForeground(dpy,gc,muted);text(24,60,"選擇原檔與LUT，再初始化並重顯影");frame(18,96,684,298,line_color);draw_sidebar();
 int cursor=srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row),total=srw_file_mode?srw_count:nxl_srw_ui_count();
 if(cursor<0){srw_row=3;cursor=nxl_srw_ui_index(srw_row);}int first=cursor/5*5;
 for(int row=0;row<5&&first+row<total;row++){
  int index=first+row,y=104+row*56;
  if(index==cursor)for(int k=0;k<8;k++)fill(96,y+k*7,588,7,sidebar_focus?button_bg:selection_colors[k]);else fill(108,y+55,568,1,line_color);
  XSetForeground(dpy,gc,ink);
  if(srw_file_mode){limited_text(122,y+14,srw_basename(srw_files[index]),550);continue;}
  int id=nxl_srw_ui_at(index);const char*title=id==0?"選擇SRW原檔":id==2?"重顯影LUT":id==3?"工作階段狀態":id==4?"初始化並重顯影":id==5?"新工作階段":"恢復相機";
  const char*value=id==0?(srw_source[0]?srw_basename(srw_source):"尚未選擇"):id==2?(!strcmp(srw_lut_id,"off")?"關閉／原色":info_for(srw_lut_id)->name):id==3?nxl_raw_status_state_name(&raw_status):id==4?"OK 開始":id==5?"只準備，不自動執行":"還原原廠取景";
  limited_text(122,y+14,title,260);limited_text(390,y+14,value,285);
 }
 XSetForeground(dpy,gc,muted);text(24,400,"上下／下撥輪 選擇");char page[32];snprintf(page,sizeof page,"%d / %d",total?cursor+1:0,total);text(620,400,page);
 if(notice[0]){fill(20,395,680,36,bg);XSetForeground(dpy,gc,accent);limited_text(24,400,notice,650);}
 XSetForeground(dpy,gc,ink);text(24,442,"左右 切換功能／內容");text(320,442,"OK 選擇");text(540,442,"MENU 返回");publish_ui();
}
