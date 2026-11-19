static void release_confirm(void){
 if(sidebar_focus){sidebar_focus=0;return;}
 if(left_panel==2)video_confirm();
 else if(left_panel==1)srw_confirm();
 else if(video_child>0&&video_command)strcpy(notice,"影片設定更新中，請稍候");
 else if(rearm_child>0)strcpy(notice,"工作階段更新中，請稍候");
 else if(!detail_open()){action=confirm_action();quitting=1;}
}
static void key(unsigned code,int value){
 if(code>=sizeof down||value<0||value>2)return;
 if(detail_key(code,value))return;
 if(code==83&&value==1){action=0;quitting=1;return;}
 if(lut_picker_key(code,value))return;
 if(value==1)down[code]=1;
 if(down[NXL_EV]&&(down[NXL_AEL]||down[NXL_WIFI]))combo_block=1;
 if(value==1||value==2){
  int step=nxl_item_step(code);
  if(code==75){sidebar_focus=1;notice[0]=0;}
  else if(code==77){sidebar_focus=0;notice[0]=0;}
  else if(step&&sidebar_focus){switch_panel(left_panel+step);sidebar_focus=1;}
  else if(step&&left_panel==2)video_navigate(release_video_index()+step);
  else if(step&&left_panel==1)srw_navigate((srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row))+step);
  else if(step)navigate(selected+step);
  else if(!left_panel&&(code==177||code==178)){browser_group(category+(code==177?1:-1));notice[0]=0;nxl_help_reset(&help,ticks());}
 }
 if(value!=0||!down[code])return;
 down[code]=0;int blocked=combo_block;
 if(!down[NXL_EV]&&!down[NXL_AEL]&&!down[NXL_WIFI])combo_block=0;
 if(!blocked&&code==NXL_EV&&!left_panel&&!sidebar_focus)ui_control(1);
 if(!blocked&&code==NXL_WIFI&&!left_panel&&!sidebar_focus)ui_control(2);
 if(code==96)release_confirm();
 if(code==127){if(help.visible)nxl_help_close(&help);else if(left_panel==1&&srw_file_mode){srw_file_mode=0;nxl_help_reset(&help,ticks());}else quitting=1;}
 if(code==NXL_S1||code==126)quitting=1;
}
