from context import *
sys.path[:0]=[str(PY),str(SRC/'tools'),str(A.parent/'formal-rc2-authoring')]
from cfunc import function
from post_file_model import PostFileModel
U=T/'base/nx-lut-lite/src';v=(U/'menu_video.inc').read_text('utf8');m=(U/'menu.c').read_text('utf8')
p=W/'tests/ui/vlog-dispatch.c'
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
static int video_child,video_command,rearm_child,lens_child,video_fd,video_used,video_timeout_sent;
static unsigned long video_started;static char notice[120],video_output[2048];static int calls;
enum{F_GETFL=1,F_SETFL=2,O_NONBLOCK=4};
static unsigned long ticks(void){return 1234;}
static pid_t backend(const char*mode,const char*name,int*fd){assert(!strcmp(mode,"gamma-toggle")&&!name[0]);calls++;*fd=44;return 77;}
static int fcntl(int f,int c,...){assert(f==44&&(c==F_GETFL||c==F_SETFL));return 0;}
static int close(int f){assert(f==44);return 0;}
static int kill(int p,int s){(void)p;(void)s;assert(0);return 0;}
#define SIGTERM 15
'''
code=prefix+function(v,'video_menu_start')+r'''
int main(void){video_menu_start(24);assert(calls==1&&video_child==77&&video_command==24&&video_fd==44&&video_started==1234);video_menu_start(24);assert(calls==1);video_child=0;rearm_child=1;video_menu_start(24);assert(calls==1);rearm_child=0;lens_child=1;video_menu_start(24);assert(calls==1);puts("PASS actual VLog shortcut dispatch to gamma-toggle with busy guards");return 0;}
'''
p.write_text(code,'utf8',newline='\n');host('vlog-dispatch',p,p.with_suffix('.arm'));fs=p.parent/'dispatch-fs';fs.mkdir(exist_ok=True);r=PostFileModel(p.with_suffix('.arm'),[],fs).run();assert r['exit']==0,r
assert 'if(ok&&detail_visible&&(video_command==21||video_command==22||video_command==24))detail_open_current();'in v
assert 'if(vlog_shortcut)'in m and 'while(video_child>0)'in m
put(W/'shortcut-dispatch-validation.json',dict(passed=True,result=r,actual_ARM_dispatch=True,new_shortcut_uses_existing_Gamma_toggle=True,busy_blocks_second_dispatch=True,detail_scope_refresh_on_success=True,backend_and_OS_modeled=True,hardware_tested=False))
print('PASS RC5 VLog dispatch')
