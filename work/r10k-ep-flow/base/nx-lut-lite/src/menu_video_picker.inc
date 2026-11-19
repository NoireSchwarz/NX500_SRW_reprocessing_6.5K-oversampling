/* The three production output routes share the SRW picker geometry. */
static int video_picker_cursor,video_picker_press=-1;
static const int video_picker_commands[]={3,1,2};
static const char*video_picker_titles[]={"全寬UHD 24p","UHD轉2.5K","恢復原廠UHD"};
static int video_picker_count(void){return 3;}
static void video_picker_move(int step){video_picker_cursor=(video_picker_cursor+step+3)%3;}
static void video_picker_begin(void){
 if(video_child>0||rearm_child>0){strcpy(notice,"設定更新中，請稍候");return;}
 if(!menu_modal_input_take())return;
 video_picker_cursor=video_status.mode==3||video_status.mode==4?0:video_status.mode==2?1:2;
 video_picker_press=-1;video_picker_open=1;pointer_down=0;pointer_cell=-1;notice[0]=0;memset(down,0,sizeof down);nxl_help_close(&help);
}
static void video_picker_choose(void){
 int command=video_picker_commands[video_picker_cursor];video_picker_open=0;video_picker_press=-1;video_row=9;video_menu_start(command);
}
static int video_picker_key(unsigned code,int value){
 if(!video_picker_open)return 0;
 if(value==1)down[code]=1;
 if((value==1||value==2)&&nxl_item_step(code))video_picker_move(nxl_item_step(code));
 if(value==0&&down[code]){down[code]=0;if(code==96)video_picker_choose();else if(code==127){video_picker_open=0;video_picker_press=-1;}else if(code==NXL_S1||code==126||code==83){video_picker_open=0;action=0;quitting=1;}}
 return 1;
}
static int video_picker_pointer(XEvent*e){
 if(!video_picker_open||(e->type!=ButtonPress&&e->type!=ButtonRelease))return 0;
 int x=e->xbutton.x,y=e->xbutton.y;
 if(e->type==ButtonPress){pointer_down=1;video_picker_press=-1;
  if(e->xbutton.button==4||e->xbutton.button==5)video_picker_move(e->xbutton.button==4?-1:1);
  else if(e->xbutton.button==1&&x>=116&&x<680&&y>=155&&y<275)video_picker_press=(y-155)/40;
  else if(e->xbutton.button==1&&x>=594&&x<680&&y>=112&&y<150)video_picker_press=-2;
 }else{pointer_down=0;if(e->xbutton.button==1){
  if(video_picker_press>=0&&x>=116&&x<680&&y>=155&&y<275&&video_picker_press==(y-155)/40){video_picker_cursor=video_picker_press;video_picker_choose();}
  else if(video_picker_press==-2&&x>=594&&x<680&&y>=112&&y<150)video_picker_open=0;
 }video_picker_press=-1;}return 1;
}
static void draw_video_picker(void){
 if(!video_picker_open)return;fill(104,106,592,294,panel);frame(104,106,592,294,muted);XSetForeground(dpy,gc,ink);text(120,116,"選擇畫面模式");text(604,116,"返回");
 for(int i=0;i<3;i++){int y=155+i*40;if(i==video_picker_cursor)fill(116,y,564,38,selection_colors[3]);XSetForeground(dpy,gc,ink);limited_text(126,y+5,video_picker_titles[i],540);}
 XSetForeground(dpy,gc,muted);text(122,320,"停錄並回照片待機後切換");text(122,366,"OK 套用／MENU 返回");
}
