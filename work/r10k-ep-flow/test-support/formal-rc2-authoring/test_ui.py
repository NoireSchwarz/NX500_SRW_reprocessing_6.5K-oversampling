from context import *
import shutil,re
shutil.copyfile(A.parent/'formal-rc1-authoring/cfunc.py',A/'cfunc.py')
from cfunc import function
sys.path[:0]=[str(PY),str(SRC/'tools')];from post_file_model import PostFileModel
V=W/'tests/ui';V.mkdir(parents=True,exist_ok=True);U=T/'base/nx-lut-lite/src'
main=(U/'menu.c').read_text('utf8');detail=(U/'menu_lut_detail_v2.inc').read_text('utf8');video=(U/'menu_video.inc').read_text('utf8')
def check(name,source):
 p=V/(name+'.c');p.write_text(source,'utf8',newline='\n');host(name,p,p.with_suffix('.arm'));f=V/(name+'-fs');(f/'tmp/nx-lut-lite').mkdir(parents=True,exist_ok=True);r=PostFileModel(p.with_suffix('.arm'),[],f).run();assert r['exit']==0,(name,r);return r
prefix=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "../../build-tree/base/nx-lut-lite/src/lut_detail_flow.h"
#include "../../build-tree/base/nx-lut-lite/src/look_info.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_state.h"
typedef int Window;typedef struct{int mode,detail;}XFocusChangeEvent;
enum{NotifyNormal=0,NotifyGrab=1,NotifyUngrab=2,NotifyInferior=2,RevertToPointerRoot=0,CurrentTime=0};
static int detail_visible,lut_picker_open,video_picker_open,video_child,quitting,action,detail_hidden,detail_return,detail_dirty,detail_queued_mix,detail_done,detail_eof,detail_status,detail_overflow,detail_read_error,detail_running_mix;
static int rearm_child,video_enabled,count=1,selected=1,category,total_count=2,view[512]={0},detail_view[512],detail_fd=-1,detail_drag,detail_cell,pointer_down,pointer_cell,combo_block;
static struct ld_flow detail;static struct nxl_help_state help;static unsigned char down[256];static pid_t detail_child;
static int detail_pa[4],detail_pb[4],detail_committed_params[4],detail_log_scope;static size_t detail_used;static unsigned long now,detail_due,detail_message_until;
static char names[512][81]={"fuji_provia100f","vlog_pan_dramatic"},notice[120],detail_message[160],detail_committed_b[81],active_name[81],detail_a[81],detail_b[81],detail_output[4096],detail_expected[400],engine[1100]="/engine";
static int active_strength,dpy,win=1,detail_complete,unmaps,maps,focus_changes,starts,log_mode;
static unsigned long ticks(void){return now;}
static void launch_event(const char*a,const char*b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
static const char*selected_id(void){return names[0];}
static int effective_strength(void){return 100;}
static int detail_load_parameters(void){return 0;}
static int detail_cache_save(void){return 1;}
static void detail_close(int n){detail_return=n;}
static void XMoveResizeWindow(int d,int w,int x,int y,int width,int height){(void)d;(void)w;assert(!x&&y==240&&width==720&&height==240);}
static void XUnmapWindow(int d,int w){(void)d;(void)w;unmaps++;}
static void XMapRaised(int d,int w){(void)d;(void)w;maps++;}
static void XSetInputFocus(int d,int w,int r,int t){(void)d;(void)w;(void)r;(void)t;focus_changes++;}
static void task_child_watch_detach(void){}
static int test_access(const char*p,int m){(void)p;(void)m;return log_mode?0:-1;}
static int test_pipe(int*p){p[0]=90;p[1]=91;starts++;return 0;}
static int test_close(int fd){(void)fd;return 0;}
static int test_fork(void){return 77;}
static int test_fcntl(int fd,int cmd,...){(void)fd;(void)cmd;return 0;}
static int ConnectionNumber(int d){(void)d;return 3;}
static int test_wait(pid_t p,int*s,int flags){(void)flags;if(!detail_complete)return 0;*s=0;return p;}
static void detail_drain(void){if(detail_complete){strcpy(detail_output,detail_expected);detail_eof=1;}}
#define access test_access
#define pipe test_pipe
#define close test_close
#define fork test_fork
#define fcntl test_fcntl
#define waitpid test_wait
'''
actual=function((U/'browser.h').read_text('utf8'),'info_for')
actual+=''.join(function(detail,n)for n in ['detail_reset_inputs','detail_name','detail_queue','detail_open','detail_open_log','detail_start','detail_poll'])+function(main,'menu_focus_out')
tests=r'''
int main(void){assert(detail_open()&&detail_visible&&!detail_dirty&&!detail_child);for(now=0;now<3000;now+=50)detail_poll();assert(!starts&&!unmaps&&detail_visible);
 XFocusChangeEvent e={NotifyNormal,3};menu_focus_out(&e);assert(!quitting&&detail_visible);detail_visible=0;lut_picker_open=1;menu_focus_out(&e);assert(!quitting);lut_picker_open=0;video_picker_open=1;menu_focus_out(&e);assert(!quitting);video_picker_open=0;video_child=1;menu_focus_out(&e);assert(!quitting);video_child=0;
 e.mode=NotifyGrab;menu_focus_out(&e);assert(!quitting);e.mode=NotifyNormal;e.detail=NotifyInferior;menu_focus_out(&e);assert(!quitting);e.detail=3;menu_focus_out(&e);assert(quitting);quitting=0;
 log_mode=1;assert(detail_open_log()&&detail_visible&&!detail_dirty);now=5000;detail_poll();assert(!starts&&!unmaps);detail_queue(0);detail_poll();assert(!starts);now+=281;detail_poll();assert(starts==1&&detail_child==77&&!detail_hidden&&detail_visible&&!unmaps);menu_focus_out(&e);assert(!quitting);detail_complete=1;detail_poll();assert(!detail_child&&detail_visible&&!detail_hidden&&!unmaps&&!maps&&!focus_changes);
 puts("PASS actual 280ms open/idle replay, mapped apply, transient focus in all selectors and exact receipt");return 0;}
'''
result=check('lifetime',prefix+actual+tests)
# Production picker and navigation logic, with camera action dispatch modeled.
prefix=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../build-tree/base/nx-lut-lite/src/keys.h"
#include <stdlib.h>
#include "../../build-tree/base/nx-lut-lite/src/menu_srw_layout.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_state.h"
#include "../../build-tree/base/nx-lut-lite/src/gamma_reload_state.h"
enum{ButtonPress=4,ButtonRelease=5};typedef struct{int type;struct{int button,x,y;}xbutton;}XEvent;
static int video_picker_open,video_picker_cursor,video_picker_press=-1,video_child,rearm_child,video_row,pointer_down,pointer_cell,action,quitting,dispatch=-1;static unsigned char down[256];static char notice[120];
static struct{int mode;}video_status;static struct nxl_help_state help;static struct nxl_gamma_status gamma_status;static int gamma_command_failed;
static void video_menu_start(int n){dispatch=n;}
static int detail_open_log(void){dispatch=88;return 1;}
'''
picker=(U/'menu_video_picker.inc').read_text('utf8');picker=picker[picker.index('static const int video_picker_commands'):picker.index('static void draw_video_picker')]
actual=picker+re.search(r'static const unsigned char release_video_rows\[\]=\{[^}]+\};',video).group()+'\n'+''.join(function(video,n)for n in ['release_video_count','release_video_index','video_navigate','video_confirm'])
tests=r'''
static void press(int c){assert(video_picker_key(c,1));assert(video_picker_key(c,0));}
int main(void){assert(video_picker_count()==3&&strlen(video_picker_titles[0])>0);assert(nxl_srw_ui_count()==5&&nxl_srw_ui_index(5)<0&&nxl_srw_ui_index(13)<0&&nxl_srw_ui_index(9)<0);for(int i=0;i<5;i++)assert(nxl_srw_ui_touch(i,104+i*56)==i);
 assert(release_video_count()==4);for(int i=0;i<100;i++){video_navigate(i);assert(video_row==0||video_row==9||video_row==6||video_row==8);assert(release_video_index()<5);}
 video_row=9;video_status.mode=3;video_confirm();assert(video_picker_open&&video_picker_cursor==0&&dispatch==-1);press(127);assert(!video_picker_open&&dispatch==-1);
 for(int i=0;i<3;i++){video_status.mode=i==0?3:i==1?2:1;video_confirm();assert(video_picker_cursor==i);press(96);assert(!video_picker_open&&dispatch==(i==0?3:i==1?1:2));}
 video_confirm();press(163);assert(video_picker_cursor==0);press(165);assert(video_picker_cursor==2);press(127);
 for(int i=0;i<3;i++){video_confirm();XEvent e={.type=ButtonPress,.xbutton={1,200,155+i*40+10}};assert(video_picker_pointer(&e));e.type=ButtonRelease;assert(video_picker_pointer(&e));assert(dispatch==(i==0?3:i==1?1:2)&&!video_picker_open);}
 puts("PASS 5 SRW rows, 4 VID rows, exactly 3 output routes, key/touch confirmation and one-level MENU");return 0;}
'''
navigation=check('navigation',prefix+actual+tests)
assert'XUnmapWindow'not in function(detail,'detail_start')and 'detail_queue(0)'not in function(detail,'detail_open')
assert'release_prepare_next'not in main and 'release_next_status'not in main
put(W/'UI-validation.json',dict(passed=True,lifetime=result,navigation=navigation,actual_production_C_executed_as_ARM=True,
 OS_X11_and_camera_dispatch_modeled=True,opening_no_auto_apply=True,live_apply_never_unmaps_selector=True,
 all_modals_keep_focus_refresh=True,explicit_external_parent_focus_still_exits=True,hardware_tested=False))
print('PASS RC2 actual UI lifetime and navigation regressions',flush=True)
