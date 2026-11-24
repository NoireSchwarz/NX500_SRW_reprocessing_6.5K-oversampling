/* SPDX-License-Identifier: AGPL-3.0-only.
 * Existing menu -> A half-window -> EV B selector -> B parameters ->
 * prepare mixture -> A OK commits. Every MENU goes back exactly one level.
 */
#include "lut_detail_flow.h"
static struct ld_flow detail;
static int detail_log_scope;
static int detail_visible,detail_view[512],detail_b_view[512],detail_hidden,detail_return,detail_dirty,detail_queued_mix;
static int detail_fd=-1,detail_eof,detail_done,detail_status,detail_drag,detail_cell=-1,detail_overflow,detail_read_error,detail_running_mix;
static pid_t detail_child;static Window detail_dialog;
static unsigned long detail_due,detail_message_until;
static char detail_committed_b[81],detail_a[81],detail_b[81],detail_output[4096],detail_expected[400],detail_message[160];
static int detail_committed_params[4],detail_pa[4],detail_pb[4];static size_t detail_used;
static const char*detail_labels[]={"濃淡","色相","銳度","對比"};
static void detail_draw(void);

static const char*detail_short_name(const char*id){
 if(!strcmp(id,"off"))return detail_log_scope?"純Log":"原廠";
 if(!strcmp(id,"film_cinestill"))return "CineStill";
 if(!strcmp(id,"fuji_400h"))return "400H";
 if(!strcmp(id,"fuji_astia100f"))return "Astia";
 if(!strcmp(id,"fuji_classicchrome"))return "Classic";
 if(!strcmp(id,"fuji_eterna500"))return "Eterna";
 if(!strcmp(id,"fuji_velvia50"))return "Velvia";
 if(!strcmp(id,"kodak_2383"))return "2383";
 if(!strcmp(id,"kodak_ektar100"))return "Ektar";
 if(!strcmp(id,"mono_acros"))return "Acros";
 if(!strcmp(id,"pentax_satobi"))return "Satobi";
 if(!strcmp(id,"ricoh_retro"))return "Retro";
 if(!strcmp(id,"xh_5219_bright"))return "5219亮";
 if(!strcmp(id,"xh_5219_hasselblad"))return "5219哈蘇";

 if(!strcmp(id,"pan_backlight_n"))return "暖調逆光";
 if(!strcmp(id,"pan_smoky_s"))return "煙霧色調";
 if(!strcmp(id,"pan_dramatic_s"))return "大片標準";
 if(!strcmp(id,"vlog_pan_dramatic"))return "大片Log";

 if(!strcmp(id,"fuji_provia100f"))return "Provia";
 if(!strcmp(id,"kodak_portra400"))return "Portra";
 if(!strcmp(id,"mono_hp5"))return "HP5";
 if(!strcmp(id,"ejay_skin_native"))return "膚色";
 if(strstr(id,"5207_n_fx"))return "5207 FX";
 if(strstr(id,"5207_n"))return "5207 N";
 if(strstr(id,"5207_s"))return "5207 S";
 if(strstr(id,"g200_n_fx_l"))return "G200 冷";
 if(strstr(id,"g200_fx_n"))return "G200 暖";
 if(!strcmp(id,"vlog_xh_5219_bright"))return "5219亮";
 if(!strcmp(id,"vlog_xh_5219_hasselblad"))return "5219哈蘇";
 if(!strcmp(id,"vlog_v709"))return "V709 還原";
 const struct nxl_look_info*i=info_for(id);return i==&nxl_unknown_info?id:i->name;
}

static void detail_reset_inputs(void){memset(down,0,sizeof down);combo_block=pointer_down=0;pointer_cell=-1;detail_drag=0;detail_cell=-1;}
static const char*detail_name(int i){int n=detail_view[ld_wrap(i,detail.count)];return n<0?"off":names[n];}
static void detail_queue(int mixture){detail_dirty=1;detail_queued_mix=mixture&&detail.prepared;detail_due=ticks()+280UL;}
static void detail_dialog_close(void){if(detail_dialog){nxl_window_forget(detail_dialog);XDestroyWindow(dpy,detail_dialog);detail_dialog=0;canvas=win;if(!detail_hidden)XSetInputFocus(dpy,win,RevertToPointerRoot,CurrentTime);}}
static void detail_dialog_open(void){
 if(detail_dialog)return;XSetWindowAttributes a;memset(&a,0,sizeof a);a.override_redirect=False;a.background_pixel=bg;
 a.event_mask=ExposureMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|ButtonMotionMask|FocusChangeMask;
 detail_dialog=XCreateWindow(dpy,RootWindow(dpy,DefaultScreen(dpy)),80,248,560,224,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel|CWEventMask,&a);
 XStoreName(dpy,detail_dialog,"NX LUT B");nxl_window_prepare(dpy,detail_dialog,win,1);nxl_window_place(dpy,detail_dialog,80,248,560,224,ticks());if(!detail_hidden){XMapRaised(dpy,detail_dialog);XSetInputFocus(dpy,detail_dialog,RevertToPointerRoot,CurrentTime);}
}
static void detail_b_rebuild(void){
 detail.b_count=0;for(int pass=0;pass<(detail.b_category==0?2:1);pass++)for(int i=0;i<total_count;i++){
  int fav=favorite_index(names[i])>=0;const struct nxl_look_info*info=info_for(names[i]);
  if(!strcmp(info->group,"loglut"))continue;
  if(detail.b_category==0&&fav!=(pass==0))continue;if(detail.b_category==1&&!fav)continue;
  if(detail.b_category>=2&&strcmp(info->group,group_ids[detail.b_category]))continue;detail_b_view[detail.b_count++]=i;
 }
 detail.b_cursor=0;for(int i=0;i<detail.b_count;i++)if(!strcmp(names[detail_b_view[i]],detail_committed_b))detail.b_cursor=i;
}
static int detail_params_valid(const int*);
static int detail_cache_path(char path[256]){
 const char*id=detail_name(detail.cursor);if(!strcmp(id,"off")||!name_ok(id))return 0;
 const char*root="/tmp/nx-lut-lite/detail-edits-rc1";struct stat st;
 if(mkdir(root,0700)&&errno!=EEXIST)return 0;
 if(srw_lstat(root,&st)||!S_ISDIR(st.st_mode))return 0;
 return snprintf(path,256,"%s/%s_%s",root,detail_log_scope?"log":"std",id)<256;
}
static int detail_cache_save(void){
 char path[256],text[400];if(!detail_cache_path(path))return 0;
 const char*b=detail_committed_b[0]?detail_committed_b:detail_name(detail.cursor);
 int pb[4];memcpy(pb,detail.prepared?detail_committed_params:detail.b,sizeof pb);if(detail_log_scope)memset(pb,0,sizeof pb);
 int n=snprintf(text,sizeof text,"NXL_DETAIL_EDIT_RC1 A=%s %d %d %d %d B=%s %d %d %d %d prepared=%d\n",detail_name(detail.cursor),detail.a[0],detail.a[1],detail.a[2],detail.a[3],b,pb[0],pb[1],pb[2],pb[3],detail_log_scope?0:detail.prepared);
 if(n<=0||n>=(int)sizeof text)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;struct stat st;
 int ok=!srw_fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&!ftruncate(fd,0)&&write(fd,text,(size_t)n)==n&&!fsync(fd);if(close(fd))ok=0;return ok;
}
static int detail_load_parameters(void){
 char path[256],line[400],a[81],b[81],extra;int pa[4],pb[4],prepared;
 if(!detail_cache_path(path))return 0;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 if(srw_fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<=0||st.st_size>=400){close(fd);return 0;}
 ssize_t n=read(fd,line,sizeof line-1);close(fd);if(n!=st.st_size)return 0;line[n]=0;
 if(sscanf(line,"NXL_DETAIL_EDIT_RC1 A=%80s %d %d %d %d B=%80s %d %d %d %d prepared=%d\n%c",a,pa,pa+1,pa+2,pa+3,b,pb,pb+1,pb+2,pb+3,&prepared,&extra)!=11||strcmp(a,detail_name(detail.cursor))||!name_ok(b)||!detail_params_valid(pa)||!detail_params_valid(pb)||(prepared!=0&&prepared!=1))return 0;
 if(detail_log_scope&&(prepared||strcmp(a,b)||pb[0]||pb[1]||pb[2]||pb[3]))return 0;
 int found=0;for(int i=0;i<total_count;i++)if(!strcmp(names[i],b)&&((!strcmp(info_for(b)->group,"loglut"))==detail_log_scope))found=1;
 if(!found)return 0;
 memcpy(detail.a,pa,sizeof pa);memcpy(detail.b,pb,sizeof pb);memcpy(detail_committed_params,pb,sizeof pb);strcpy(detail_committed_b,b);detail.prepared=prepared;
 strcpy(detail_message,"已載入暫存參數，按OK套用");return 1;
}
static void detail_switch_a(int next){
 if(next==detail.cursor)return;
 (void)detail_cache_save();detail.cursor=ld_wrap(next,detail.count);
 memset(detail.a,0,sizeof detail.a);memset(detail.b,0,sizeof detail.b);detail.a[0]=detail_log_scope?100:effective_strength();detail.b[0]=100;
 detail.prepared=0;detail_committed_b[0]=0;
 if(!detail_load_parameters())strcpy(detail_message,"尚未套用，按OK裝載");
 detail_dirty=0;if(!strcmp(detail_name(detail.cursor),"off")){memset(detail.a,0,sizeof detail.a);detail.prepared=0;}
}

static void detail_close(int where){
 (void)detail_cache_save();
 detail_return=where;if(detail_child||detail_dirty)return;
 detail_dialog_close();detail_visible=0;if(!detail_log_scope)selected=detail.cursor;detail_reset_inputs();notice[0]=0;
 if(where==1){snprintf(notice,sizeof notice,"%s",detail_message);nxl_window_place(dpy,win,0,0,720,480,ticks());XMapRaised(dpy,win);XSetInputFocus(dpy,win,RevertToPointerRoot,CurrentTime);detail_hidden=0;}
 else{action=0;quitting=1;}
}
static int detail_params_valid(const int*p){return p[0]>=0&&p[0]<=100&&p[0]%5==0&&p[1]>=-10&&p[1]<=10&&p[2]>=-10&&p[2]<=10&&p[3]>=-10&&p[3]<=10;}

static int detail_open(void){
 if(selected>count||rearm_child>0)return 0;if(access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0){strcpy(notice,"請先還原V‑Log，再編輯普通LUT");return 1;}detail_log_scope=0;
 if(count<0||count>=512)return 1;if(!menu_modal_input_take())return 1;detail_view[0]=-1;memcpy(detail_view+1,view,(size_t)count*sizeof *view);ld_begin(&detail,count+1,selected,selected?effective_strength():0,category);
 detail_visible=1;detail_return=detail_dirty=detail_done=detail_eof=0;detail_committed_b[0]=detail_message[0]=0;detail_reset_inputs();
 nxl_window_place(dpy,win,0,240,720,240,ticks());nxl_help_close(&help);if(!detail_load_parameters())strcpy(detail_message,"調整參數或按OK套用");return 1;
}

static int detail_open_log(void){
 if(rearm_child>0||video_child>0){strcpy(notice,"設定更新中，請稍候");return 1;}
 if(access("/tmp/nx-lut-lite/clean-log.policy",F_OK)!=0){strcpy(notice,"請先啟用V‑Log");return 1;}
 detail_view[0]=-1;int n=1,at=0;for(int i=0;i<total_count&&n<512;i++)if(!strcmp(info_for(names[i])->group,"loglut")){if(!strcmp(names[i],active_name))at=n;detail_view[n++]=i;}
 if(!n){strcpy(notice,"尚無Log專用LUT");return 1;}
 if(!menu_modal_input_take())return 1;
 detail_log_scope=1;ld_begin(&detail,n,at,at?100:0,0);
 detail_visible=1;detail_return=detail_dirty=detail_done=detail_eof=0;detail_committed_b[0]=detail_message[0]=0;detail_reset_inputs();
 nxl_window_place(dpy,win,0,240,720,240,ticks());nxl_help_close(&help);if(!detail_load_parameters())strcpy(detail_message,"調整參數或按OK套用");return 1;
}

static void detail_result_action(int result){
 if(result==LD_OPEN_B){detail_b_rebuild();detail_dialog_open();}
 else if(result==LD_B_CATEGORY)detail_b_rebuild();
 else if(result==LD_A_CHANGED){if(!detail_log_scope)selected=detail.cursor;detail_dirty=0;strcpy(detail_message,detail.prepared?"混合已準備，按OK裝載":"尚未套用，按OK裝載");}
 else if(result==LD_EDIT_B){if(detail_committed_b[0]&&!strcmp(names[detail_b_view[detail.b_cursor]],detail_committed_b))memcpy(detail.b,detail_committed_params,sizeof detail.b);}
 else if(result==LD_ARM_B){strcpy(detail_committed_b,names[detail_b_view[detail.b_cursor]]);memcpy(detail_committed_params,detail.b,sizeof detail.b);detail_dialog_close();strcpy(detail_message,"混合已準備，按 OK 應用");}
 else if(result==LD_BACK){if(detail.level==LD_A)detail_dialog_close();}
 else if(result==LD_COMMIT){detail_queue(1);detail_due=0;}
 else if(result==LD_PARENT)detail_close(1);
 else if(result==LD_LIVE)detail_close(2);
}
static int detail_key(unsigned code,int value){
 if(!detail_visible)return 0;
 if(detail_child){if(value==1){if(code==127)detail_close(1);else if(code==125||code==126||code==83)detail_close(2);}return 1;}
 if((detail_log_scope||!strcmp(detail_name(detail.cursor),"off"))&&code==173&&value){strcpy(detail_message,"Log使用單一專用LUT；混合僅限普通模式");return 1;}
 if(!strcmp(detail_name(detail.cursor),"off")&&(code==177||code==178))return 1;
 int old_cursor=detail.cursor;
 if(value==1||(value==2&&code!=96&&code!=127&&code!=173&&code!=125&&code!=126&&code!=83)){if(value==1)down[code]=1;int result=ld_key(&detail,code);if(result==LD_A_CHANGED&&detail.cursor!=old_cursor){int next=detail.cursor;detail.cursor=old_cursor;detail_switch_a(next);}else detail_result_action(result);}
 else if(!value)down[code]=0;return 1;
}
static void detail_slider(int x,int left,int width,int*p,int parameter){int n=(x-left)*100/width;if(n<0)n=0;if(n>100)n=100;*p=parameter?(n+2)/5-10:((n+2)/5)*5;}
static int detail_pointer(XEvent*e){
 if(!detail_visible||detail_hidden)return 0;
 if(detail_child){if(e->type==ButtonRelease&&e->xbutton.button==1&&e->xany.window==win&&e->xbutton.y<38&&e->xbutton.x>=500)detail_close(1);return e->type==ButtonPress||e->type==ButtonRelease||e->type==MotionNotify;}
 int popup=detail_dialog&&(e->xany.window==detail_dialog);
 if(e->type==MotionNotify&&detail_drag&&!strcmp(detail_name(detail.cursor),"off"))return 1;
 if(e->type==MotionNotify&&detail_drag){if(detail.level==LD_A){detail_slider(e->xmotion.x,240,440,detail.a+detail.parameter,detail.parameter);detail_result_action(LD_A_CHANGED);}else if(popup&&detail.level==LD_B_PARAMS)detail_slider(e->xmotion.x,220,310,detail.b+detail.b_parameter,detail.b_parameter);return 1;}
 if(e->type!=ButtonPress&&e->type!=ButtonRelease)return 0;if(detail.level!=LD_A&&!popup)return 1;
 int x=e->xbutton.x,y=e->xbutton.y;
 if(e->type==ButtonPress){detail_cell=-1;if(e->xbutton.button==4||e->xbutton.button==5){detail_result_action(ld_key(&detail,e->xbutton.button==4?72:80));return 1;}if(e->xbutton.button!=1)return 1;
  if(detail.level==LD_A){
   if(y<38&&x>=500)detail_cell=1;else if(y<38&&x>=350)detail_cell=2;
   else if(y>=44&&y<94&&x>=20&&x<700){int first=detail.cursor>2?detail.cursor-2:0;if(first>detail.count-5)first=detail.count>5?detail.count-5:0;int index=first+(x-20)/136;if(index<detail.count)detail_cell=100+index;}
   else if(y>=116&&y<154&&x>=232&&x<=690&&strcmp(detail_name(detail.cursor),"off")){detail_drag=1;detail_slider(x,240,440,detail.a+detail.parameter,detail.parameter);detail_result_action(LD_A_CHANGED);}
   else if(y>=160&&y<198&&x>=20&&x<700)detail_cell=10+(x-20)/170;
  }else if(detail.level==LD_B_LIST){
   if(y>=34&&y<68&&x>=16&&x<544)detail_cell=20+(x-16)/66;
   else if(y>=72&&y<174&&x>=16&&x<544){int first=detail.b_cursor/3*3,index=first+(y-72)/34;if(index<detail.b_count)detail_cell=100+index;}
   else if(y>=182)detail_cell=x>=320?3:1;
  }else{
   if(y>=38&&y<174){detail.b_parameter=(y-38)/34;if(x>=220){detail_drag=1;detail_slider(x,220,310,detail.b+detail.b_parameter,detail.b_parameter);}}
   else if(y>=182)detail_cell=x>=320?3:1;
  }
 }else{
  detail_drag=0;if(e->xbutton.button!=1)return 1;int cell=detail_cell;detail_cell=-1;
  if(cell==1&&((detail.level==LD_A&&y<38&&x>=500)||(detail.level!=LD_A&&y>=182&&x<320)))detail_result_action(ld_key(&detail,127));
  else if(cell==2&&detail.level==LD_A&&y<38&&x>=350&&x<500){if(detail_log_scope||!strcmp(detail_name(detail.cursor),"off"))strcpy(detail_message,"此項不使用混合LUT");else detail_result_action(ld_key(&detail,173));}
  else if(cell==3&&detail.level!=LD_A&&y>=182&&x>=320)detail_result_action(ld_key(&detail,96));
  else if(cell>=10&&cell<14&&detail.level==LD_A&&y>=160&&y<198&&x>=20&&x<700&&cell==10+(x-20)/170)detail.parameter=cell-10;
  else if(cell>=20&&cell<28&&detail.level==LD_B_LIST&&y>=34&&y<68&&x>=16&&x<544&&cell==20+(x-16)/66){detail.b_category=cell-20;detail_b_rebuild();}
  else if(cell>=100){if(detail.level==LD_A&&y>=44&&y<94){detail_switch_a(cell-100);}else if(detail.level==LD_B_LIST&&y>=72&&y<174)detail.b_cursor=cell-100;}
 }return 1;
}
static void detail_start(void){
 int p[2];if(pipe(p)){strcpy(detail_message,"無法開始應用");detail_dirty=0;return;}
 strcpy(detail_a,detail_name(detail.cursor));memcpy(detail_pa,detail.a,sizeof detail_pa);
 strcpy(detail_b,detail_queued_mix?detail_committed_b:detail_a);memset(detail_pb,0,sizeof detail_pb);if(detail_queued_mix)memcpy(detail_pb,detail_committed_params,sizeof detail_pb);
 snprintf(detail_expected,sizeof detail_expected,"NX_LUT_DETAIL_APPLIED2 A=%s %d %d %d %d B=%s %d %d %d %d\n",detail_a,detail_pa[0],detail_pa[1],detail_pa[2],detail_pa[3],detail_b,detail_pb[0],detail_pb[1],detail_pb[2],detail_pb[3]);
 if(!strcmp(detail_a,"off"))snprintf(detail_expected,sizeof detail_expected,"NX_LUT_BASELINE_APPLIED3 mode=%s\n",detail_log_scope?"VLOG":"STD");
 pid_t c=fork();if(!c){task_child_watch_detach();menu_input_child_close();close(p[0]);dup2(p[1],1);dup2(p[1],2);close(p[1]);close(ConnectionNumber(dpy));char a[4][8],b[4][8];for(unsigned i=0;i<4;i++){snprintf(a[i],8,"%d",detail_pa[i]);snprintf(b[i],8,"%d",detail_pb[i]);}
  if(!strcmp(detail_a,"off")){execl("/bin/sh","sh",engine,"off",(char*)0);_exit(127);}
  execl("/bin/sh","sh",engine,detail_log_scope?"log-detail":"detail",detail_a,a[0],a[1],a[2],a[3],detail_b,b[0],b[1],b[2],b[3],(char*)0);_exit(127);
 }
 close(p[1]);if(c<0){close(p[0]);detail_dirty=0;strcpy(detail_message,"無法開始應用");return;}
 detail_child=c;detail_running_mix=detail_queued_mix;detail_fd=p[0];detail_eof=detail_overflow=detail_read_error=0;int flags=fcntl(detail_fd,F_GETFL);if(flags<0||fcntl(detail_fd,F_SETFL,flags|O_NONBLOCK)<0){close(detail_fd);detail_fd=-1;detail_eof=detail_read_error=1;}
 detail_dirty=0;detail_used=detail_done=0;detail_output[0]=0;strcpy(detail_message,"套用中，請稍候");detail_hidden=0;
}
static void detail_drain(void){if(detail_fd<0)return;for(unsigned pass=0;pass<16;pass++){char b[512];ssize_t n=read(detail_fd,b,sizeof b);if(n>0){size_t room=sizeof detail_output-1-detail_used,copy=(size_t)n<room?(size_t)n:room;if(copy<(size_t)n)detail_overflow=1;memcpy(detail_output+detail_used,b,copy);detail_used+=copy;detail_output[detail_used]=0;}else if(!n){close(detail_fd);detail_fd=-1;detail_eof=1;break;}else if(errno==EINTR)continue;else if(errno==EAGAIN)break;else{close(detail_fd);detail_fd=-1;detail_eof=detail_read_error=1;break;}}}
static int detail_poll(void){
 if(!detail_visible)return 0;if(detail_child){detail_drain();if(!detail_done){pid_t p=waitpid(detail_child,&detail_status,WNOHANG);if(p==detail_child)detail_done=1;}
  if(detail_done){detail_drain();if(detail_eof){char*r=strstr(detail_output,detail_expected);int ok=!detail_overflow&&!detail_read_error&&WIFEXITED(detail_status)&&!WEXITSTATUS(detail_status)&&r&&!strcmp(r,detail_expected);
   FILE*f=fopen("/tmp/nx-lut-lite/detail-action.log","w");if(f){fwrite(detail_output,1,detail_used,f);fclose(f);}
   int configured=!detail_overflow&&!detail_read_error&&strstr(detail_output,"NXL_LUT_CONFIGURED1\n")!=NULL;
   const char *reload=lut_reload_notice(configured,strstr(detail_output,"ERR LUT_RELOAD_RESTART_REQUIRED\n")!=NULL);
   if(ok||configured){(void)detail_cache_save();refresh_lut_runtime();}
   if(ok)strcpy(detail_message,"已裝載，半按快門返回取景");
   else{detail_dirty=0;strcpy(detail_message,reload?reload:"應用失敗，請檢查運行記錄");}
   detail_message_until=ticks()+1800UL;detail_child=0;if(detail_return&&!detail_dirty){detail_close(detail_return);return 1;}detail_hidden=0;return 1;
  }}
 }else if(detail_message[0]&&!detail.prepared&&detail_message_until&&ticks()>=detail_message_until){detail_message[0]=0;detail_message_until=0;return 1;}else if(detail_dirty&&(detail_return||ticks()>=detail_due)){detail_start();return 1;}else if(detail_return){detail_close(detail_return);return 1;}return 0;
}
static void detail_draw(void){
 if(detail_hidden)return;canvas=win;fill(0,0,720,240,bg);fill(0,0,720,3,accent);fill(0,38,720,1,line_color);
 char line[200];snprintf(line,sizeof line,"LUT / %s / A",detail_log_scope?"Log LUT":group_names[category]);XSetForeground(dpy,gc,ink);limited_text(20,5,line,320);XSetForeground(dpy,gc,muted);text(350,5,detail_log_scope?"Log專用":"EV 混合");text(510,5,"MENU 返回");
 int first=detail.cursor>2?detail.cursor-2:0;if(first>detail.count-5)first=detail.count>5?detail.count-5:0;
 for(int i=0;i<5;i++){int index=first+i,x=20+i*136;if(index>=detail.count)continue;fill(x,44,128,48,index==detail.cursor?panel:button_bg);frame(x,44,128,48,index==detail.cursor?accent:line_color);XSetForeground(dpy,gc,index==detail.cursor?ink:muted);const char*id=detail_name(index);limited_text(x+8,53,detail_short_name(id),114);}
 fill(20,103,680,4,button_bg);int width=680/(detail.count?detail.count:1);if(width<12)width=12;if(width>680)width=680;fill(20+(680-width)*detail.cursor/(detail.count>1?detail.count-1:1),101,width,8,accent);
 int value=detail.parameter?(detail.a[detail.parameter]+10)*5:detail.a[0];snprintf(line,sizeof line,"%s %s%d%s",detail_labels[detail.parameter],detail.parameter&&detail.a[detail.parameter]>0?"+":"",detail.a[detail.parameter],detail.parameter?"":"%");XSetForeground(dpy,gc,ink);text(20,119,line);fill(240,132,440,4,line_color);fill(240,132,value*440/100,4,accent);fill(236+value*440/100,124,8,20,ink);
 for(int i=0;i<4;i++){int x=20+i*170;fill(x,160,162,38,detail.parameter==i?panel:button_bg);XSetForeground(dpy,gc,detail.parameter==i?accent:ink);snprintf(line,sizeof line,"%s %d%s",detail_labels[i],detail.a[i],i?"":"%");text(x+10,164,line);}
 XSetForeground(dpy,gc,muted);limited_text(20,207,detail_message[0]?detail_message:info_for(detail_name(detail.cursor))->tone,680);publish_ui();
 if(detail.level!=LD_A&&detail_dialog){canvas=detail_dialog;fill(0,0,560,224,bg);frame(0,0,560,224,accent);XSetForeground(dpy,gc,ink);text(16,2,detail.level==LD_B_LIST?"選擇混合 B":"調整 B 參數");
  if(detail.level==LD_B_LIST){for(int i=0;i<8;i++){int x=16+i*66;fill(x,34,64,34,i==detail.b_category?panel:button_bg);XSetForeground(dpy,gc,i==detail.b_category?accent:ink);text(x+4,36,group_names[i]);}
   int start=detail.b_cursor/3*3;for(int i=0;i<3;i++){int n=start+i;if(n>=detail.b_count)break;fill(16,72+i*34,528,32,n==detail.b_cursor?panel:button_bg);XSetForeground(dpy,gc,n==detail.b_cursor?ink:muted);const char*id=names[detail_b_view[n]];const struct nxl_look_info*info=info_for(id);limited_text(26,74+i*34,info==&nxl_unknown_info?id:info->name,500);}
  }else for(int i=0;i<4;i++){int y=38+i*34;fill(16,y,528,32,i==detail.b_parameter?panel:bg);snprintf(line,sizeof line,"%s %d%s",detail_labels[i],detail.b[i],i?"":"%");XSetForeground(dpy,gc,i==detail.b_parameter?accent:ink);text(24,y,line);int v=i?(detail.b[i]+10)*5:detail.b[0];fill(220,y+16,310,3,line_color);fill(220,y+16,v*310/100,3,accent);}
  fill(16,182,264,36,button_bg);fill(288,182,256,36,panel);XSetForeground(dpy,gc,ink);text(34,185,"MENU 返回");XSetForeground(dpy,gc,accent);text(322,185,detail.level==LD_B_LIST?"OK 參數":"準備混合");canvas=win;
 }XFlush(dpy);
}

static int detail_applied_parameters(void){
 char line[400],a[81],b[81],extra;int pa[4],pb[4];
 int fd=open("/tmp/nx-lut-lite/session/detail-parameters",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 if(srw_fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>=400){close(fd);return 0;}
 ssize_t n=read(fd,line,sizeof line-1);close(fd);if(n!=st.st_size)return 0;line[n]=0;
 if(sscanf(line,"NX_LUT_DETAIL_APPLIED2 A=%80s %d %d %d %d B=%80s %d %d %d %d\n%c",a,pa,pa+1,pa+2,pa+3,b,pb,pb+1,pb+2,pb+3,&extra)!=10||strcmp(a,detail_name(detail.cursor))||!detail_params_valid(pa)||!detail_params_valid(pb))return 0;
 int found=0;for(int i=0;i<total_count;i++)if(!strcmp(names[i],b)&&((!strcmp(info_for(b)->group,"loglut"))==detail_log_scope))found=1;
 if(!found||(detail_log_scope&&(strcmp(a,b)||pb[0])))return 0;
 memcpy(detail.a,pa,sizeof pa);memcpy(detail.b,pb,sizeof pb);memcpy(detail_committed_params,pb,sizeof pb);strcpy(detail_committed_b,b);detail.prepared=!detail_log_scope&&pb[0]>0;return 1;
}
static int detail_open_current(void){
 if(detail_child||detail_dirty||video_child||rearm_child){strcpy(notice,"設定更新中，請稍候");return 1;}
 if(detail_visible){(void)detail_cache_save();detail_dialog_close();detail_visible=0;}
 refresh_lut_runtime();int log=access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0;
 lut_picker_open=video_picker_open=0;sidebar_focus=0;left_panel=log?2:0;
 if(log){if(!detail_open_log()||!detail_visible)return 1;}
 else{
  category=0;browser_rebuild(active_name[0]?active_name:"#off");
  if(!detail_open()||!detail_visible)return 1;
 }
 int at=0;for(int i=1;i<detail.count;i++)if(!strcmp(detail_name(i),active_name))at=i;
 detail.cursor=at;memset(detail.a,0,sizeof detail.a);memset(detail.b,0,sizeof detail.b);detail.b[0]=100;detail_committed_b[0]=0;detail.prepared=0;
 if(at){detail.a[0]=active_strength;detail_applied_parameters();}
 detail_dirty=0;detail.level=LD_A;detail_message_until=0;
 strcpy(detail_message,access("/tmp/nx-lut-lite/session/reload.pending",F_OK)==0?"上次裝載未確認，按OK重試":"目前設定；修改後按OK裝載");return 1;
}
