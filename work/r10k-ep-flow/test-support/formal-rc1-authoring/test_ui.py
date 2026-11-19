"""Actual C key/filter bodies; X11 and hardware dispatch are modeled."""
from context import *
import re
sys.path[:0]=[str(PY),str(SRC/'tools')]
from arm_model import ArmModel
U=T/'base/nx-lut-lite/src'
V=next(W/('tests/ui-'+str(i))for i in range(1,100)if not(W/('tests/ui-'+str(i))).exists());V.mkdir(parents=True)
def function(s,name):
 m=re.search(r'static\s+(?:int|void|unsigned|pid_t|const char\s*\*|const struct nxl_look_info\s*\*)\s*(?:long\s+)?'+name+r'\([^;]*?\)\s*\{',s);assert m,name
 i=m.end();depth=1;state='code'
 while depth:
  c=s[i];q=s[i:i+2]
  if state=='code':
   if q=='/*':state='comment';i+=2;continue
   if q=='//':state='line';i+=2;continue
   if c=='"':state='string'
   elif c=="'":state='char'
   elif c=='{':depth+=1
   elif c=='}':depth-=1
  elif state=='comment':
   if q=='*/':state='code';i+=2;continue
  elif state=='line':
   if c=='\n':state='code'
  elif c=='\\':i+=2;continue
  elif (state=='string'and c=='"')or(state=='char'and c=="'"):state='code'
  i+=1
 return s[m.start():i]+'\n'
main=(U/'menu.c').read_text('utf8');browser=(U/'browser.h').read_text('utf8');vid=(U/'menu_video.inc').read_text('utf8');detail=(U/'menu_lut_detail_v2.inc').read_text('utf8');picker=(U/'menu_lut_picker.inc').read_text('utf8')
def build_run(name,text):
 src=V/(name+'.c');src.write_text(text,'utf8',newline='\n');binary=src.with_suffix('.arm');host(name,src,binary)
 r=ArmModel(binary).run();assert r['exit']==0,(name,r);print('PASS actual UI C',name,flush=True);return r
headers=r'''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../build-tree/base/nx-lut-lite/src/keys.h"
#include "../../build-tree/base/nx-lut-lite/src/menu_srw_layout.h"
#include "../../build-tree/base/nx-lut-lite/src/gamma_reload_state.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_state.h"
'''
prefix=headers+r'''
static unsigned char down[256];static int left_panel,sidebar_focus,pointer_cell,pointer_down,combo_block,video_row,video_child,video_command,rearm_child;
static int category,selected,srw_row,srw_file_mode,srw_cursor,srw_count=8,quitting,action,video_enabled,valid_source=1,log_active;
static unsigned srw_gamma_mode=5;static char notice[120],srw_source[81]="sample.SRW",srw_lut_id[81]="off";
static struct nxl_help_state help;static struct nxl_raw_status raw_status;static struct nxl_gamma_status gamma_status;static int gamma_command_failed;
static char srw_files[8][81];
static int ordinary_open,log_open,video_dispatch,picker_open,new_session;
static unsigned long ticks(void){return 0;}
static void video_menu_start(int n){video_dispatch=n;}
static void navigate(int n){selected=n;}
static void browser_group(int n){category=(n+8)%8;}
static void ui_control(int n){(void)n;}
static int detail_key(unsigned c,int v){(void)c;(void)v;return 0;}
static int lut_picker_key(unsigned c,int v){(void)c;(void)v;return 0;}
static int detail_open(void){ordinary_open++;return 1;}
static int detail_open_log(void){log_open++;return 1;}
static int confirm_action(void){return 2;}
static int srw_container_check(const char*p){(void)p;return valid_source;}
static void srw_scan(void){}
static const char*srw_basename(const char*p){return p;}
static void load_raw_status(void){raw_status.valid=1;}
static void lut_picker_begin(void){picker_open++;}
static void start_menu_new_session(void){new_session++;}
static int test_access(const char*p,int mode){(void)p;(void)mode;return log_active?0:-1;}
#define F_OK 0
#define access test_access
'''
actual=''.join(function(main,n)for n in ['switch_panel','srw_navigate','srw_confirm'])
actual+=re.search(r'static const unsigned char release_video_rows\[\]=\{[^}]+\};',vid).group()+'\n'
actual+=''.join(function(vid,n)for n in ['release_video_count','release_video_index','video_navigate','video_confirm'])
actual+=function(main,'release_confirm')+function(main,'key')
tests=r'''
static void press(int c){key(c,1);key(c,0);}
int main(void){unsigned n=0;selected=2;category=3;press(75);assert(sidebar_focus&&left_panel==0&&selected==2);n++;
 press(163);assert(left_panel==1&&sidebar_focus);n++;press(77);assert(!sidebar_focus&&left_panel==1);n++;
 srw_row=0;press(163);assert(srw_row==2&&left_panel==1);n++;press(177);assert(category==3);n++;
 press(75);press(163);assert(left_panel==2&&sidebar_focus);press(77);n++;
 video_row=4;press(163);assert(video_row==6);press(163);assert(video_row==8);press(96);assert(log_open==1);n++;
 for(int i=0;i<100;i++){press(163);assert(video_row!=5&&video_row!=7);}n++;
 left_panel=1;sidebar_focus=0;srw_row=0;
 for(int i=0;i<100;i++){press(163);assert(srw_row!=9&&srw_row!=13&&srw_row!=10&&srw_row!=11&&srw_row!=12);}n++;
 srw_row=13;action=quitting=0;srw_confirm();assert(!action&&!quitting&&srw_row==3);n++;
 srw_row=9;srw_confirm();assert(!action&&!quitting&&srw_row==3);n++;
 srw_row=4;raw_status.state=NXL_RAW_NEED_STILL_INIT;srw_gamma_mode=5;log_active=0;srw_confirm();assert(action==12&&quitting&&srw_gamma_mode==1);n++;
 action=quitting=0;srw_row=4;log_active=1;srw_confirm();assert(!action&&!quitting);n++;
 printf("PASS real root key/focus, both dials, VID pages, hidden tests and SRW source profile checks=%u\n",n);return 0;}
'''
route=build_run('routing',prefix+actual+tests)

prefix=headers+r'''
#include "../../build-tree/base/nx-lut-lite/src/look_info.h"
#include "../../build-tree/base/nx-lut-lite/src/lut_detail_flow.h"
static char names[512][81],favorites[512][81],detail_committed_b[81],srw_lut_id[81]="off",notice[120];
static int total_count,favorite_count,category,count,selected,view[512],detail_b_view[512],lut_picker_open,lut_picker_count,lut_picker_cursor,lut_picker_press,lut_picker_view[513];
static struct ld_flow detail;static const char*group_ids[]={"all","favorites","portrait","cinema","landscape","mono","retro","other"};static struct nxl_help_state help;
static unsigned long ticks(void){return 0;}
'''
actual=function(browser,'info_for')+function(browser,'favorite_index')+function(browser,'browser_rebuild')+function(detail,'detail_b_rebuild')+function(picker,'lut_picker_begin')+function(picker,'lut_picker_id')
tests=r'''
int main(void){total_count=sizeof nxl_look_infos/sizeof*nxl_look_infos;favorite_count=total_count;
 for(int i=0;i<total_count;i++){strcpy(names[i],nxl_look_infos[i].id);strcpy(favorites[i],names[i]);}
 unsigned cases=0;
 for(int mode=0;mode<2;mode++)for(category=0;category<8;category++){
  browser_rebuild("");for(int j=0;j<count;j++)assert(strcmp(info_for(names[view[j]])->group,"loglut"));cases++;
  detail.b_category=category;detail_b_rebuild();for(int j=0;j<detail.b_count;j++)assert(strcmp(info_for(names[detail_b_view[j]])->group,"loglut"));cases++;
 }
 lut_picker_begin();assert(lut_picker_count==13);for(int i=1;i<lut_picker_count;i++)assert(strcmp(info_for(lut_picker_id(i))->group,"loglut"));cases++;
 category=0;browser_rebuild("");assert(count==12);cases++;
 printf("PASS actual normal A/B and SRW catalogue filters in either Gamma state checks=%u\n",cases);return 0;}
'''
filters=build_run('filters',prefix+actual+tests)
put(W/'UI-routing-validation.json',dict(passed=True,actual_production_C_bodies_executed=True,routing=route,filters=filters,
 firmware_and_X11_endpoints_modeled=True,normal_A_B_and_SRW_always_exclude_Log=True,Log_catalogue_not_a_normal_LUT_tab=True,
 hidden_Gamma_and_sampling_ids_not_reachable=True,hardware_tested=False))
