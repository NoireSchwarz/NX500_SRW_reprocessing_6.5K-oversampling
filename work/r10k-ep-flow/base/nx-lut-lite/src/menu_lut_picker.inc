/* Favorites-only modal. Selecting changes the SRW table, never live ISP. */
static int lut_picker_open,lut_picker_count,lut_picker_cursor,lut_picker_press=-1;
static int lut_picker_view[513];
static const char*lut_picker_id(int row){return row>0&&row<lut_picker_count?names[lut_picker_view[row]]:"off";}
static void lut_picker_begin(void){
 lut_picker_count=1;lut_picker_cursor=0;lut_picker_press=-1;
 for(int i=0;i<total_count&&lut_picker_count<513;i++)if(favorite_index(names[i])>=0){lut_picker_view[lut_picker_count]=i;if(!strcmp(srw_lut_id,names[i]))lut_picker_cursor=lut_picker_count;lut_picker_count++;}
 lut_picker_open=1;notice[0]=0;nxl_help_reset(&help,ticks());
}
static void lut_picker_move(int step){lut_picker_cursor=(lut_picker_cursor+step+lut_picker_count)%lut_picker_count;}
static void lut_picker_choose(void){
 char previous[81];strcpy(previous,srw_lut_id);strcpy(srw_lut_id,lut_picker_id(lut_picker_cursor));
 if(!browser_save()){strcpy(srw_lut_id,previous);strcpy(notice,"LUT SELECTION SAVE FAILED");return;}
 lut_picker_open=0;notice[0]=0;srw_row=2;nxl_help_reset(&help,ticks());
}
static int lut_picker_key(unsigned code,int value){
 if(!lut_picker_open)return 0;
 if(value==1)down[code]=1;
 if((value==1||value==2)&&nxl_item_step(code))lut_picker_move(nxl_item_step(code));
 if(value==0&&down[code]){down[code]=0;if(code==96)lut_picker_choose();else if(code==127){lut_picker_open=0;nxl_help_reset(&help,ticks());}else if(code==NXL_S1||code==126){lut_picker_open=0;action=0;quitting=1;}}
 return 1;
}
static int lut_picker_pointer(XEvent*e){
 if(!lut_picker_open||(e->type!=ButtonPress&&e->type!=ButtonRelease))return 0;
 int x=e->xbutton.x,y=e->xbutton.y;
 if(e->type==ButtonPress){pointer_down=1;lut_picker_press=-1;
  if(e->xbutton.button==4||e->xbutton.button==5)lut_picker_move(e->xbutton.button==4?-1:1);
  else if(e->xbutton.button==1&&x>=116&&x<680&&y>=155&&y<355){int row=lut_picker_cursor/5*5+(y-155)/40;if(row<lut_picker_count)lut_picker_press=row;}
  else if(e->xbutton.button==1&&x>=594&&x<680&&y>=112&&y<150)lut_picker_press=-2;
 }else{pointer_down=0;if(e->xbutton.button==1){if(lut_picker_press>=0&&x>=116&&x<680&&y>=155&&y<355&&lut_picker_press==lut_picker_cursor/5*5+(y-155)/40){lut_picker_cursor=lut_picker_press;lut_picker_choose();}else if(lut_picker_press==-2&&x>=594&&x<680&&y>=112&&y<150)lut_picker_open=0;}lut_picker_press=-1;}
 return 1;
}
static void draw_lut_picker(void){
 if(!lut_picker_open)return;fill(104,106,592,294,panel);frame(104,106,592,294,muted);XSetForeground(dpy,gc,ink);text(120,116,"SELECT LUT / FAVORITES");text(604,116,"BACK");
 int first=lut_picker_cursor/5*5;for(int i=0;i<5&&first+i<lut_picker_count;i++){int row=first+i,y=155+i*40;
  if(row==lut_picker_cursor)fill(116,y,564,38,selection_colors[3]);XSetForeground(dpy,gc,ink);
  const char*id=lut_picker_id(row);const struct nxl_look_info*info=info_for(id);const char*label=row?(!strcmp(info->id,id)?info->name:id):"OFF / ORIGINAL";
  limited_text(126,y+5,label,540);
 }
 char page[48];snprintf(page,sizeof page,"%d / %d",lut_picker_cursor+1,lut_picker_count);XSetForeground(dpy,gc,muted);text(122,366,"OK SELECT / MENU BACK");limited_text(566,366,page,116);
 if(lut_picker_count==1){XSetForeground(dpy,gc,muted);text(128,208,"No favorites yet");}
}
