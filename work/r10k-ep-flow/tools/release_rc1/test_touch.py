from context import *
from cfunc import function
sys.path[:0]=[str(PY),str(SRC/'tools')]
from arm_model import ArmModel
V=next(W/('tests/touch-'+str(i))for i in range(1,100)if not(W/('tests/touch-'+str(i))).exists());V.mkdir(parents=True)
s=(T/'base/nx-lut-lite/src/menu_lut_detail_v2.inc').read_text('utf8')
prefix=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../build-tree/base/nx-lut-lite/src/lut_detail_flow.h"
enum{ButtonPress=4,ButtonRelease=5,MotionNotify=6};
typedef struct{int type;struct{int window;}xany;struct{int button,x,y;}xbutton;struct{int x;}xmotion;}XEvent;
static struct ld_flow detail;static int detail_visible=1,detail_hidden,detail_dialog,detail_drag,detail_cell=-1,detail_log_scope,selected,queued,mixture,closed;
static int detail_b_view[2]={0,1},detail_committed_params[4];static unsigned char down[256];
static char names[2][81]={"A","B"},detail_committed_b[81],detail_message[160];static unsigned long detail_due;
static void detail_b_rebuild(void){detail.b_count=2;}
static void detail_dialog_open(void){detail_dialog=2;}
static void detail_dialog_close(void){detail_dialog=0;}
static void detail_queue(int mix){queued++;mixture=mix;}
static void detail_close(int where){closed=where;}
static void detail_switch_a(int n){detail.cursor=n;}
'''
actual=''.join(function(s,n)for n in ['detail_result_action','detail_key','detail_slider','detail_pointer'])
tests=r'''
static void event(int type,int x,int y,int popup){XEvent e={0};e.type=type;e.xany.window=popup?2:1;e.xbutton.button=1;e.xbutton.x=x;e.xbutton.y=y;e.xmotion.x=x;assert(detail_pointer(&e));}
static void click(int x,int y,int popup){event(ButtonPress,x,y,popup);event(ButtonRelease,x,y,popup);}
int main(void){ld_begin(&detail,2,0,100,0);click(205,175,0);assert(detail.parameter==1);
 event(ButtonPress,240,130,0);assert(detail.a[1]==-10&&detail_drag);event(MotionNotify,680,130,0);assert(detail.a[1]==10);event(ButtonRelease,680,130,0);assert(!detail_drag);
 queued=0;click(400,20,0);assert(detail.level==LD_B_LIST&&detail_dialog==2);click(100,115,1);assert(detail.b_cursor==1);
 click(400,200,1);assert(detail.level==LD_B_PARAMS&&!detail.prepared);event(ButtonPress,220,50,1);event(MotionNotify,530,50,1);event(ButtonRelease,530,50,1);assert(detail.b[0]==100);
 click(400,200,1);assert(detail.level==LD_A&&detail.prepared&&!detail_dialog&&queued==0&&!strcmp(detail_committed_b,"B"));
 detail_key(96,1);detail_key(96,0);assert(queued==1&&mixture==1);
 detail_key(173,1);detail_key(173,0);assert(detail.level==LD_B_LIST);detail_key(96,1);detail_key(96,0);assert(detail.level==LD_B_PARAMS);
 detail_key(127,1);detail_key(127,0);assert(detail.level==LD_B_LIST);detail_key(127,1);detail_key(127,0);assert(detail.level==LD_A);detail_key(127,1);detail_key(127,0);assert(closed==1);
 detail_log_scope=1;detail_key(173,1);detail_key(173,0);assert(detail.level==LD_A&&!detail_dialog);click(400,20,0);assert(detail.level==LD_A&&!detail_dialog);
 puts("PASS actual touch tabs and sliders, B prepare without commit, final A OK, one-level MENU, Log B exclusion");return 0;}
'''
p=V/'touch.c';p.write_text(prefix+actual+tests,'utf8',newline='\n');host('touch',p,V/'touch.arm');r=ArmModel(V/'touch.arm').run();assert r['exit']==0,r
put(W/'touch-validation.json',dict(passed=True,actual_C_pointer_key_and_flow=True,actual_ARM=True,result=r,X11_event_delivery_modeled=True,hardware_tested=False))
print('PASS actual touch, slider and final mixture confirmation',flush=True)
