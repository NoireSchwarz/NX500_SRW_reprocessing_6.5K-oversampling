from context import *
import re
sys.path[:0]=[str(PY),str(SRC/'tools'),str(A.parent/'formal-rc2-authoring')]
from cfunc import function
from post_file_model import PostFileModel
V=W/'tests/ui';V.mkdir(parents=True,exist_ok=True);U=T/'base/nx-lut-lite/src'
main=(U/'menu.c').read_text('utf8');detail=(U/'menu_lut_detail_v2.inc').read_text('utf8')
def check(name,code):
 p=V/(name+'.c');p.write_text(code,'utf8',newline='\n');host(name,p,p.with_suffix('.arm'))
 f=V/(name+'-fs');(f/'tmp/nx-lut-lite').mkdir(parents=True,exist_ok=True);r=PostFileModel(p.with_suffix('.arm'),[],f).run();assert r['exit']==0,(name,r);return r
s=(A.parent/'formal-rc2-authoring/test_ui.py').read_text('utf8')
prefix=re.search("prefix=r'''(.*?)'''",s,re.S).group(1)
prefix=prefix.replace(',quitting,action,',',').replace('rearm_child,video_enabled,','rearm_child,')
prefix=prefix.replace('static int detail_load_parameters(void){return 0;}','static int detail_load_parameters(void){detail.a[0]=25;return 0;}')
prefix=prefix.replace('static void task_child_watch_detach(void){}','''static void task_child_watch_detach(void){}
static void menu_input_child_close(void){}
static int modal_available=1;static int menu_modal_input_take(void){return modal_available;}
static int sidebar_focus,left_panel;
static void refresh_lut_runtime(void){strcpy(active_name,log_mode?"vlog_pan_dramatic":"fuji_provia100f");active_strength=60;}
static void browser_rebuild(const char*w){(void)w;count=1;view[0]=0;selected=1;}
static void detail_dialog_close(void){}
static int detail_applied_parameters(void){detail.a[0]=60;detail.a[1]=2;detail.b[0]=40;detail.prepared=!detail_log_scope;strcpy(detail_committed_b,"fuji_provia100f");return 1;}
''')
prefix=prefix.replace('if(detail_complete){strcpy(detail_output,detail_expected);','if(detail_complete){strcpy(detail_output,detail_expected);')
actual=function((U/'browser.h').read_text('utf8'),'info_for')+''.join(function(detail,n)for n in ['detail_reset_inputs','detail_name','detail_queue','detail_switch_a','detail_open','detail_open_log','detail_start','detail_poll','detail_open_current'])
tests=r'''
int main(void){
 modal_available=0;assert(detail_open()&&!detail_visible&&!detail_child&&!detail_dirty);modal_available=1;
 assert(detail_open()&&detail_visible&&!detail_dirty&&detail.count==2&&detail.cursor==1);
 for(now=0;now<3000;now+=50)detail_poll();assert(!starts&&!unmaps&&detail_visible);
 detail_switch_a(0);assert(!detail_dirty&&!strcmp(detail_name(detail.cursor),"off")&&detail.a[0]==0);
 detail_switch_a(1);assert(!detail_dirty);now+=1000;detail_poll();assert(!starts);
 detail_queue(0);detail_due=0;detail_poll();assert(starts==1&&detail_child==77&&!detail_hidden&&detail_visible&&!unmaps);
 detail_complete=1;detail_poll();assert(!detail_child&&detail_visible&&!detail_hidden&&!unmaps&&!maps&&!focus_changes);
 detail.a[0]=10;detail.prepared=1;assert(detail_open_current()&&detail_visible&&detail.cursor==1&&detail.a[0]==60&&detail.a[1]==2&&detail.b[0]==40&&detail.prepared);
 log_mode=1;assert(detail_open_current()&&detail_visible&&detail_log_scope&&detail.count==2&&detail.cursor==1&&detail.a[0]==60&&!detail.prepared);
 puts("PASS actual UI ARM: no apply on open/change; OK-only worker stays mapped; shortcut uses applied parameters, STD/Log/off scope");return 0;}
'''
lifetime=check('manual-and-current',prefix+actual+tests)
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../build-tree/base/nx-lut-lite/src/ev_triple.h"
#include "../../build-tree/base/nx-lut-lite/src/keys.h"
static struct nxl_ev_taps menu_ev_taps;static unsigned char down[256];static int combo_block,singles,quick,direct,video_child,video_command;static unsigned long now;
static unsigned long ticks(void){return now;}
static void key_direct(unsigned c,int v){direct++;if(c==NXL_EV&&!v)singles++;}
static int detail_open_current(void){quick++;return 1;}
'''
actual=''.join(function(main,n)for n in ['menu_ev_single','menu_ev_tick','key'])
tests=r'''
static void reset(void){memset(&menu_ev_taps,0,sizeof menu_ev_taps);memset(down,0,sizeof down);combo_block=singles=quick=direct=0;now=100;}
static void tap(void){key(NXL_EV,1);now+=50;key(NXL_EV,0);now+=100;}
int main(void){
 reset();tap();assert(!singles&&!quick);now+=500;assert(menu_ev_tick()&&singles==1&&!quick);
 reset();tap();tap();tap();assert(quick==1&&!singles);now+=600;menu_ev_tick();assert(quick==1&&!singles);
 reset();key(NXL_EV,1);for(int i=0;i<10;i++){now+=30;key(NXL_EV,2);}key(NXL_EV,0);assert(!quick);now+=600;menu_ev_tick();assert(singles==1);
 reset();key(NXL_EV,1);now+=500;key(NXL_EV,0);tap();tap();assert(!quick);
 reset();tap();key(80,1);key(80,0);tap();tap();assert(!quick&&singles==1);
 reset();tap();now+=1000;menu_ev_tick();tap();tap();assert(!quick&&singles==1);
 reset();down[NXL_AEL]=1;tap();tap();tap();assert(!quick&&!singles);
 reset();key(NXL_EV,1);key(NXL_WIFI,1);key(NXL_EV,0);key(NXL_WIFI,0);tap();tap();assert(!quick);
 reset();video_child=1;video_command=3;key(NXL_S1,1);key(NXL_S1,0);assert(!direct);video_child=0;key(NXL_S1,1);assert(direct==1);
 puts("PASS actual gesture wrapper: delayed single EV, triple EV, repeat/hold/gap/other-key/chord cancellation");return 0;
}
'''
gesture=check('triple-EV',prefix+actual+tests)
# There is exactly one queue site in the UI action handler: confirmed OK.
action=function(detail,'detail_result_action')
assert 'detail_queue(0)'not in function(detail,'detail_switch_a') and action.count('detail_queue(')==1
assert 'LD_COMMIT){detail_queue(1);detail_due=0;'in action
assert 'XUnmapWindow'not in function(detail,'detail_start')
assert 'menu_input_child_close()'in function(detail,'detail_start')
assert 'menu_input_lease.h'in main
put(W/'UI-validation.json',dict(passed=True,lifetime=lifetime,gesture=gesture,manual_OK_only=True,applied_receipt_precedes_draft=True,modal_input_lease_preserved=True,actual_production_C_as_ARM=True,OS_X11_and_camera_modeled=True,hardware_tested=False))
print('PASS RC4 UI',flush=True)
