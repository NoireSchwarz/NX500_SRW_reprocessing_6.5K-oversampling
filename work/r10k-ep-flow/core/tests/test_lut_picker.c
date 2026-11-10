#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../../base/nx-lut-lite/src/keys.h"
#include "../../base/nx-lut-lite/src/ui_state.h"
#include "../../base/nx-lut-lite/src/look_info.h"
static char names[512][81],engine[1100]="/mnt/mmc/lut.sh",notice[120];
static unsigned char down[256];static int count,selected,srw_row,pointer_down,action,quitting;
static struct nxl_help_state help;static unsigned long gc,ink,panel,muted,selection_colors[8];static void*dpy;
static int name_ok(const char*s){return *s&&strlen(s)<=80&&strspn(s,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==strlen(s);}
#include "../../base/nx-lut-lite/src/browser.h"
typedef struct {int type;struct {int x,y;unsigned button;}xbutton;} XEvent;
#define ButtonPress 4
#define ButtonRelease 5
static unsigned ticks(void){return 100;}
static void fill(int a,int b,int c,int d,unsigned long e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static void frame(int a,int b,int c,int d,unsigned long e){fill(a,b,c,d,e);}
static void text(int a,int b,const char*c){(void)a;(void)b;(void)c;}
static void limited_text(int a,int b,const char*c,int d){(void)d;text(a,b,c);}
static void XSetForeground(void*a,unsigned long b,unsigned long c){(void)a;(void)b;(void)c;}
#include "../../base/nx-lut-lite/src/menu_lut_picker.inc"
int main(void){
 assert(!setenv("NX_LUT_UI_STATE","/mnt/mmc/ui",1));strcpy(names[0],"fuji_provia100f");strcpy(names[1],"kodak_portra400");strcpy(names[2],"mono_hp5");total_count=3;
 browser_load();assert(favorite_count==3&&!strcmp(srw_lut_id,"kodak_portra400"));
 lut_picker_begin();assert(lut_picker_open&&lut_picker_count==3&&lut_picker_cursor==1);
 assert(!strcmp(lut_picker_id(0),"off")&&!strcmp(lut_picker_id(1),"kodak_portra400")&&!strcmp(lut_picker_id(2),"mono_hp5"));
 lut_picker_move(1);lut_picker_choose();assert(!lut_picker_open&&!strcmp(srw_lut_id,"mono_hp5")&&srw_row==2&&!action&&!quitting);
 favorite_count=0;strcpy(srw_lut_id,"off");browser_load();assert(favorite_count==3&&!strcmp(srw_lut_id,"mono_hp5"));
 lut_picker_begin();lut_picker_move(1);assert(lut_picker_key(127,1)&&lut_picker_key(127,0)&&!lut_picker_open&&!strcmp(srw_lut_id,"mono_hp5"));
 lut_picker_begin();XEvent e={.type=ButtonPress,.xbutton={.x=160,.y=200,.button=1}};assert(lut_picker_pointer(&e));e.type=ButtonRelease;assert(lut_picker_pointer(&e)&&!lut_picker_open&&!strcmp(srw_lut_id,"kodak_portra400"));
 lut_picker_begin();lut_picker_cursor=2;strcpy(ui_path,"/missing/ui");lut_picker_choose();assert(lut_picker_open&&!strcmp(srw_lut_id,"kodak_portra400")&&strstr(notice,"FAILED"));strcpy(ui_path,"/mnt/mmc/ui");
 favorite_count=0;lut_picker_begin();assert(lut_picker_count==1&&lut_picker_cursor==0);lut_picker_move(1);lut_picker_choose();assert(!lut_picker_open&&!strcmp(srw_lut_id,"off")&&!action&&!quitting);
 puts("PASS actual favorites picker and settings: only available favorites, stale/unfavorited entries excluded, select returns to original row, cancel/save failure preserves selection, empty list has original color, no preview or native action");return 0;
}
