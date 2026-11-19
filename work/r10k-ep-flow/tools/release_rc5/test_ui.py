from context import *
s=(A.parent/'formal-rc4-authoring/test_ui.py').read_text('utf8')
s=s[:s.index("prefix=r'''\n#include <assert.h>\n#include <stdio.h>\n#include <string.h>")]
exec(compile(s,str(A/'test_ui.py'),'exec'))
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../build-tree/base/nx-lut-lite/src/ev_triple.h"
#include "../../build-tree/base/nx-lut-lite/src/keys.h"
static struct nxl_ev_taps menu_ev_taps;static struct nxl_shortcut menu_shortcut;static unsigned char down[256];
static int combo_block,singles,quick,direct,video_child,video_command,detail_child,detail_dirty,rearm_child,vlog;static unsigned long now;static char notice[120];
static unsigned long ticks(void){return now;}
static void key_direct(unsigned c,int v){direct++;if(c==NXL_EV&&!v)singles++;if(c<256&&v<2)down[c]=v;}
static int detail_open_current(void){quick++;return 1;}
static void video_menu_start(int n){assert(n==24);vlog++;}
'''
actual=''.join(function(main,n)for n in ['menu_ev_single','menu_ev_tick','key'])
tests=r'''
static void reset(void){memset(&menu_ev_taps,0,sizeof menu_ev_taps);memset(&menu_shortcut,0,sizeof menu_shortcut);memset(down,0,sizeof down);combo_block=singles=quick=direct=video_child=video_command=detail_child=detail_dirty=rearm_child=vlog=0;now=100;}
static void tap(void){key(NXL_EV,1);now+=50;key(NXL_EV,0);now+=100;}
static void chord(int first,int last){key(first,1);key(last,1);assert(!vlog);key(first,0);assert(!vlog);key(last,0);}
int main(void){
 reset();tap();assert(!singles&&!quick);now+=500;assert(menu_ev_tick()&&singles==1&&!quick);
 reset();tap();tap();assert(quick==1&&!singles);now+=600;menu_ev_tick();assert(quick==1&&!singles);
 reset();key(NXL_EV,1);for(int i=0;i<10;i++){now+=25;key(NXL_EV,2);}key(NXL_EV,0);assert(!quick);now+=600;menu_ev_tick();assert(singles==1);
 reset();key(NXL_EV,1);now+=500;key(NXL_EV,0);tap();assert(!quick);
 reset();tap();key(80,1);key(80,0);tap();assert(!quick&&singles==1);
 reset();tap();now+=1000;menu_ev_tick();tap();assert(!quick&&singles==1);
 reset();chord(NXL_EV,NXL_WIFI);assert(vlog==1&&!quick&&!singles);now+=1000;menu_ev_tick();assert(!singles);
 reset();chord(NXL_WIFI,NXL_EV);assert(vlog==1&&!quick&&!singles);
 reset();chord(NXL_EV,NXL_AEL);assert(!vlog&&!quick&&!singles);
 reset();chord(NXL_AEL,NXL_WIFI);assert(!vlog&&!quick&&!singles);
 reset();key(NXL_EV,1);key(NXL_WIFI,1);key(NXL_AEL,1);key(NXL_EV,0);key(NXL_WIFI,0);key(NXL_AEL,0);assert(!vlog&&!quick);
 reset();detail_child=1;chord(NXL_EV,NXL_WIFI);assert(!vlog);
 reset();video_child=1;video_command=3;key(NXL_S1,1);key(NXL_S1,0);assert(!direct);video_child=0;key(NXL_S1,1);assert(direct==1);
 struct nxl_shortcut listener={0};assert(!nxl_activate(&listener,NXL_EV,1));assert(!nxl_activate(&listener,NXL_WIFI,1));assert(!nxl_activate(&listener,NXL_EV,2));assert(!nxl_activate(&listener,NXL_WIFI,0));assert(nxl_activate(&listener,NXL_EV,0)==NXL_TOGGLE_VLOG);
 puts("PASS actual double EV, EV+WiFi VLog in both orders, menu/ambiguity/busy guards and one dispatch per release");return 0;
}
'''
gesture=check('double-EV-and-VLog',prefix+actual+tests)
assert '--vlog-toggle'in(U/'keys.c').read_text('utf8')
assert 'command==NXL_TOGGLE_VLOG&&menu_active()'in(U/'keys.c').read_text('utf8')
put(W/'UI-validation.json',dict(passed=True,lifetime=lifetime,gesture=gesture,double_EV_current_applied_state=True,EV_WiFi_VLog=True,AEL_WiFi_not_bound=True,menu_and_listener_no_duplicate_dispatch=True,actual_ARM_control_flow=True,OS_X11_and_backend_modeled=True,hardware_tested=False))
print('PASS RC5 UI')
