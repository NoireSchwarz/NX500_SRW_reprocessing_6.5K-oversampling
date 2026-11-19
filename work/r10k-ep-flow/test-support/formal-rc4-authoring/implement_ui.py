from context import *
import shutil
U=T/'base/nx-lut-lite/src'
shutil.copyfile(A/'ev_triple.h',U/'ev_triple.h')
p=U/'menu_lut_detail_v2.inc'
patch(p,'if(!strcmp(id,"film_cinestill"))','if(!strcmp(id,"off"))return detail_log_scope?"純Log":"原廠";\n if(!strcmp(id,"film_cinestill"))')
patch(p,'return names[detail_view[ld_wrap(i,detail.count)]];','int n=detail_view[ld_wrap(i,detail.count)];return n<0?"off":names[n];')
patch(p,'const char*id=detail_name(detail.cursor);if(!name_ok(id))return 0;','const char*id=detail_name(detail.cursor);if(!strcmp(id,"off")||!name_ok(id))return 0;')
patch(p,'if(!detail_load_parameters()){detail_message[0]=0;detail_queue(0);}\n else detail_dirty=0;','if(!detail_load_parameters())strcpy(detail_message,"尚未套用，按OK裝載");\n detail_dirty=0;if(!strcmp(detail_name(detail.cursor),"off")){memset(detail.a,0,sizeof detail.a);detail.prepared=0;}')
patch(p,'if(!detail_log_scope)selected=detail.cursor+1;','if(!detail_log_scope)selected=detail.cursor;',2)
patch(p,'if(!selected_id()[0]||rearm_child>0)return 0;','if(selected>count||rearm_child>0)return 0;')
patch(p,'detail_log_scope=0;if(video_enabled){strcpy(notice,"先關閉視頻 LUT 再調整");return 1;}','detail_log_scope=0;')
patch(p,'if(count<=0||count>512)return 1;if(!menu_modal_input_take())return 1;memcpy(detail_view,view,(size_t)count*sizeof *view);ld_begin(&detail,count,selected-1,effective_strength(),category);','if(count<0||count>=512)return 1;if(!menu_modal_input_take())return 1;detail_view[0]=-1;memcpy(detail_view+1,view,(size_t)count*sizeof *view);ld_begin(&detail,count+1,selected,selected?effective_strength():0,category);')
patch(p,'int n=0,at=0;for(int i=0;i<total_count;i++)','detail_view[0]=-1;int n=1,at=0;for(int i=0;i<total_count&&n<512;i++)')
patch(p,'ld_begin(&detail,n,at,100,0);','ld_begin(&detail,n,at,at?100:0,0);')
patch(p,'if(!detail.prepared)detail_queue(0);else strcpy(detail_message,"混合已準備，按 OK 應用");','detail_dirty=0;strcpy(detail_message,detail.prepared?"混合已準備，按OK裝載":"尚未套用，按OK裝載");')
patch(p,'if(detail_log_scope&&code==173&&value)','if((detail_log_scope||!strcmp(detail_name(detail.cursor),"off"))&&code==173&&value)')
patch(p,'int old_cursor=detail.cursor;','if(!strcmp(detail_name(detail.cursor),"off")&&(code==177||code==178))return 1;\n int old_cursor=detail.cursor;')
patch(p,'if(e->type==MotionNotify&&detail_drag)','if(e->type==MotionNotify&&detail_drag&&!strcmp(detail_name(detail.cursor),"off"))return 1;\n if(e->type==MotionNotify&&detail_drag)')
patch(p,'else if(y>=116&&y<154&&x>=232&&x<=690){','else if(y>=116&&y<154&&x>=232&&x<=690&&strcmp(detail_name(detail.cursor),"off")){')
patch(p,'if(detail_log_scope)strcpy(detail_message,"Log使用單一專用LUT");','if(detail_log_scope||!strcmp(detail_name(detail.cursor),"off"))strcpy(detail_message,"此項不使用混合LUT");')
patch(p,' pid_t c=fork();if(!c){',' if(!strcmp(detail_a,"off"))snprintf(detail_expected,sizeof detail_expected,"NX_LUT_BASELINE_APPLIED3 mode=%s\\n",detail_log_scope?"VLOG":"STD");\n pid_t c=fork();if(!c){')
patch(p,'  execl("/bin/sh","sh",engine,detail_log_scope?', '  if(!strcmp(detail_a,"off")){execl("/bin/sh","sh",engine,"off",(char*)0);_exit(127);}\n  execl("/bin/sh","sh",engine,detail_log_scope?')
patch(p,'strcpy(active_name,detail_a);active_strength=detail_pa[0];strcpy(detail_message,detail.prepared&&!detail_running_mix?"混合已準備，按 OK 應用":"已應用，半按快門重載");','refresh_lut_runtime();strcpy(detail_message,"已裝載，半按快門返回取景");')

# Applied receipt, not an unconfirmed same-boot draft, drives triple-EV entry.
s='''
static int detail_applied_parameters(void){
 char line[400],a[81],b[81],extra;int pa[4],pb[4];
 int fd=open("/tmp/nx-lut-lite/session/detail-parameters",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 if(srw_fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>=400){close(fd);return 0;}
 ssize_t n=read(fd,line,sizeof line-1);close(fd);if(n!=st.st_size)return 0;line[n]=0;
 if(sscanf(line,"NX_LUT_DETAIL_APPLIED2 A=%80s %d %d %d %d B=%80s %d %d %d %d\\n%c",a,pa,pa+1,pa+2,pa+3,b,pb,pb+1,pb+2,pb+3,&extra)!=10||strcmp(a,detail_name(detail.cursor))||!detail_params_valid(pa)||!detail_params_valid(pb))return 0;
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
'''
with p.open('a',encoding='utf8',newline='\n')as f:f.write(s)
p=U/'menu.c'
patch(p,'static void key(unsigned code,int value){','static void key_direct(unsigned code,int value){')
insert='''#include "ev_triple.h"
static struct nxl_ev_taps menu_ev_taps;
static void menu_ev_single(unsigned n){while(n--){key_direct(NXL_EV,1);key_direct(NXL_EV,0);}}
static int menu_ev_tick(void){unsigned n=nxl_ev_expire(&menu_ev_taps,ticks());if(n){menu_ev_single(n);return 1;}return 0;}
static void key(unsigned code,int value){
 if(code>=sizeof down||value<0||value>2)return;
 unsigned long now=ticks();
 if(code==NXL_EV){
  if(value==1)down[code]=1;
  if(down[NXL_AEL]||down[NXL_WIFI]||combo_block){memset(&menu_ev_taps,0,sizeof menu_ev_taps);if(!value)down[code]=0;return;}
  if(value==0)down[code]=0;
  if(nxl_ev_feed(&menu_ev_taps,code,value,now))detail_open_current();return;
 }
 if(value==1){unsigned n=menu_ev_taps.count;menu_ev_taps.count=0;menu_ev_single(n);}
 nxl_ev_feed(&menu_ev_taps,code,value,now);key_direct(code,value);
}
'''
patch(p,'static void restore_and_close(void) {',insert+'\nstatic void restore_and_close(void) {')
patch(p,'    int video_shortcut=argc>1','    int quick_lut=argc>1&&!strcmp(argv[1],"--quick-lut");\n    int video_shortcut=argc>1')
patch(p,'fds[0].fd=ConnectionNumber(dpy); nxl_help_reset(&help,ticks()); draw();','fds[0].fd=ConnectionNumber(dpy); nxl_help_reset(&help,ticks());if(quick_lut)detail_open_current();draw();')
patch(p,'        if(detail_poll())draw();','        if(menu_ev_tick())draw();\n        if(detail_poll())draw();')
patch(p,' else if(!detail_open()){action=confirm_action();quitting=1;}',' else if(selected==count+1)video_menu_start(40);\n else if(!detail_open()){action=confirm_action();quitting=1;}')
patch(p,'if (e.type==1) { key(e.code,e.value);','if(e.type==0&&e.code==3){memset(&menu_ev_taps,0,sizeof menu_ev_taps);memset(down,0,sizeof down);combo_block=0;}\n                if (e.type==1) { key(e.code,e.value);')

p=U/'keys.c'
patch(p,'#include "common.h"','#include "common.h"\n#include "ev_triple.h"\n#include <sys/syscall.h>\n#include <time.h>\nstatic unsigned long key_ticks(void){struct timespec ts;if(syscall(263,CLOCK_MONOTONIC,&ts))return 0;return(unsigned long)ts.tv_sec*1000UL+(unsigned long)ts.tv_nsec/1000000UL;}\nstatic int menu_active(void){int f=nxl_lock("/tmp/nx-lut-lite/menu.lock");if(f<0)return 1;close(f);return 0;}')
patch(p,'struct nxl_shortcut shortcut = {0};','struct nxl_shortcut shortcut = {0};\n    struct nxl_ev_taps taps={0};')
patch(p,'if (e.type == 0 && e.code == 3) memset(&shortcut, 0, sizeof shortcut);','if (e.type == 0 && e.code == 3){memset(&shortcut,0,sizeof shortcut);memset(&taps,0,sizeof taps);}')
patch(p,'int command=nxl_activate(&shortcut, e.code, e.value);','''int command=nxl_activate(&shortcut,e.code,e.value);
                int quick=nxl_ev_feed(&taps,e.code,e.value,key_ticks());
                if(shortcut.chord.ael||shortcut.wifi||shortcut.pending||command)memset(&taps,0,sizeof taps);
                if(quick&&!command&&!menu_active())command=3;''')
patch(p,'command==NXL_TOGGLE_VIDEO?"--video-toggle":"--toggle"','command==3?"--quick-lut":command==NXL_TOGGLE_VIDEO?"--video-toggle":"--toggle"')
patch(p,'NX_LUT_KEYS_VERSION=0.2.1; KEYS_LOGS_FOLDER_R4','NX_LUT_KEYS_VERSION=0.4.0-RC4; EV_TRIPLE_CURRENT_LUT; CHORD_PRIORITY')

p=T/'base/nx-lut-lite/tools/build_zig.py'
patch(p,'p.add_argument("--menu-only", action="store_true")','p.add_argument("--menu-only", action="store_true")\np.add_argument("--keys-menu-only", action="store_true")')
patch(p,'(() if a.menu_only else ("cube2lut"','(() if (a.menu_only or a.keys_menu_only) else ("cube2lut"')
for p in U.iterdir():
 if p.suffix in ['.c','.h','.inc']and p.name!='ui_font.h':
  s=p.read_text('utf8');s=s.replace('0.4.0 RC3','0.4.0 RC4').replace('0.4.0-RC3','0.4.0-RC4');p.write_text(s,'utf8',newline='\n')
p=U/'menu_video.inc'
patch(p,'const char*mode=command==30?', 'const char*mode=command==40?"video-toggle":command==30?')
patch(p,' if(video_command>=30){',''' if(video_command==40){
  int ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status)&&strstr(video_output,"NXL_LUT_RELOAD_CONFIRMED1 EOF14 DMA7 ACCESS_CLEAR");
  refresh_lut_runtime();strcpy(notice,ok?"視頻LUT已裝載，半按快門返回":"裝載未確認，請查看運行記錄");return 1;
 }
 if(video_command>=30){''')
patch(p,'UHD24 - RESELECT NATIVE 4K24 / PRO TO LOAD','全寬UHD24規格已重新提交')
patch(p,'CROP 4K30P / 80M - RESELECT UHD30P / PRO','原生UHD30規格已重新提交')
patch(p,'2.5K SELECTED - RESELECT UHD ON CAMERA','2.5K規格已重新提交')
patch(p,'NATIVE UHD / PRO 70M RESTORED','原生UHD規格已重新提交')
patch(p,'純V‑Log已準備，原LUT已清除','V‑Log已裝載，原LUT已清除')
print('PASS manual OK, mapped reload, applied-state EV shortcut and video labels')
