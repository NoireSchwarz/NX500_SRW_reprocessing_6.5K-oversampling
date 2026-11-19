"""Render actual ARM menu drawing functions through recorded X11 rectangles."""
from context import *
from cfunc import function
import re
sys.path[:0]=[str(PY),str(SRC/'tools')]
from arm_model import ArmModel
from PIL import Image,ImageDraw
V=next(W/('tests/visual-'+str(i))for i in range(1,100)if not(W/('tests/visual-'+str(i))).exists());V.mkdir(parents=True)
U=T/'base/nx-lut-lite/src';main=(U/'menu.c').read_text('utf8');detail=(U/'menu_lut_detail_v2.inc').read_text('utf8');vid=(U/'menu_video.inc').read_text('utf8');browser=(U/'browser.h').read_text('utf8');picker=(U/'menu_lut_picker.inc').read_text('utf8')
prefix=r'''#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "../../build-tree/base/nx-lut-lite/src/ui_font.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_i18n.h"
#include "../../build-tree/base/nx-lut-lite/src/look_info.h"
#include "../../build-tree/base/nx-lut-lite/src/ui_state.h"
#include "../../build-tree/base/nx-lut-lite/src/gamma_reload_state.h"
#include "../../build-tree/base/nx-lut-lite/src/video_menu_state.h"
#include "../../build-tree/base/nx-lut-lite/src/lut_detail_flow.h"
typedef struct{short x,y;unsigned short width,height;}XRectangle;
static int dpy,gc,win=1,canvas=1;static unsigned long fg;
static unsigned long bg=0x242628,ink=0xf4f5f6,accent=0x21c9ee,dark=0x202326,button_bg=0x35383b,line_color=0x61666a,muted=0xb8bdc1,panel=0x263947,color_tab=0x50565b;
static unsigned long selection_colors[]={0x059dd2,0x059acf,0x0597cc,0x0594c9,0x0591c6,0x058ec3,0x058bc0,0x0588bd};
static void XSetForeground(int d,int g,unsigned long p){(void)d;(void)g;fg=p;}
static void XFillRectangle(int d,int c,int g,int x,int y,unsigned w,unsigned h){(void)d;(void)g;printf("R %d %d %d %u %u %lu\n",c,x,y,w,h,fg);}
static void XFillRectangles(int d,int c,int g,XRectangle*r,int n){for(int i=0;i<n;i++)XFillRectangle(d,c,g,r[i].x,r[i].y,r[i].width,r[i].height);}
static void XFlush(int d){(void)d;}
static void publish_ui(void){}
static int left_panel,sidebar_focus,category,selected=1,count,total_count,view[512],favorite_count=2,active_strength=100,video_enabled;
static int srw_file_mode,srw_cursor,srw_count=1,srw_row=4,video_row=1,gamma_command_failed,log_active;
static char names[512][81],favorites[512][81],active_name[81],notice[120],srw_source[120]="SAM_0183.SRW",srw_lut_id[81]="off",srw_files[1][81]={{"SAM_0183.SRW"}};
static struct nxl_raw_status raw_status={.state=NXL_RAW_NEED_STILL_INIT,.valid=1};static struct nxl_help_state help;
static struct nxl_video_status video_status={.valid=1,.mode=3};static struct nxl_gamma_status gamma_status;
static int detail_visible,detail_hidden,detail_dialog,detail_view[512],detail_b_view[512],detail_log_scope;static struct ld_flow detail;
static char detail_message[160],detail_committed_b[81];static const char*detail_labels[]={"濃淡","色相","銳度","對比"};
static const char*group_names[]={"全部","收藏","人像","電影","風景","黑白","復古","其他"};static const char*group_ids[]={"all","favorites","portrait","cinema","landscape","mono","retro","other"};
static int lut_picker_open,lut_picker_count,lut_picker_cursor,lut_picker_view[513];
static int test_access(const char*p,int m){(void)p;(void)m;return log_active?0:-1;}
#define F_OK 0
#define access test_access
static int effective_strength(void){return 100;}
static const char*srw_basename(const char*s){return s;}
'''
src=prefix
for name in ['fill','next_cp','text','limited_text','frame']:src+=function(main,name)
for name in ['info_for','selected_id','favorite_index','browser_rebuild']:src+=function(browser,name)
src+=function(main,'selected_info')+function(main,'row_title')+function(main,'draw_sidebar')
src+=function(picker,'lut_picker_id')+function(picker,'draw_lut_picker')
src+=re.search(r'static const unsigned char release_video_rows\[\]=\{[^}]+\};',vid).group()+'\n'
for name in ['release_video_count','release_video_index','draw_video']:src+=function(vid,name)
src+=function(main,'draw_srw')
src+=function(detail,'detail_name')+function(detail,'detail_short_name')+function(detail,'detail_b_rebuild')+function(detail,'detail_draw')+function(main,'draw')
src+=r'''
int main(int argc,char**argv){assert(argc==2);int mode=atoi(argv[1]);total_count=sizeof nxl_look_infos/sizeof*nxl_look_infos;
 for(int i=0;i<total_count;i++)strcpy(names[i],nxl_look_infos[i].id);strcpy(favorites[0],names[0]);strcpy(favorites[1],names[1]);browser_rebuild("");
 if(mode==0){sidebar_focus=1;draw();}
 if(mode==1){left_panel=1;draw_srw();}
 if(mode==2){left_panel=2;draw_video();}
 if(mode==3){left_panel=2;video_row=8;log_active=1;draw_video();}
 if(mode>=4){memcpy(detail_view,view,sizeof view);ld_begin(&detail,count,3,65,0);detail.a[1]=3;detail.a[2]=-2;detail.a[3]=4;
  if(mode==5||mode==6){detail_dialog=2;detail.level=mode==5?LD_B_LIST:LD_B_PARAMS;detail.b_category=3;detail_b_rebuild();detail.b[0]=80;detail.b[1]=-4;}
  if(mode==7){int n=0;for(int i=0;i<total_count;i++)if(!strcmp(info_for(names[i])->group,"loglut"))detail_view[n++]=i;detail_log_scope=1;ld_begin(&detail,n,0,100,0);}
  detail_draw();}
 return 0;}
'''
p=V/'render.c';p.write_text(src,'utf8',newline='\n');host('visual-functions',p,V/'render.arm')
out=W/'UI-preview';out.mkdir(exist_ok=True);rows=[]
names=['01-LUT-sidebar','02-SRW','03-VID-page1','04-VID-page2','05-A-half','06-B-picker','07-B-params','08-Log-half']
for mode,name in enumerate(names):
 r=ArmModel(V/'render.arm',[str(mode)]).run();assert r['exit']==0,r
 images={1:Image.new('RGB',(720,240 if mode>=4 else 480),(36,38,40)),2:Image.new('RGB',(560,224),(36,38,40))};ops=0
 for line in r['stdout'].splitlines():
  tag,c,x,y,w,h,pixel=line.split();assert tag=='R';c,x,y,w,h,pixel=map(int,[c,x,y,w,h,pixel]);assert w<1000 and h<1000
  im=images[c];ImageDraw.Draw(im).rectangle((x,y,x+w-1,y+h-1),fill=((pixel>>16)&255,(pixel>>8)&255,pixel&255));ops+=1
 target=out/(name+'.png');images[2 if mode in [5,6]else 1].save(target)
 rows.append(dict(name=name,mode=mode,rectangles=ops,image=str(target)))
# Compact visual index for inspection, from the real generated bitmaps.
sheet=Image.new('RGB',(1440,480*3),(24,25,26))
for i,k in enumerate([0,1,2,3,4,7]):
 im=Image.open(rows[k]['image']);sheet.paste(im,(i%2*720,i//2*480))
sheet.save(out/'contact-sheet.png')
put(W/'UI-visual-validation.json',dict(passed=True,actual_ARM_drawing_functions=True,X11_rectangle_endpoint_modeled=True,
 actual_bitmap_font=True,screens=rows,hardware_tested=False))
print('PASS eight actual menu renderings',out,flush=True)
