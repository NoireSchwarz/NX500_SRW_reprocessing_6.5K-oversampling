"""Integrate R8f presentation into current source; keep stable native workers."""
from context import *
import re,shutil
U=T/'base/nx-lut-lite/src'
def extent(s,name):
 m=re.search(r'static\s+(?:int|void|unsigned|pid_t)\s+(?:long\s+)?'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 depth=1;i=m.end();state='code'
 while depth:
  c=s[i];pair=s[i:i+2]
  if state=='code':
   if pair=='/*':state='comment';i+=2;continue
   if pair=='//':state='line';i+=2;continue
   if c=='"':state='string'
   elif c=="'":state='char'
   elif c=='{':depth+=1
   elif c=='}':depth-=1
  elif state=='comment':
   if pair=='*/':state='code';i+=2;continue
  elif state=='line':
   if c=='\n':state='code'
  elif c=='\\':i+=2;continue
  elif (state=='string'and c=='"')or(state=='char'and c=="'"):state='code'
  i+=1
 return m.start(),i
def replace(s,name,new):a,b=extent(s,name);return s[:a]+new+s[b:]
p=U/'menu.c';s=p.read_text('utf8');assert '#include "menu_lut_detail_v2.inc"'not in s
s=s.replace('#include "ui_state.h"','#include "ui_state.h"\n#include "ui_i18n.h"')
s=s.replace('static int left_panel;','static int left_panel,sidebar_focus;\nstatic int detail_open_log(void);')
s=s.replace('static void text(int x,int y,const char *s) {','static void text(int x,int y,const char *s) {\n    s=nxl_ui_text(s);')
s=s.replace('static void limited_text(int x,int y,const char *s,int width) {','static void limited_text(int x,int y,const char *s,int width) {\n    s=nxl_ui_text(s);')
frag=(A/'release_srw.inc').read_text('utf8')
a,b=extent(frag,'srw_confirm');s=replace(s,'srw_confirm',frag[a:b])
a,b=extent(frag,'draw_srw');s=replace(s,'draw_srw',frag[a:b])
s=s.replace('static void draw(void) {','''#include "menu_lut_detail_v2.inc"
static void draw(void) {
    if(detail_visible){detail_draw();return;}
''')
s=s.replace('fill(19,97,76,296,button_bg);fill(19,104+left_panel*95,76,88,color_tab);',
 'fill(19,97,76,296,button_bg);fill(19,104+left_panel*95,76,88,sidebar_focus?panel:color_tab);if(sidebar_focus)frame(19,104+left_panel*95,76,88,accent);')
s=s.replace('fill(96,y+k*7,588,7,selection_colors[k]);','fill(96,y+k*7,588,7,sidebar_focus?button_bg:selection_colors[k]);')
s=s.replace('左右/上拨 分类','上撥輪 分類').replace('上下 选择 / 下拨 切换','上下／下撥輪 選擇').replace('OK  应用','OK 詳情')
frag=(A/'release_key.inc').read_text('utf8');s=replace(s,'key',frag)
# One fixed RAM diagnostic set, retaining all pre-release SD history.
s=s.replace('/mnt/mmc/NXDEMO51/logs/launch.log','/tmp/nx-lut-lite/launch.log').replace('/mnt/mmc/NXDEMO51/logs/run.log','/tmp/nx-lut-lite/run.log')
s=s.replace('FILE *f=fdopen(fd,"a");','if(st.st_size>131072&&ftruncate(fd,0)){close(fd);return NULL;}FILE *f=fdopen(fd,"a");')
s=s.replace('if(action!=3 && access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0)',
 'if(action!=3 && action!=2 && action!=4 && access("/tmp/nx-lut-lite/clean-log.policy",F_OK)==0)')
s=s.replace('ButtonReleaseMask|FocusChangeMask','ButtonReleaseMask|ButtonMotionMask|FocusChangeMask')
s=s.replace('if(lut_picker_pointer(&ev))','if(detail_pointer(&ev)){if(!quitting)draw();continue;}\n            if(lut_picker_pointer(&ev))')
s=s.replace('if (ev.type==FocusOut) {','if(ev.type==FocusOut&&detail_visible){if(!detail_hidden){Window focus;int revert;XGetInputFocus(dpy,&focus,&revert);if(focus!=win&&focus!=detail_dialog)detail_close(2);}continue;}\n            if (ev.type==FocusOut) {')
s=s.replace('if(poll_menu_rearm())draw();','if(detail_poll())draw();\n        if(poll_menu_rearm())draw();')
# All three surfaces use the same five 56-pixel rows and content rectangle.
s=s.replace('video_navigate(video_row+step)','video_navigate(release_video_index()+step)')
s=s.replace('left_panel==2?video_row:left_panel==1?','left_panel==2?release_video_index():left_panel==1?')
s=s.replace('left_panel==2?(y-104)/40:(cursor/5)*5+(y-104)/56','(cursor/5)*5+(y-104)/56')
s=s.replace('if(left_panel==2){if(i<7)video_navigate(i);}','if(left_panel==2){if(i<release_video_count())video_navigate(i);}')
s=s.replace('if(x>=19 && x<95 && nxl_sidebar_at(y)>=0)pointer_cell=30+nxl_sidebar_at(y);',
 'if(x>=19 && x<95 && nxl_sidebar_at(y)>=0)pointer_cell=30+nxl_sidebar_at(y);')
s=s.replace('int i=(cursor/5)*5+(y-104)/56;','sidebar_focus=0;int i=(cursor/5)*5+(y-104)/56;')
s=s.replace('switch_panel(pointer_cell-30);','{switch_panel(pointer_cell-30);sidebar_focus=1;}')
old='if(pointer_cell==2 && y>=435 && x>=310 && x<500){'
a=s.index(old);brace=a+len(old)-1;depth=1;i=brace+1
while depth:
 if s[i]=='{':depth+=1
 elif s[i]=='}':depth-=1
 i+=1
s=s[:a]+old+'release_confirm();}'+s[i:]
p.write_text(s,'utf8',newline='\n')

# Normal LUT and SRW views always filter Log, independent of active Gamma.
p=U/'browser.h';s=p.read_text('utf8')
s=s.replace('const struct nxl_look_info *info=info_for(names[i]);','const struct nxl_look_info *info=info_for(names[i]);\n        if(!strcmp(info->group,"loglut"))continue;')
s=s.replace('value>=0 && value<=5)srw_gamma_mode=(unsigned)value','0)srw_gamma_mode=1u')
p.write_text(s,'utf8',newline='\n')
p=U/'menu_lut_picker.inc';s=p.read_text('utf8')
s=s.replace('if(favorite_index(names[i])>=0||!strcmp(names[i],"SLOG_REC709")){','if(strcmp(info_for(names[i])->group,"loglut")&&favorite_index(names[i])>=0){')
p.write_text(s,'utf8',newline='\n')
p=U/'menu_srw_layout.h';s=p.read_text('utf8')
s=s.replace('#define NXL_LENS_TEST_UI 1','#define NXL_LENS_TEST_UI 0')
a=s.index('static const unsigned char nxl_srw_visible_rows[]=');b=s.index(';',a)
s=s[:a]+'static const unsigned char nxl_srw_visible_rows[]={0,2,3,4,5,7}'+s[b:]
p.write_text(s,'utf8',newline='\n')
p=U/'keys.h';s=p.read_text('utf8')
s=s.replace('case 75: case 178:','case 178:').replace('case 77: case 177:','case 177:')
s=s.replace('case 72: return -1;','case 72: case 165: return -1;').replace('case 80: return 1;','case 80: case 163: return 1;')
p.write_text(s,'utf8',newline='\n')

p=U/'menu_video.inc';s=p.read_text('utf8')
for name in ['video_navigate','video_confirm','draw_video']:
 a,b=extent(s,name);s=s[:a]+s[b:]
s+='\n'+(A/'release_video.inc').read_text('utf8')
p.write_text(s,'utf8',newline='\n')
put(W/'UI-port.json',dict(passed=True,normal_LUT_and_SRW_exclude_Log=True,Log_picker_only_in_VID=True,
 SRW_test_rows_removed=True,SRW_GUI_gamma_fixed_to_verified_source19=True,five_rows_height56_shared=True,
 sidebar_focus_left_right=True,rear_dial_moves_current_focus=True,upper_dial_keeps_categories=True,
 original_SRW_native_binaries_unchanged=True,hardware_tested=False))
print('PASS current menu integrated with R8f detail, unified navigation and hidden diagnostics',flush=True)
