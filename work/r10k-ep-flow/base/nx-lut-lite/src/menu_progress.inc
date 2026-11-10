/* One passive X11 overlay during the worker. No native focus or input grab.
 * The owning action process closes it before reopening the normal menu. */
#include "progress_view.h"
static struct nxl_progress_view task_progress;
static int task_progress_auto,task_auto_result;
/* Direct window drawing: no server-side pixmap or CopyArea fallback. */
static unsigned long task_progress_drawn,task_progress_issue_color;
static int task_progress_visible,task_progress_check,task_phase_fd=-1,task_trace_fd=-1,task_trace_owned,task_progress_reused,task_progress_y;
static char task_trace_path[160];
static void task_trace_event(const char*event,const char*detail,int value){
 if(task_trace_fd<0)return;char line[512];int n=snprintf(line,sizeof line,"STAGE_UI ms=%lu event=%s value=%d detail=%s\n",ticks(),event,value,detail?detail:"");
 if(n>0&&(unsigned)n<sizeof line)(void)write(task_trace_fd,line,(size_t)n);
}
static void task_trace_export(void){
 if(task_trace_fd>=0){close(task_trace_fd);task_trace_fd=-1;}
 if(!task_trace_owned||!task_trace_path[0])return;int fd=open(task_trace_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;
 char bytes[32768];ssize_t n=read(fd,bytes,sizeof bytes);close(fd);if(n>0)launch_output("STAGE_TRACE",bytes,(size_t)n);
 fd=open("/tmp/nx-lut-lite/memory-action.log",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd>=0){n=read(fd,bytes,sizeof bytes);close(fd);if(n>0)launch_output("TASK_STDOUT",bytes,(size_t)n);}
}
static int task_progress_open(int check){
 task_progress_reused=dpy&&win&&gc;task_progress_y=task_progress_reused?288:0;
 unsigned char bytes[16];char operation[33];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);ssize_t n=fd>=0?read(fd,bytes,sizeof bytes):-1;if(fd>=0)close(fd);
 if(n!=sizeof bytes){unsigned long seed=ticks();for(unsigned i=0;i<16;i++)bytes[i]=(unsigned char)((seed>>(i%4*8))^((unsigned)getpid()>>(i%3*8))^i);}
 for(unsigned i=0;i<16;i++)snprintf(operation+i*2,3,"%02x",bytes[i]);
 if(setenv("NX51_PROGRESS_TOKEN",operation,1)||setenv("NX51_PROGRESS_UI","1",1))return 0;
 nxl_progress_view_begin(&task_progress,operation,check);task_progress.auto_init=task_progress_auto;task_progress_check=check;snprintf(task_trace_path,sizeof task_trace_path,"/tmp/nx-lut-lite/stage-ui-%s.log",operation);task_trace_fd=open(task_trace_path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);task_trace_owned=task_trace_fd>=0;task_phase_fd=np_stage_open(operation);if(task_phase_fd<0){launch_event("PROGRESS_CHANNEL_FAILED",operation,-1,errno);return 0;}
 if(task_progress_reused){
  int flags=fcntl(ConnectionNumber(dpy),F_GETFD);if(flags<0||fcntl(ConnectionNumber(dpy),F_SETFD,flags|FD_CLOEXEC))return 0;
  XSynchronize(dpy,False);xerror=0;task_progress_issue_color=color("#EF8178",ink);task_progress_visible=1;task_progress.changed=1;task_progress_drawn=0;
  launch_event("PROGRESS_REUSES_MENU_WINDOW","existing visible menu retained",(int)win,0);return 1;
 }
 dpy=XOpenDisplay(NULL);if(!dpy){launch_event("PROGRESS_XOPEN_FAILED","passive task overlay",-1,0);return 0;}
 XSetErrorHandler(ignore_error);XSynchronize(dpy,False);xerror=0;int connection_flags=fcntl(ConnectionNumber(dpy),F_GETFD);if(connection_flags<0||fcntl(ConnectionNumber(dpy),F_SETFD,connection_flags|FD_CLOEXEC)){XCloseDisplay(dpy);dpy=NULL;return 0;}palette();task_progress_issue_color=color("#EF8178",ink);int screen=DefaultScreen(dpy);
 XSetWindowAttributes attr;memset(&attr,0,sizeof attr);attr.override_redirect=True;attr.background_pixel=bg;attr.event_mask=ExposureMask;
 win=XCreateWindow(dpy,RootWindow(dpy,screen),0,288,720,192,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel|CWEventMask,&attr);
 XStoreName(dpy,win,"NX LUT Task Progress");unsigned long opaque=0xffffffffUL,hints[9]={1,0,0,0,0,0,0,0,0};
 XChangeProperty(dpy,win,XInternAtom(dpy,"_NET_WM_WINDOW_OPACITY",False),6,32,PropModeReplace,(unsigned char*)&opaque,1);
 Atom wm_hints=XInternAtom(dpy,"WM_HINTS",False);XChangeProperty(dpy,win,wm_hints,wm_hints,32,PropModeReplace,(unsigned char*)hints,9);
 canvas=win;gc=XCreateGC(dpy,win,0,NULL);XSetGraphicsExposures(dpy,gc,False);task_progress_visible=1;
 /* Match the existing menu: ordinary opaque window, map before drawing. */
 XMapRaised(dpy,win);XSync(dpy,False);
 task_progress.changed=1;task_progress_drawn=0;return 1;
}
static void task_progress_paint(void);
static void task_progress_read(void){
 for(unsigned packets=0;packets<64;packets++){
  /* Check readiness before every receive and paint before draining the next
   * packet. A burst must not suppress all its intermediate stage displays. */
  struct pollfd ready={task_phase_fd,POLLIN,0};int available=poll(&ready,1,0);
  if(available<=0||!(ready.revents&POLLIN))break;
  struct np_record record;ssize_t n=recvfrom(task_phase_fd,&record,sizeof record,MSG_DONTWAIT|MSG_TRUNC,NULL,NULL);
  if(n<0){if(errno==EINTR)continue;break;}if(n!=(ssize_t)sizeof record||!np_valid(&record)||record.version!=3||strcmp(record.operation,task_progress.operation))continue;
  task_trace_event("RECEIVED",record.phase,(int)record.pid);
  (void)nxl_progress_view_record(&task_progress,&record);
  task_progress_paint();
 }
}
static void task_progress_paint(void){
 if(!task_progress_visible||!task_progress.changed)return;
 /* This passive window reads no X events or replies while native work is
  * active. Its only repaint trigger is a real stage/terminal notification. */
 task_trace_event("RENDER_BEGIN",task_progress.title,(int)task_progress.percent);
 int y=task_progress_y;canvas=win;fill(0,y,720,192,bg);frame(12,y+6,696,182,line_color);
 unsigned long foreground=task_progress.issue?task_progress_issue_color:accent;
 XSetForeground(dpy,gc,ink);text(28,y+16,task_progress_auto?"AUTO INITIALIZE":"SRW / LUT / JPEG");text(544,y+16,"STAGE STATUS");
 XSetForeground(dpy,gc,foreground);limited_text(28,y+52,task_progress.title,660);
 XSetForeground(dpy,gc,ink);limited_text(28,y+88,task_progress.detail,660);
 fill(28,y+128,588,18,dark);fill(28,y+128,(int)(588u*task_progress.percent/100u),18,foreground);frame(28,y+128,588,18,line_color);
 char percent[24];snprintf(percent,sizeof percent,"%u%%",task_progress.percent);XSetForeground(dpy,gc,foreground);text(636,y+121,percent);
 XSetForeground(dpy,gc,muted);text(28,y+158,"STAGE CHANGES ONLY - AUTO RETURN TO EV+AEL");
 XMapRaised(dpy,win);XFlush(dpy);task_trace_event("DRAWN",task_progress.title,(int)task_progress.percent);
 if(!task_progress_drawn)launch_event("PROGRESS_WINDOW_MAPPED",task_progress.operation,task_progress_check,0);
 task_progress_drawn=1;task_progress.changed=0;
}
static int task_progress_ready(void){
 if(!task_progress_visible||!task_progress_drawn||!dpy)return 0;
 /* A dead server invokes Xlib's fatal I/O handler before a backend exists.
  * The round trip also delivers asynchronous drawable/GC errors now. */
 launch_event("PROGRESS_DRAW_SYNC_BEGIN",task_progress.operation,0,0);XSync(dpy,False);
 if(xerror){launch_event("PROGRESS_DRAW_REJECTED",task_progress.operation,-1,0);return 0;}
 launch_event("PROGRESS_DRAW_SYNC_VERIFIED",task_progress.operation,0,0);return 1;
}
static void task_progress_pump(void){task_progress_read();task_progress_paint();}
static void task_progress_complete(int confirmed,int check){nxl_progress_view_finish(&task_progress,confirmed,check);task_progress_paint();}
static void task_progress_close(void){if(task_trace_fd>=0){close(task_trace_fd);task_trace_fd=-1;}if(task_phase_fd>=0){close(task_phase_fd);task_phase_fd=-1;}if(task_progress_visible){if(!task_progress_reused){XFreeGC(dpy,gc);XDestroyWindow(dpy,win);XCloseDisplay(dpy);dpy=NULL;canvas=0;}task_progress_visible=0;}task_progress_reused=0;unsetenv("NX51_PROGRESS_UI");unsetenv("NX51_PROGRESS_TOKEN");}
static void task_progress_return(const char*root){
 char program[1200];if(!root||snprintf(program,sizeof program,"%s/bin/nxl-menu",root)>=(int)sizeof program)return;
 launch_event("PROGRESS_RETURN_TO_MENU",program,0,0);execl(program,program,"--task-result",(char*)NULL);
 launch_event("PROGRESS_MENU_EXEC_FAILED",program,-1,errno);
}
