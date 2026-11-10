/* SPDX-License-Identifier: AGPL-3.0-only
 * Opaque bitmap menu. Live LUT writes close the menu; SRW tasks use a passive overlay.
 * No AF commands, synthetic shutter events, or input grabs are used.
 */
#include "common.h"
#include "font.h"
#include "ui_font.h"
#include "look_info.h"
#include "ui_state.h"
#include <time.h>
#include <X11/Xlib.h>
#include "menu_child_watch.h"
static Display *dpy;
static Window win, previous;
static Drawable canvas;
static GC gc;
static int previous_revert, count, selected, raw_available, pointer_cell;
static volatile sig_atomic_t action, quitting;
static unsigned char down[256];
static char names[512][81], engine[1100], active_name[81];
static char demo_look[81];
static struct nxl_raw_status raw_status;
static struct nxl_progress_status progress_status;
static unsigned long progress_polled;
static struct nxl_help_state help;
static int pointer_down,video_enabled,combo_block,apply_strength=100;
static char notice[120];
static int name_ok(const char *s);
#include "browser.h"
#include "srw_catalog.h"
static int left_panel;
static pid_t video_child;static int video_command;static void video_menu_start(int);
static unsigned long bg, ink, accent, dark, button_bg;
static unsigned long line_color,muted,panel,color_tab,selection_colors[8];
static int xerror;
static int menu_lock_fd=-1,menu_input_fd[2]={-1,-1},rearm_inline,rearm_is_new;
static pid_t rearm_child;
static int memory_background(int menu_lock,int create_new);
static void start_menu_rearm(void);
static void start_menu_new_session(void);
static void task_trace_event(const char*,const char*,int);
static int poll_menu_rearm(void);
static void stop(int sig) { (void)sig; action = 0; quitting = 1; }
static int ignore_error(Display *d, XErrorEvent *e) { (void)d; (void)e; xerror = 1; return 0; }
static int name_ok(const char *s) {
    unsigned n = 0;
    for (; *s; ++s, ++n)
        if (!( (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
               (*s >= '0' && *s <= '9') || *s == '_' || *s == '-')) return 0;
    return n > 0 && n <= 80;
}
/* Diagnostics start before a workflow/job exists. Only fixed SD leaf files
 * are opened; the camera's legitimate /mnt/mmc alias is intentionally allowed. */
static FILE *launch_log_open(const char *path) {
    int fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return NULL;struct stat st;
    if(srw_fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return NULL;}
    FILE *f=fdopen(fd,"a");if(!f)close(fd);return f;
}
static void launch_log_close(FILE *f) {
    if(f){fflush(f);fsync(fileno(f));fclose(f);}
}
static void launch_event(const char *event,const char *detail,int value,int error) {
    int original_errno=errno;
    const char *paths[]={"/mnt/mmc/NXDEMO51/logs/launch.log","/mnt/mmc/NXDEMO51/logs/run.log"};
    for(unsigned i=0;i<2;i++) {
        FILE *f=launch_log_open(paths[i]);if(!f)continue;
        fprintf(f,"MENU_LAUNCH pid=%ld event=%s value=%d errno=%d detail=%s\n",(long)getpid(),event,value,error,detail?detail:"");
        launch_log_close(f);
    }
    errno=original_errno;
}
static void launch_output(const char *kind,const char *bytes,size_t n) {
    int original_errno=errno;
    const char *paths[]={"/mnt/mmc/NXDEMO51/logs/launch.log","/mnt/mmc/NXDEMO51/logs/run.log"};
    for(unsigned i=0;i<2;i++) {
        FILE *f=launch_log_open(paths[i]);if(!f)continue;
        fprintf(f,"MENU_%s_BEGIN bytes=%u\n",kind,(unsigned)n);fwrite(bytes,1,n,f);
        fputs("\nMENU_OUTPUT_END\n",f);launch_log_close(f);
    }
    errno=original_errno;
}
/* Structured status is stdout-only. Keep diagnostics on a separate SD log
 * descriptor so even successful session diagnostics cannot corrupt protocol. */
static void structured_stderr_to_sd(void) {
    FILE *f=launch_log_open("/mnt/mmc/NXDEMO51/logs/launch.log");
    if(!f)f=launch_log_open("/mnt/mmc/NXDEMO51/logs/run.log");
    if(f){dup2(fileno(f),2);fclose(f);}
    /* If neither SD log opens, retain inherited stderr rather than discard it. */
}
static pid_t backend(const char *mode, const char *name, int *result_fd) {
    int diagnostic=!strcmp(mode,"memory-test")||!strcmp(mode,"new-session")||!strcmp(mode,"prepare-full")||!strcmp(mode,"restore-camera")||!strcmp(mode,"rearm-session")||!strcmp(mode,"auto-init-test");
    int p[2];
    if (pipe(p)) {if(diagnostic)launch_event("PIPE_FAILED",mode,-1,errno);return -1;}
    pid_t c = fork();
    if (!c) {
        task_child_watch_detach();
        close(p[0]);dup2(p[1],1);
        if(!strcmp(mode,"new-session"))structured_stderr_to_sd();else dup2(p[1],2);
        close(p[1]);
        char level[8];snprintf(level,sizeof level,"%d",apply_strength);
        if(!strncmp(mode,"video-probe-",12)) {
            char script[1200];const char*root=getenv("NX_LUT_ROOT");if(!root||snprintf(script,sizeof script,"%s/video-probe.sh",root)>=(int)sizeof script)_exit(127);
            const char*verb=!strcmp(mode,"video-probe-start")?"start":!strcmp(mode,"video-probe-stop")?"stop":!strcmp(mode,"video-probe-mark")?"mark":"status";
            execl("/bin/sh","sh",script,verb,(char*)0);_exit(127);
        }
        if(!strcmp(mode,"video-status")||!strcmp(mode,"video-2k")||!strcmp(mode,"video-uhd")||!strcmp(mode,"video-full")||!strcmp(mode,"video-crop80")) {
            char script[1200];const char*root=getenv("NX_LUT_ROOT");if(!root||snprintf(script,sizeof script,"%s/video-mode.sh",root)>=(int)sizeof script)_exit(127);
            execl("/bin/sh","sh",script,!strcmp(mode,"video-2k")?"2k":!strcmp(mode,"video-uhd")?"uhd":!strcmp(mode,"video-full")?"full":!strcmp(mode,"video-crop80")?"crop80":"status",(char*)0);_exit(127);
        }
        if(!strncmp(mode,"lens-",5)) {
            char script[1200];const char *root=getenv("NX_LUT_ROOT");
            if(!root||snprintf(script,sizeof script,"%s/lens-capture.sh",root)>=(int)sizeof script)_exit(127);
            const char*verb=!strcmp(mode,"lens-start")?"start":!strcmp(mode,"lens-stop")?"stop":!strcmp(mode,"lens-mark")?"mark":"status";
            execl("/bin/sh","sh",script,verb,(char*)0);_exit(127);
        }
        if(!strcmp(mode,"new-session")) {
            const char *root=getenv("NX_LUT_ROOT");char manager[1200];
            if(!root || root[0]!='/' || snprintf(manager,sizeof manager,"%s/bin/nxl-session.arm",root)>=(int)sizeof manager){launch_event("ROOT_INVALID",root,-1,EINVAL);fputs("MENU_EXEC_ERROR invalid NX_LUT_ROOT\n",stderr);_exit(127);}
            launch_event("EXEC_ATTEMPT",manager,0,0);
            execl(manager,manager,"/mnt/mmc/NXDEMO51","new",(char*)0);
            int saved=errno;launch_event("EXEC_FAILED",manager,127,saved);fprintf(stderr,"MENU_EXEC_ERROR target=%s errno=%d\n",manager,saved);_exit(127);
        }
        if(!strcmp(mode,"memory-test") || !strcmp(mode,"prepare-full") || !strcmp(mode,"restore-camera") || !strcmp(mode,"rearm-session") || !strcmp(mode,"auto-init-test")) {
            char script[1200];snprintf(script,sizeof script,"%s",engine);
            char *slash=strrchr(script,'/');if(!slash){launch_event("SCRIPT_PATH_INVALID",engine,127,EINVAL);fputs("MENU_EXEC_ERROR missing script parent\n",stderr);_exit(127);}
            snprintf(slash+1,sizeof script-(size_t)(slash+1-script),"%s",!strcmp(mode,"auto-init-test")?"auto-init.sh":"memory-test.sh");
            launch_event("EXEC_ATTEMPT",script,0,0);
            if(!strcmp(mode,"auto-init-test"))execl("/bin/sh","sh",script,(char*)0);
            else if(!strcmp(mode,"prepare-full"))execl("/bin/sh","sh",script,"--prepare-full",(char*)0);
            else if(!strcmp(mode,"restore-camera"))execl("/bin/sh","sh",script,"--restore-camera",(char*)0);
            else if(!strcmp(mode,"rearm-session"))execl("/bin/sh","sh",script,"--rearm",(char*)0);
            else execl("/bin/sh","sh",script,name,level,(char *)0);
            int saved=errno;launch_event("EXEC_FAILED",script,127,saved);fprintf(stderr,"MENU_EXEC_ERROR shell=/bin/sh script=%s errno=%d\n",script,saved);_exit(127);
        }
        execl("/bin/sh", "sh", engine, mode, name, level, (char *)0);
        _exit(127);
    }
    close(p[1]);
    if (c < 0) {int saved=errno;close(p[0]);if(diagnostic)launch_event("BACKEND_FORK_FAILED",mode,-1,saved);return -1;}
    if(diagnostic){const char*passive=getenv("NX51_PROGRESS_UI");if(passive&&!strcmp(passive,"1"))task_trace_event("BACKEND_STARTED",mode,(int)c);else launch_event("BACKEND_STARTED",mode,(int)c,0);}
    *result_fd = p[0]; fcntl(*result_fd, F_SETFD, FD_CLOEXEC);
    return c;
}
static void load_names(void) {
    int fd;pid_t c=backend("list","",&fd);if(c<0)return;
    FILE *f=fdopen(fd,"r");char line[256];
    if(f) {while(fgets(line,sizeof line,f)) {
        line[strcspn(line,"\r\n")]=0;
        if(total_count<512 && name_ok(line))strcpy(names[total_count++],line);
    }fclose(f);}else close(fd);
    int status;while(waitpid(c,&status,0)<0 && errno==EINTR) {}
    video_enabled=access("/tmp/nx-lut-lite/session/video_enabled",F_OK)==0 || access("/tmp/nx-lut-lite/session/video_pending",F_OK)==0;
    f=fopen("/tmp/nx-lut-lite/session/current","r");
    if(f) {if(fgets(line,sizeof line,f)) {line[strcspn(line,"\r\n")]=0;if(name_ok(line))strcpy(active_name,line);}fclose(f);}
    f=fopen("/tmp/nx-lut-lite/session/strength","r");
    if(f) {int n;if(fscanf(f,"%d",&n)==1 && strength_ok(n))active_strength=n;fclose(f);}
    browser_load();browser_rebuild(cursors[category][0]?cursors[category]:active_name);
}
static unsigned long ticks(void) {
#if defined(__arm__)
    struct { int sec, nsec; } t;
    register int r0 __asm__("r0")=1;
    register int r1 __asm__("r1")=(int)&t;
    register int r7 __asm__("r7")=263; /* ARM clock_gettime, no new libc dependency */
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1),"r"(r7) : "memory","cc");
    if (r0<0) return 0;
    return (unsigned long)t.sec*1000UL+(unsigned)t.nsec/1000000UL;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (unsigned long)t.tv_sec*1000UL+(unsigned long)t.tv_nsec/1000000UL;
#endif
}
static void load_raw_status(void) {
    memset(&raw_status,0,sizeof raw_status);
    const char *base=getenv("NX_LUT_ROOT");if(!base || base[0]!='/' || strlen(base)>1000){launch_event("STATUS_ROOT_INVALID",base,-1,EINVAL);return;}
    char manager[1200];if(snprintf(manager,sizeof manager,"%s/bin/nxl-session.arm",base)>=(int)sizeof manager){launch_event("STATUS_PATH_INVALID",base,-1,EINVAL);return;}
    int channels[2];if(pipe(channels)){launch_event("STATUS_PIPE_FAILED",manager,-1,errno);return;}
    pid_t child=fork();
    if(!child) {
        close(channels[0]);dup2(channels[1],1);close(channels[1]);
        structured_stderr_to_sd();
        execl(manager,manager,"status",(char*)0);
        fprintf(stderr,"STATUS_EXEC_FAILED target=%s errno=%d\n",manager,errno);_exit(127);
    }
    close(channels[1]);if(child<0){int saved=errno;close(channels[0]);launch_event("STATUS_FORK_FAILED",manager,-1,saved);return;}
    int flags=fcntl(channels[0],F_GETFL);if(flags<0 || fcntl(channels[0],F_SETFL,flags|O_NONBLOCK)<0){int saved=errno;close(channels[0]);kill(child,SIGTERM);waitpid(child,NULL,WNOHANG);launch_event("STATUS_PIPE_SETUP_FAILED",manager,-1,saved);return;}
    char output[2048];size_t used=0;int eof=0,overflow=0,status=0,finished=0,read_error=0;
    unsigned long start=ticks();
    while(ticks()-start<500UL && !quitting) {
        ssize_t got=read(channels[0],output+used,sizeof output-1-used);
        if(got>0){used+=(size_t)got;if(used==sizeof output-1){overflow=1;break;}}
        else if(!got){eof=1;break;}
        else if(errno!=EAGAIN && errno!=EINTR){read_error=errno;break;}
        struct pollfd pipefd={channels[0],POLLIN|POLLHUP,0};poll(&pipefd,1,10);
    }
    close(channels[0]);
    pid_t done;do {done=waitpid(child,&status,WNOHANG);if(done==child){finished=1;break;}if(done<0)break;if(!eof || ticks()-start>=500UL)break;poll(NULL,0,5);}while(!finished);
    if(!finished){kill(child,SIGTERM);waitpid(child,&status,WNOHANG);}
    output[used]=0;
    if(eof && !overflow && finished && WIFEXITED(status) && !WEXITSTATUS(status))nxl_raw_status_parse(output,&raw_status);
    if(!raw_status.valid) {
        int code=!finished?-1:WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):-1;
        const char *reason=overflow?"STATUS_STDOUT_OVERFLOW":read_error?"STATUS_READ_FAILED":!finished?(quitting?"STATUS_QUERY_CANCELLED":"STATUS_QUERY_TIMEOUT"):!WIFEXITED(status)?"STATUS_QUERY_SIGNAL":WEXITSTATUS(status)?"STATUS_QUERY_EXIT":"STATUS_PROTOCOL_INVALID";
        launch_event(reason,manager,code,read_error);if(used)launch_output("STATUS_STDOUT",output,used);
    }
}
#include "../../../core/src/sha256.h"
#define NP_FSTAT srw_fstat
#include "../../../common/progress_journal.h"
/* Last complete append record; old SD replacement snapshots are not read. */
static void load_progress_status(void) {
    memset(&progress_status,0,sizeof progress_status);progress_polled=ticks();
    struct np_record record;
    if(np_last("/mnt/mmc/NXDEMO51/logs/PROGRESS.events",&record)){
        progress_status.valid=1;strcpy(progress_status.phase,record.phase);strcpy(progress_status.state,record.state);
        strcpy(progress_status.detail,record.detail);strcpy(progress_status.time,record.stamp);
    }
}
static unsigned long color(const char *name, unsigned long fallback) {
    XColor screen_color, exact;
    if (XAllocNamedColor(dpy, DefaultColormap(dpy,DefaultScreen(dpy)),name,&screen_color,&exact)) return screen_color.pixel;
    return fallback;
}
static void palette(void) {
    unsigned long white=WhitePixel(dpy,DefaultScreen(dpy));
    bg=color("#242628",white); ink=color("#F4F5F6",white);
    accent=color("#21C9EE",white); dark=color("#202326",white);
    color_tab=color("#50565B",white); button_bg=color("#35383B",white); line_color=color("#61666A",white);
    muted=color("#B8BDC1",white); panel=color("#263947",white);
    for (int i=0;i<8;++i) {
        char s[16]; snprintf(s,sizeof s,"#%02x%02x%02x",5,157-i*3,210-i*3);
        selection_colors[i]=color(s,accent);
    }
}
static void fill(int x,int y,int w,int h,unsigned long pixel) {
    XSetForeground(dpy,gc,pixel); XFillRectangle(dpy,canvas,gc,x,y,(unsigned)w,(unsigned)h);
}
static unsigned next_cp(const char **p) {
    const unsigned char *s=(const unsigned char *)*p; unsigned c=*s++;
    if (c>=0xe0 && c<=0xef && s[0] && s[1] && (s[0]&0xc0)==0x80 && (s[1]&0xc0)==0x80) {
        c=((c&15)<<12)|((s[0]&63)<<6)|(s[1]&63); s+=2;
    } else if(c>=0xc2 && c<=0xdf && s[0] && (s[0]&0xc0)==0x80) { c=((c&31)<<6)|(s[0]&63); ++s; }
    else if(c>=128) c='?';
    *p=(const char *)s; return c;
}
static void text(int x,int y,const char *s) {
    XRectangle runs[1800]; int n=0;
    while(*s && x<704) {
        const struct nxl_ui_glyph *g=nxl_ui_glyph(next_cp(&s));
        for(int row=0;row<28;++row) for(int c=0;c<26;) {
            if(!(g->row[row]&(1u<<c))) { ++c; continue; }
            int first=c; while(c<26 && (g->row[row]&(1u<<c))) ++c;
            runs[n++]=(XRectangle){(short)(x+first),(short)(y+row),(unsigned short)(c-first),1};
            if(n==1800) { XFillRectangles(dpy,canvas,gc,runs,n); n=0; }
        }
        x+=g->advance;
    }
    if(n) XFillRectangles(dpy,canvas,gc,runs,n);
}
static void limited_text(int x,int y,const char *s,int width) {
    char out[256]; unsigned n=0; int used=0; const char *p=s;
    while(*p) {
        const char *begin=p; const struct nxl_ui_glyph *g=nxl_ui_glyph(next_cp(&p));
        if((width<24 || used+g->advance>(unsigned)(width-24)) && *p) { memcpy(out+n,"...",3); n+=3; break; }
        unsigned len=(unsigned)(p-begin); if(n+len>=sizeof out-4)break;
        memcpy(out+n,begin,len); n+=len; used+=g->advance;
    }
    out[n]=0; text(x,y,out);
}
static const struct nxl_look_info *selected_info(void) {
    if(!selected) return &nxl_restore_info;
    if(selected==count+1)return &nxl_video_info;
    return info_for(selected_id());
}
static const char *row_title(int index) {
    if(!index)return nxl_restore_info.name;
    if(index==count+1)return video_enabled?"视频 LUT: ON":"视频 LUT: OFF";
    const struct nxl_look_info *info=info_for(names[view[index-1]]);
    return info==&nxl_unknown_info?names[view[index-1]]:info->name;
}
static void frame(int x,int y,int w,int h,unsigned long pixel) {
    fill(x,y,w,1,pixel); fill(x,y+h-1,w,1,pixel);fill(x,y,1,h,pixel);fill(x+w-1,y,1,h,pixel);
}
static void switch_panel(int next) {
    left_panel=(next+3)%3;notice[0]=0;srw_file_mode=0;
    if(left_panel==2)video_menu_start(0);
    pointer_cell=-1;pointer_down=0;nxl_help_reset(&help,ticks());
}
static void draw_sidebar(void) {
    fill(19,97,76,296,button_bg);fill(19,104+left_panel*95,76,88,color_tab);
    const char*names[]={"LUT","SRW","VID"};for(int i=0;i<3;i++){
        int y=112+i*95;frame(39,y,36,24,left_panel==i?ink:muted);
        if(i==0)for(int j=0;j<3;j++){fill(42+j*11,y+3,6,3,ink);fill(42+j*11,y+17,6,3,ink);}
        if(i==2)for(int j=0;j<14;j++)fill(47+j,y+5+j/3,1,14-2*(j/3),left_panel==i?ink:muted);
        XSetForeground(dpy,gc,left_panel==i?accent:muted);text(31,y+38,names[i]);
    }
}
static void publish_ui(void) {
    unsigned long visible=(unsigned long)help.visible,which=(unsigned long)left_panel;
    XChangeProperty(dpy,win,XInternAtom(dpy,"_NX_LUT_HELP_VISIBLE",False),6,32,PropModeReplace,(unsigned char *)&visible,1);
    XChangeProperty(dpy,win,XInternAtom(dpy,"_NX_LUT_LEFT_PANEL",False),6,32,PropModeReplace,(unsigned char *)&which,1);
    XFlush(dpy);
}
static void srw_navigate(int value) {
    if(srw_file_mode)srw_cursor=srw_count?(value%srw_count+srw_count)%srw_count:0;
    else srw_row=nxl_srw_ui_at(value);
    notice[0]=0;nxl_help_reset(&help,ticks());
}
#include "menu_lut_picker.inc"
#include "menu_lens.inc"
#include "menu_video.inc"
static void srw_confirm(void) {
    if(video_child>0&&video_command){strcpy(notice,"VIDEO UPDATE RUNNING - PLEASE WAIT");return;}
    if(srw_file_mode){
        if(srw_count&&srw_cursor>=0&&srw_cursor<srw_count&&srw_container_check(srw_files[srw_cursor])){
            strcpy(srw_source,srw_files[srw_cursor]);strcpy(srw_lut_id,"off");srw_file_mode=0;srw_row=0;strcpy(notice,"SOURCE SELECTED / LUT OFF");
        }else strcpy(notice,"SOURCE UNAVAILABLE - RESCAN");
        nxl_help_reset(&help,ticks());return;
    }
    if(nxl_srw_ui_index(srw_row)<0){srw_row=3;strcpy(notice,"SESSION STATUS");return;}
    if(lens_child>0){strcpy(notice,"LENS RECORDER UPDATE - PLEASE WAIT");return;}
    if(srw_row==13){unsigned old=srw_gamma_mode;srw_gamma_mode=(old+1)%3;if(!browser_save()){srw_gamma_mode=old;strcpy(notice,"GAMMA SELECTION SAVE FAILED");}else notice[0]=0;return;}
    if(srw_row>=10){lens_menu_start(srw_row==10?1:srw_row==11?2:3);return;}
    if(rearm_child>0 && srw_row>=4 && srw_row!=6){strcpy(notice,"SESSION UPDATE RUNNING - PLEASE WAIT");nxl_help_reset(&help,ticks());return;}
    if(srw_row==0){srw_scan();srw_file_mode=1;notice[0]=0;nxl_help_reset(&help,ticks());return;}
    if(srw_row==2){lut_picker_begin();return;}
    if(srw_row==1) {strcpy(notice,"SRW -> ISP LUT -> JPEG 4:2:2");nxl_help_reset(&help,ticks());return;}
    if(srw_row==9) {
        if(!srw_container_check(srw_source)){strcpy(notice,"SELECT THE NEW RAW + JPEG FIRST");return;}
        load_raw_status();
        if(!raw_status.valid)strcpy(notice,"SESSION STATUS UNAVAILABLE");
        else if(raw_status.state==NXL_RAW_FRONT_RUNNING||raw_status.state==NXL_RAW_BACKEND_RUNNING)strcpy(notice,"SESSION RUNNING - PLEASE WAIT");
        else if(raw_status.state==NXL_RAW_RESTART_REQUIRED&&!nxl_raw_can_rearm(&raw_status))strcpy(notice,"INITIALIZATION BLOCKED - CHECK LOGS");
        else {action=12;quitting=1;}
    } else if(srw_row==8) {
        if(nxl_raw_can_rearm(&raw_status))start_menu_rearm();
        else strcpy(notice,"NO HELD TASK - VIEW SESSION STATUS");
    } else if(srw_row==7) {
        if(raw_status.state==NXL_RAW_FRONT_RUNNING || raw_status.state==NXL_RAW_BACKEND_RUNNING)strcpy(notice,"SESSION RUNNING - PLEASE WAIT");
        else {action=10;quitting=1;}
    } else if(srw_row==6) {
        load_progress_status();notice[0]=0;nxl_help_reset(&help,ticks());help.visible=1;return;
    } else if(srw_row==5) {
        start_menu_new_session();
    } else if(srw_row==4) {
        if(!srw_container_check(srw_source)){strcpy(notice,"SELECT THE NEW RAW + JPEG FIRST");return;}
        if(!raw_status.valid)strcpy(notice,"SESSION STATUS UNAVAILABLE");
        else if(raw_status.state==NXL_RAW_COMPLETE)strcpy(notice,"COMPLETE - DCIM / NXLIxxxx.JPG");
        else if(nxl_raw_can_rearm(&raw_status))start_menu_rearm();
        else if(!strcmp(raw_status.reason,"FRONT_REINSTALL_REQUIRED"))strcpy(notice,"REINSTALL FULL PACKAGE FOR NEW FRONT");
        else if(raw_status.state==NXL_RAW_RESTART_REQUIRED){srw_row=3;strcpy(notice,nxl_raw_status_hint(&raw_status));nxl_help_reset(&help,ticks());return;}
        else if(raw_status.state==NXL_RAW_FRONT_RUNNING || raw_status.state==NXL_RAW_BACKEND_RUNNING)strcpy(notice,"SESSION RUNNING - PLEASE WAIT");
        else if(video_enabled)strcpy(notice,"请先关闭视频 LUT");
        else {strcpy(demo_look,"raw422");apply_strength=100;action=12;quitting=1;}
    } else strcpy(notice,srw_row==0?"FIXED SOURCE: SAM_0183.SRW":nxl_raw_status_hint(&raw_status));
    nxl_help_reset(&help,ticks());
}
static void draw_srw(void) {
    if(srw_row==2){help.visible=0;help.suppressed=1;}
    static const char *titles[]={"SELECT SRW","FORMAT","LUT PIPELINE","SESSION STATE","CONTINUE","NEW SESSION","PROGRESS / LOG","CAMERA RECOVERY","RESOURCE CHECK","AUTO + COMPARE","LENS RECORD START","LENS MARK","LENS RECORD STOP","GAMMA TEST"};
    static const char *tones[]={"SELECT THE PHOTO CAPTURED AFTER BOOT / RAW + JPEG","NV16 - KEEP ALL CHROMA ROWS / JPEG 422","APPLIED LUT SNAPSHOT - IDENTITY WHEN OFF","ONE JOB - DURABLE STATE / REPORTS","ONE START - SRW / LUT / JPEG","CREATE A NEW JOB - NO HARDWARE START","READ ONLY - CURRENT PHASE AND SAVED LOGS","LIVEVIEW ONLY - DOES NOT RESET RAW SESSION","VERIFY RETIRED BUFFERS BEFORE FRESH RAW","ORIGINAL CONFIGURATION / SRW / LUT / JPEG","READ ONLY - KEEPS RECORDING WHEN MENU CLOSES","TIMESTAMP THE CURRENT TEST STEP","SAVE EVIDENCE AND RETURN THE LOGGER LOCK","OK TO SWITCH: FACTORY / SOURCE / LIFT TEST"};
    fill(0,0,720,480,bg);XSetForeground(dpy,gc,ink);text(24,20,"SRW 显影");text(552,20,"MENU 返回");
    XSetForeground(dpy,gc,muted);text(24,60,"R10k EP + FRAME FLOW");
    frame(18,96,684,298,line_color);draw_sidebar();
    int cursor=srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row),total=srw_file_mode?srw_count:nxl_srw_ui_count();if(cursor<0){srw_row=3;cursor=nxl_srw_ui_index(srw_row);}int first=(cursor/5)*5;
    for(int row=0;row<5 && first+row<total;++row) {
        int i=first+row,y=104+row*56;
        if(i==cursor)for(int k=0;k<8;++k)fill(96,y+k*7,588,7,selection_colors[k]);
        else fill(108,y+55,568,1,line_color);
        XSetForeground(dpy,gc,ink);
        if(srw_file_mode)limited_text(122,y+14,srw_basename(srw_files[i]),530);
        else {
            i=nxl_srw_ui_at(i);
            text(122,y+14,i==4?nxl_raw_status_action(&raw_status):titles[i]);char value[120];value[0]=0;
            if(i==0)snprintf(value,sizeof value,"%s",srw_source[0]?srw_basename(srw_source):"OK / DCIM RAW + JPEG");
            if(i==1)strcpy(value,"NV16 / JPEG 4:2:2");
            if(i==13)snprintf(value,sizeof value,"%s",srw_gamma_mode==0?"FACTORY":srw_gamma_mode==1?"SOURCE 19": "LIFT TEST");
            if(i==2)snprintf(value,sizeof value,"%s",!strcmp(srw_lut_id,"off")?"OFF / ORIGINAL":srw_lut_id);
            if(i==3)snprintf(value,sizeof value,"%s",nxl_raw_status_state_name(&raw_status));
            if(i==4)strcpy(value,!raw_status.valid?"CHECK":nxl_raw_can_prepare_full(&raw_status)?"OK":raw_status.state==NXL_RAW_COMPLETE?"VIEW":raw_status.state==NXL_RAW_RESTART_REQUIRED?"CHECK":raw_status.state==NXL_RAW_FRONT_RUNNING || raw_status.state==NXL_RAW_BACKEND_RUNNING?"WAIT":!strcmp(raw_status.reason,"FRONT_REINSTALL_REQUIRED")?"CHECK":"OK");
            if(i==5)strcpy(value,"OK / CREATE ONLY");
            if(i==6)strcpy(value,"OK / VIEW STATUS");
            if(i==7)strcpy(value,raw_status.state==NXL_RAW_FRONT_RUNNING || raw_status.state==NXL_RAW_BACKEND_RUNNING?"WAIT":"OK");
            if(i==8)strcpy(value,nxl_raw_can_rearm(&raw_status)?"OK / READ ONLY":"CHECK");
            if(i==9)strcpy(value,"OK / INITIALIZE");
            if(i==10)strcpy(value,lens_recording?"ON / CONTINUE":"OFF / START");
            if(i==11)strcpy(value,"OK / TIMESTAMP");
            if(i==12)strcpy(value,lens_recording?"ON / STOP":"OFF");
            limited_text(i==4||i==7?550:325,y+14,value,i==4||i==7?130:345);
        }
    }
    if(!total){XSetForeground(dpy,gc,muted);text(122,200,"无 SRW 原片");}
    XSetForeground(dpy,gc,muted);text(24,400,"下拨 切换");
    if(srw_scan_limited && srw_file_mode)text(340,400,"仅显示部分原片");
    char page[40];snprintf(page,sizeof page,"%d / %d",total?cursor+1:0,total);text(620,400,page);
    if(notice[0]){fill(20,395,680,36,bg);XSetForeground(dpy,gc,accent);limited_text(24,400,notice,650);}
    XSetForeground(dpy,gc,ink);text(24,442,"上下 选择");text(320,442,"OK  选择");text(540,442,"MENU 返回");
    if(help.visible && srw_row==6 && !srw_file_mode) {
        fill(100,96,592,298,panel);frame(100,96,592,298,muted);XSetForeground(dpy,gc,ink);
        text(118,108,"PROGRESS / LOG");text(552,108,"MENU 关闭");fill(116,145,560,1,line_color);
        char label[120];
        snprintf(label,sizeof label,"PHASE: %s",progress_status.valid?progress_status.phase:"UNAVAILABLE");limited_text(118,154,label,550);
        snprintf(label,sizeof label,"STATE: %s",progress_status.valid?progress_status.state:"CHECK SD CARD");limited_text(118,191,label,550);
        limited_text(118,230,progress_status.valid?progress_status.detail:"No complete progress record found.",550);
        XSetForeground(dpy,gc,muted);text(118,276,"NXDEMO51 / LOGS:");
        limited_text(118,310,"PROGRESS.events / launch.log",550);limited_text(118,347,"run.log / rearm.log",550);
    } else if(help.visible) {
        int y=(cursor%5)<=2?270:100;
        fill(100,y,592,144,panel);frame(100,y,592,144,muted);XSetForeground(dpy,gc,ink);
        text(118,y+12,srw_file_mode?"SRW 原片":titles[srw_row]);text(552,y+12,"MENU 关闭");
        fill(116,y+48,560,1,line_color);XSetForeground(dpy,gc,ink);
        limited_text(118,y+57,srw_file_mode?"按 OK 选择，MENU 返回。":tones[srw_row],554);
        limited_text(118,y+92,srw_row==7?"NO JPEG OUTPUT - SESSION STATE KEPT":nxl_raw_status_hint(&raw_status),554);
    }
    publish_ui();
    draw_lut_picker();
}
static void draw(void) {
    if(left_panel==2){draw_video();return;}
    if(left_panel==1){draw_srw();return;}
    fill(0,0,720,480,bg);
    XSetForeground(dpy,gc,ink);text(24,20,"LUT 滤镜");text(552,20,"MENU 返回");
    for(int i=0;i<8;++i) {
        int x=24+i*84;fill(x,56,82,36,i==category?panel:button_bg);
        XSetForeground(dpy,gc,i==category?accent:ink);text(x+13,59,group_names[i]);
        if(i==category)fill(x,90,82,2,accent);
    }
    frame(18,96,684,298,line_color);
    draw_sidebar();
    int first=(selected/5)*5;
    for(int row=0;row<5 && first+row<count+2;++row) {
        int i=first+row,y=104+row*56;
        if(i==selected)for(int k=0;k<8;++k)fill(96,y+k*7,588,7,selection_colors[k]);
        if(i!=selected)fill(108,y+55,568,1,line_color);
        XSetForeground(dpy,gc,ink);limited_text(122,y+14,row_title(i),430);
        if(i>0 && i<=count && favorite_index(names[view[i-1]])>=0) {
            XSetForeground(dpy,gc,ink);text(98,y+14,"★");
        }
        if(i>0 && i<=count && !strcmp(names[view[i-1]],active_name)) {
            XSetForeground(dpy,gc,i==selected?ink:accent);char active[32];snprintf(active,sizeof active,"当前%d%%",active_strength);text(570,y+14,active);
        }
    }
    fill(687,103,5,281,button_bg);
    int total=count+2,thumb=total<=5?281:(281*5/total);if(thumb<18)thumb=18;
    int ty=103+(total>1?(281-thumb)*selected/(total-1):0);
    fill(687,ty,5,thumb,muted);
    char page[40];snprintf(page,sizeof page,"%d / %d",selected+1,total);
    XSetForeground(dpy,gc,muted);text(24,400,"EV 收藏");text(150,400,"左右/上拨 分类");
    char level[80];
    if(selected_id()[0])snprintf(level,sizeof level,"WiFi 强度 %d%%",effective_strength());
    else snprintf(level,sizeof level,"WiFi 强度");
    text(340,400,level);text(620,400,page);
    if(notice[0]) {fill(20,395,680,36,bg);XSetForeground(dpy,gc,accent);limited_text(24,400,notice,650);}
    if(!count && !help.visible) {XSetForeground(dpy,gc,muted);text(120,315,"此分类暂无滤镜");}
    XSetForeground(dpy,gc,ink);text(24,442,"上下 选择 / 下拨 切换");text(320,442,"OK  应用");text(540,442,"MENU 返回");
    if(help.visible) {
        int y=(selected%5)<=2?270:100;
        fill(102,y+3,592,144,dark);fill(100,y,592,144,panel);frame(100,y,592,144,muted);
        XSetForeground(dpy,gc,ink);limited_text(118,y+12,row_title(selected),390);text(552,y+12,"MENU 关闭");
        fill(116,y+48,560,1,line_color);
        const struct nxl_look_info *info=selected_info();
        XSetForeground(dpy,gc,ink);limited_text(118,y+57,info->tone,554);limited_text(118,y+92,info->scene,554);
    }
    publish_ui();
}
static void navigate(int value) {
    selected=(value+count+2)%(count+2);notice[0]=0;nxl_help_reset(&help,ticks());
}
static int confirm_action(void) {
    if(selected==count+1)return 4;
    if(selected && video_enabled)return 5;
    apply_strength=effective_strength();return selected?1:2;
}
static void ui_control(int command) {
    notice[0]=0;
    if(command==0)browser_group(category+1);
    else if(command==1) {
        int result=browser_favorite();
        if(result<0)strcpy(notice,"收藏保存失败，请检查存储卡");
        else if(!result)strcpy(notice,"请选择滤镜后收藏");
    } else {
        if(!selected_id()[0])strcpy(notice,"请选择滤镜后调整强度");
        else if(browser_has_strengths())strength=strength==100?25:strength+25;
        else strcpy(notice,"此滤镜仅支持 100% 强度");
    }
    nxl_help_reset(&help,ticks());
}
static void key(unsigned code,int value) {
    if(code>=sizeof down || value<0 || value>2)return;
    /* DEL belongs to the native camera: hand off immediately, no LUT action. */
    if(code==83 && value==1) { action=0;quitting=1;return; }
    if(lut_picker_key(code,value))return;
    if(value==1)down[code]=1;
    if(down[NXL_EV] && (down[NXL_AEL] || down[NXL_WIFI]))combo_block=1;
    int category_step=nxl_category_step(code),item_step=nxl_item_step(code),side_step=nxl_sidebar_step(code);
    if(value==1 || value==2) {
        if(side_step)switch_panel(left_panel+side_step);
        else if(left_panel==2 && item_step)video_navigate(video_row+item_step);
        else if(left_panel==1 && item_step)srw_navigate((srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row))+item_step);
        else if(!left_panel && category_step) {
            browser_group(category+category_step);notice[0]=0;nxl_help_reset(&help,ticks());
        } else if(!left_panel && item_step) navigate(selected+item_step);
    }
    if(value!=0 || !down[code])return;
    down[code]=0;
    int blocked=combo_block;
    if(!down[NXL_EV] && !down[NXL_AEL] && !down[NXL_WIFI])combo_block=0;
    if(!blocked && code==NXL_EV && !left_panel)ui_control(1);
    if(!blocked && code==NXL_WIFI && !left_panel)ui_control(2);
    if(code==96) {if(left_panel==2)video_confirm();else if(left_panel==1)srw_confirm();else if(video_child>0&&video_command)strcpy(notice,"VIDEO UPDATE RUNNING - PLEASE WAIT");else if(rearm_child>0)strcpy(notice,"RESET CHECK RUNNING - PLEASE WAIT");else {action=confirm_action();quitting=1;} }
    /* Fn has no diagnostic dispatch in the normal LUT menu. */
    if(code==127) {
        if(help.visible)nxl_help_close(&help);
        else if(left_panel==1 && srw_file_mode){srw_file_mode=0;nxl_help_reset(&help,ticks());}
        else quitting=1;
    }
    if(code==NXL_S1 || code==126)quitting=1;
}

static void restore_and_close(void) {
    Window current; int revert;
    XGetInputFocus(dpy, &current, &revert); XSync(dpy, False);
    /* Playback/native UI may already have taken focus. Do not steal it back. */
    if (current == win) {
        xerror = 0;
        XSetInputFocus(dpy, previous ? previous : PointerRoot, previous_revert, CurrentTime);
        XSync(dpy, False);
        if (xerror) { XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime); XSync(dpy, False); }
    }
    XUnmapWindow(dpy, win); XSync(dpy, False);
    XFreeGC(dpy, gc); XDestroyWindow(dpy, win); XCloseDisplay(dpy); dpy = NULL;
}
static void toast_timed(const char *message, int success, unsigned long duration) {
    /* Opaque banner, drawn directly like the menu. Expose redraws its text;
     * never allocate a backing pixmap or take native focus. */
    dpy = XOpenDisplay(NULL);
    if (!dpy) return;
    XSetErrorHandler(ignore_error); palette();
    XSetWindowAttributes attr; memset(&attr, 0, sizeof attr);
    int screen = DefaultScreen(dpy);
    attr.override_redirect = True; attr.background_pixel = bg;
    attr.event_mask=ExposureMask;
    win = XCreateWindow(dpy, RootWindow(dpy, screen), 0, 408, 720, 72, 0,
        CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect|CWBackPixel|CWEventMask, &attr);
    XStoreName(dpy, win, "NX LUT Status");
    unsigned long opaque=0xffffffffUL,hints[9]={1,0,0,0,0,0,0,0,0};
    Atom wm_hints=XInternAtom(dpy,"WM_HINTS",False);
    XChangeProperty(dpy,win,XInternAtom(dpy,"_NET_WM_WINDOW_OPACITY",False),6,32,PropModeReplace,(unsigned char *)&opaque,1);
    XChangeProperty(dpy,win,wm_hints,wm_hints,32,PropModeReplace,(unsigned char *)hints,9);
    canvas=win;gc=XCreateGC(dpy,canvas,0,NULL);XSetGraphicsExposures(dpy,gc,False);
    XMapRaised(dpy,win);XSync(dpy,False);
    fill(0,0,720,72,bg); XSetForeground(dpy,gc,success ? accent : ink);
    text(24,24,message);XSync(dpy,False);
    unsigned long start=ticks();
    struct pollfd socket={ConnectionNumber(dpy),POLLIN,0};
    while(ticks()-start<duration) {
        while(XPending(dpy)) {
            XEvent e;XNextEvent(dpy,&e);
            if(e.type==Expose){canvas=win;fill(0,0,720,72,bg);XSetForeground(dpy,gc,success?accent:ink);text(24,24,message);}
        }
        XFlush(dpy);
        unsigned long elapsed=ticks()-start;
        if(elapsed>=duration)break;
        int remain=(int)(duration-elapsed);
        poll(&socket,1,remain>100?100:remain);
    }
    XFreeGC(dpy,gc);XDestroyWindow(dpy,win);
    XCloseDisplay(dpy);dpy=NULL;canvas=0;
}
#include "menu_progress.inc"
static void toast(const char *message, int success) {
    toast_timed(message,success,success?900:1800);
}
static int run_action(void) {
    if(action==5) {toast("VIDEO LUT ON - DISABLE IT FIRST",0);return 0;}
    if(action==4 && !video_enabled && !active_name[0]) {
        toast("APPLY A LUT FIRST",0);return 1;
    }
    int tracing = action == 3;
    if (tracing) toast("TRACE 20S - NORMAL CAMERA VIEW",1);
    int fd; pid_t c = backend(action == 1 ? "apply" : tracing ? "trace" : action==4 ? "video-toggle" : "off", action == 1 ? selected_id() : "", &fd);
    if (c < 0) return 1;
    /* This process has no menu window/X connection during the transaction. */
    FILE *log = fopen(tracing ? "/tmp/nx-lut-lite/trace-action.log" : "/tmp/nx-lut-lite/last-action.log", "w");
    char b[256]; ssize_t n;
    while ((n = read(fd, b, sizeof b)) != 0) {
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (log) fwrite(b,1,(unsigned)n,log);
    }
    close(fd);
    int status=0; pid_t done;
    do { done=waitpid(c,&status,0); } while (done<0 && errno==EINTR);
    int rc=done==c && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    if (log) {
        fputs(tracing ? "\nMENU: trace finished; previous LUT action log preserved.\n" : rc==0 ? (action==4 ? "\nMENU: video trial toggled; start a NEW recording to test.\n" : action==2 ? "\nMENU: saved LUT state restored; mod remains installed. Physical half-press reloads it.\n" : "\nMENU: finished; physical half-press loads live preview and JPEG LUT.\n") :
            "\nMENU: backend failed; see messages above.\n",log); fclose(log);
    }
    if(action==4 && rc==0) {
        video_enabled=access("/tmp/nx-lut-lite/session/video_enabled",F_OK)==0;
        toast(video_enabled?"VIDEO LUT ON - START NEW RECORDING":"VIDEO LUT OFF - PHOTO LUT KEPT",1);
        return 0;
    }
    toast(tracing ? (rc==0 ? "TRACE SAVED - SD AF-TRACE FOLDER" : rc==10 ? "CSV SAVED - SNAPSHOT INCOMPLETE" : "TRACE FAILED - SD AF-TRACE FOLDER") :
        (rc==0 ? (action==2 ? "LUT OFF - SAVED STATE RESTORED" : "READY - HALF-PRESS THEN SHOOT") : "FAILED - CHECK LAST-ACTION.LOG"),rc==0);
    return rc;
}
static int trace_background(int menu_lock) {
    pid_t child=fork();
    if(child<0)return 1;
    if(!child) {
        /* Release the inherited frontend lock before waiting for the trace.
         * Backend operations retain their own locks; only trace is detached. */
        close(menu_lock);
        signal(SIGHUP,SIG_IGN);
        int null=open("/dev/null",O_RDWR);
        if(null>=0) {
            dup2(null,0);dup2(null,1);dup2(null,2);
            if(null>2)close(null);
        }
        int trace_lock=nxl_lock("/tmp/nx-lut-lite/trace-ui.lock");
        if(trace_lock<0) { toast("TRACE ALREADY RUNNING",1);_exit(0); }
        FILE *p=fopen("/tmp/nx-lut-lite/trace-worker.pid","w");
        if(p) {fprintf(p,"%ld\n",(long)getpid());fclose(p);}
        action=3;
        int rc=run_action();
        unlink("/tmp/nx-lut-lite/trace-worker.pid");
        close(trace_lock);_exit(rc);
    }
    return 0;
}
/* Stage receiver/renderer are defined above this lifetime loop. */
#include "menu_task_wait.inc"
#include "menu_auto_init.inc"
static int memory_background(int menu_lock,int create_new) {
    int inline_check=(create_new==4 || create_new==1) && rearm_inline;
    int foreground=(create_new==0||create_new==5)&&dpy&&win&&gc;
    const char *root=getenv("NX_LUT_ROOT");char requested[2400];
    const char *mode=create_new==5?"auto-init-test":create_new==4?"rearm-session":create_new==3?"restore-camera":create_new==2?"prepare-full":create_new==1?"new-session":"memory-test";
    snprintf(requested,sizeof requested,"operation=%s NX_LUT_ROOT=%s script=%s/%s",mode,root?root:"(unset)",root?root:"(unset)",create_new==5?"auto-init.sh":create_new==1?"bin/nxl-session.arm":"memory-test.sh");
    launch_event("REQUESTED",requested,create_new,0);
    pid_t child=foreground?0:fork();if(child<0){int saved=errno;launch_event("BACKGROUND_FORK_FAILED",requested,-1,saved);if(!inline_check)toast_timed("STOPPED - CHECK NXDEMO51/LOGS",0,7000);return 1;}
    if(!child) {
        if(create_new!=0&&create_new!=5)close(menu_lock);else if(fcntl(menu_lock,F_SETFD,FD_CLOEXEC)){launch_event("TASK_MENU_LEASE_FAILED",requested,-1,errno);if(foreground)return 69;_exit(69);}
        quitting=0;action=0;if(!foreground){signal(SIGHUP,SIG_IGN);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);}
        if(inline_check){
            /* Close only this fork's descriptors. XCloseDisplay would send
             * protocol on the parent's still-live X connection. */
            if(dpy){close(ConnectionNumber(dpy));dpy=NULL;}
            for(unsigned i=0;i<2;i++)if(menu_input_fd[i]>=0)close(menu_input_fd[i]);
            if(setenv("NX51_MENU_REARM_INLINE","1",1))_exit(1);
        }else unsetenv("NX51_MENU_REARM_INLINE");
        int show_progress=create_new==0||create_new==5;
        if(show_progress){
            if(!srw_source[0]||setenv("NX51_SOURCE_SRW",srw_source,1)){launch_event("SRW_SELECTION_MISSING",requested,-1,errno);if(foreground)return 69;_exit(69);}
            task_progress_auto=create_new==5;task_auto_result=0;
            if(create_new==0||create_new==5){char gamma[2]={(char)('0'+srw_gamma_mode),0};if(setenv("NX51_SRW_LUT",srw_lut_id,1)||setenv("NX51_GAMMA_MODE",gamma,1)){if(foreground)return 69;_exit(69);}}
            if(!task_progress_open(create_new==4)){launch_event("PROGRESS_SETUP_FAILED_NO_BACKEND",requested,-1,0);task_progress_close();if(foreground)return 69;_exit(69);}
            if(create_new==5){strcpy(task_progress.title,"Checking automatic initialization");strcpy(task_progress.detail,"Reuse live ready state or run original cold configuration");}
            task_progress_paint();
            if(!task_progress_ready()){launch_event("PROGRESS_DRAW_FAILED_NO_BACKEND",requested,-1,0);nxl_progress_view_finish(&task_progress,0,create_new==4);task_progress_close();if(foreground)return 69;task_progress_return(root);_exit(69);}
        }
        launch_event("BACKGROUND_ENTER",requested,create_new,0);
        int null=foreground?-1:open("/dev/null",O_RDWR);if(null>=0){dup2(null,0);dup2(null,1);dup2(null,2);if(null>2)close(null);}
        int job_lock=nxl_lock("/tmp/nx-lut-lite/memory-ui.lock");
        if(job_lock<0){launch_event("UI_LOCK_BUSY_OR_FAILED","/tmp/nx-lut-lite/memory-ui.lock",-1,errno);if(show_progress){task_progress_complete(0,create_new==4);poll(NULL,0,1000);task_progress_close();if(foreground)return 75;task_progress_return(root);}else if(!inline_check)toast_timed("BUSY - CHECK NXDEMO51/LOGS",0,5000);_exit(75);}
        launch_event("UI_LOCK_ACQUIRED","/tmp/nx-lut-lite/memory-ui.lock",0,0);
        if(show_progress&&!task_child_watch_open()){launch_event("CHILD_WATCH_FAILED_NO_BACKEND",requested,-1,errno);task_progress_complete(0,0);task_progress_close();close(job_lock);if(foreground)return 69;task_progress_return(root);_exit(69);}
        int fd;pid_t worker=backend(mode,create_new==1?"":demo_look,&fd);
        if(worker<0){launch_event("BACKEND_START_FAILED",requested,-1,errno);task_child_watch_close();close(job_lock);if(show_progress){task_progress_complete(0,create_new==4);poll(NULL,0,1000);task_progress_close();if(foreground)return 1;task_progress_return(root);}else if(!inline_check)toast_timed("STOPPED - CHECK NXDEMO51/LOGS",0,7000);_exit(1);}
        FILE *log=fopen("/tmp/nx-lut-lite/memory-action.log","w");
        FILE *sd_launch=show_progress?NULL:launch_log_open("/mnt/mmc/NXDEMO51/logs/launch.log");
        FILE *sd_run=show_progress?NULL:launch_log_open("/mnt/mmc/NXDEMO51/logs/run.log");
        if(sd_launch)fprintf(sd_launch,"MENU_CHILD_OUTPUT_BEGIN worker=%ld\n",(long)worker);
        if(sd_run)fprintf(sd_run,"MENU_CHILD_OUTPUT_BEGIN worker=%ld\n",(long)worker);
        char b[256],created_output[2048];size_t created_bytes=0;ssize_t n;
        int status=0;pid_t done=show_progress?task_wait_backend(worker,fd,log,&status):0;
        if(!show_progress)for(;;) {
            n=read(fd,b,sizeof b);if(!n)break;
            if(n<0){if(errno==EINTR)continue;if(show_progress&&(errno==EAGAIN||errno==EWOULDBLOCK)){struct pollfd io={fd,POLLIN|POLLHUP,0};poll(&io,1,200);continue;}launch_event("CHILD_OUTPUT_READ_FAILED",requested,-1,errno);break;}
            if(log)fwrite(b,1,(size_t)n,log);
            if(sd_launch){fwrite(b,1,(size_t)n,sd_launch);fflush(sd_launch);}
            if(sd_run){fwrite(b,1,(size_t)n,sd_run);fflush(sd_run);}
            if(create_new==1 && created_bytes<sizeof created_output-1){size_t take=(size_t)n;if(take>sizeof created_output-1-created_bytes)take=sizeof created_output-1-created_bytes;memcpy(created_output+created_bytes,b,take);created_bytes+=take;}
        }
        if(!show_progress)close(fd);if(log)fclose(log);
        if(sd_launch)fputs("\nMENU_CHILD_OUTPUT_END\n",sd_launch);if(sd_run)fputs("\nMENU_CHILD_OUTPUT_END\n",sd_run);
        launch_log_close(sd_launch);launch_log_close(sd_run);
        if(!show_progress)do{done=waitpid(worker,&status,0);}while(done<0&&errno==EINTR);
        int rc=done!=worker?1:WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):1;
        launch_event(done!=worker?"WAIT_FAILED":WIFSIGNALED(status)?"BACKEND_SIGNAL":"BACKEND_EXIT",requested,done!=worker?-1:WIFSIGNALED(status)?WTERMSIG(status):rc,done!=worker?errno:0);
        if(show_progress){
            task_progress_pump();load_raw_status();
            int confirmed=!rc&&raw_status.valid&&(create_new==4?raw_status.state==NXL_RAW_NEED_STILL_INIT:raw_status.state==NXL_RAW_COMPLETE&&raw_status.front_verified);
            if(create_new==5){task_auto_result=auto_init_menu_result(rc);auto_init_progress_complete(task_auto_result);}
            else task_progress_complete(confirmed,create_new==4);
            unsigned long until=ticks()+2000UL;while(ticks()<until){task_progress_paint();poll(NULL,0,100);}
            task_trace_export();task_progress_close();close(job_lock);if(foreground)return rc;task_progress_return(root);_exit(rc);
        }
        /* The action owns this lock until its exact child is reaped. Result
         * presentation must not hold it for another 7-12 seconds: a user may
         * already have opened the menu and requested the next resource check. */
        if(done==worker){close(job_lock);job_lock=-1;launch_event("UI_LOCK_RELEASED","/tmp/nx-lut-lite/memory-ui.lock",rc,0);}
        if(inline_check){ /* Parent refreshes status and redraws after waitpid. */ }
        else if(rc)toast_timed(create_new==4?"RESET NOT PROVEN - CHECK REARM LOG":create_new==3?"RECOVERY UNCONFIRMED - CHECK LOGS":"STOPPED - CHECK NXDEMO51/LOGS",0,create_new==3||create_new==4?12000:7000);
        else if(create_new==4)toast_timed("SESSION READY - BOOT INIT CAN BE REUSED",1,12000);
        else if(create_new==1){struct nxl_raw_status created;created_output[created_bytes]=0;if(nxl_raw_status_parse(created_output,&created) && created.state==NXL_RAW_RESTART_REQUIRED)toast_timed("HARDWARE HOLD - CHECK NXDEMO51/LOGS",0,12000);else toast_timed("NEW JOB CREATED - OPEN SESSION STATUS",1,10000);}
        if(job_lock>=0)close(job_lock);_exit(rc);
    }
    if(inline_check)rearm_child=child;
    launch_event("BACKGROUND_STARTED",requested,(int)child,0);return 0;
}
/* Kept in a separate include so tests execute this actual lifecycle. */
#include "menu_rearm.inc"
static int inspect(void) {
    printf("NX LUT Lite: read-only input/focus inspection\n");
    printf("NX_LUT_MENU_VERSION=%s LUT_DESCRIPTIONS=%u\n",NXL_DEMO_VERSION,
           (unsigned)(sizeof nxl_look_infos/sizeof nxl_look_infos[0]));
    for (int i=0;i<2;++i) {
        int fd=nxl_input(i);
        printf("INPUT%d fd=%d EV=%d AEL=%d WIFI=%d OK=%d S1=%d S2=%d\n",i,fd,
            nxl_held(fd,NXL_EV),nxl_held(fd,NXL_AEL),nxl_held(fd,NXL_WIFI),nxl_held(fd,96),nxl_held(fd,NXL_S1),nxl_held(fd,126));
        if (fd>=0) close(fd);
    }
    Display *d=XOpenDisplay(NULL);
    if (!d) { puts("X: unavailable"); return 0; }
    Window focus; int revert; char *name=NULL;
    char keymap[32] = {0}; XQueryKeymap(d,keymap);
    unsigned codes[6] = {NXL_EV,NXL_AEL,NXL_WIFI,96,NXL_S1,126};
    printf("XKEY EV AEL WIFI OK S1 S2:");
    for (int i=0;i<6;++i) {
        unsigned k=codes[i]+8;
        printf(" %d",((unsigned char)keymap[k/8]>>(k%8))&1);
    }
    puts("");
    XSetErrorHandler(ignore_error); XGetInputFocus(d,&focus,&revert);
    if (focus>PointerRoot) XFetchName(d,focus,&name);
    printf("FOCUS id=0x%lx revert=%d name=%s\n",focus,revert,name ? name : "(none)");
    if (name) XFree(name);
    XCloseDisplay(d); return 0;
}
int main(int argc,char **argv) {
    /* Installer probe: no firmware, X11, input, lock or backend access. */
    if(argc>1 && !strcmp(argv[1],"--version")) {
        puts("NX_LUT_MENU_VERSION=" NXL_DEMO_VERSION);
        puts("MENU_STATUS_REVISION=STDOUT_STATUS_ONLY_R3");
        puts("MENU_TEST_REVISION=MENU_FRESH_FULL_TEST_R4");
        puts("MENU_CAMERA_RECOVERY=CAMERA_RECOVERY_R5");
        puts("MENU_SESSION_REARM=RESOURCE_REARM_R7; INLINE_STATUS_REFRESH_R7E; INLINE_NEW_SESSION_R8A");
        puts("MENU_TASK_PROGRESS=STAGE_NOTIFICATIONS_R8I_BLC; NO_LIVE_TIMER; SRW_FAVORITES_PICKER");
        puts("MENU_AUTO_INIT=EXPLICIT_EP_LIFECYCLE_TEST_R8I_BLC; STILL_COLD_IS_NOT_READY");
        printf("LUT_DESCRIPTIONS=%u\n",(unsigned)(sizeof nxl_look_infos/sizeof nxl_look_infos[0]));
        return 0;
    }
    if (!nxl_supported()) { fputs("NX500 1.12 required\n",stderr); return 1; }
    if (!getenv("DISPLAY")) setenv("DISPLAY",":0",0);
    if(argc>1 && !strcmp(argv[1],"--demo-camera-restoring")){toast_timed("RESTORING OEM CAMERA - PLEASE WAIT",1,5000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-camera-restored")){toast_timed("REAL OEM LIVEVIEW RESTORED",1,10000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-camera-restore-failed")){toast_timed("RECOVERY UNCONFIRMED - CHECK LOGS",0,12000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-full-ready")){toast_timed("FULL TEST READY - PREPARE RAW",1,12000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-rearm-checking")){toast_timed("CHECKING RAW RESOURCES - PLEASE WAIT",1,5000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-rearm-ready")){toast_timed("SESSION READY - BOOT INIT CAN BE REUSED",1,12000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-rearm-held")){toast_timed("RESET NOT PROVEN - CHECK REARM LOG",0,12000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-ready")){toast_timed("DEMO51 SRW / LUT / JPEG - PLEASE WAIT",1,5000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-post")){toast_timed("NATIVE LUT / JPEG 422 - PLEASE WAIT",1,5000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-front-saved")){toast_timed("YUV READY - CONTINUING LUT / JPEG",1,12000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-done")){toast_timed("NATIVE.JPG SAVED - WORKSPACE RETURN VERIFIED",1,7000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-done-cached")){toast_timed("NATIVE.JPG SAVED - CACHED YUV / RESTART",1,7000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-retryable")){toast_timed("SAFE STOP - PREPARE CAMERA / RETRY STEP",0,10000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-restart-required")){toast_timed("HARDWARE HOLD - CHECK NXDEMO51/LOGS",0,12000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-error")){toast_timed("DEMO51 STOPPED - OPEN SESSION STATUS",0,7000);return 0;}
    if(argc>1 && !strcmp(argv[1],"--demo-busy")){toast_timed("DEMO51 SESSION BUSY",0,5000);return 0;}
    if (argc>1 && !strcmp(argv[1],"--inspect")) return inspect();
    int video_shortcut=argc>1 && !strcmp(argv[1],"--video-toggle");
    int lock=nxl_lock("/tmp/nx-lut-lite/menu.lock");
    if (lock<0) {launch_event("MENU_LOCK_BUSY",argc>1?argv[1]:"open",0,errno);
        if(video_shortcut) {toast("EXIT LUT MENU FIRST",0);return 1;}
        if (argc>1 && !strcmp(argv[1],"--toggle")) {
            /* A tracked operation owns the menu lease until exec reopens it.
             * Shortcut presses must not terminate its passive progress UI. */
            int task=nxl_lock("/tmp/nx-lut-lite/memory-ui.lock");
            if(task<0){launch_event("TASK_MENU_ALREADY_ACTIVE","progress window owns the menu",0,0);return 0;}
            close(task);
            FILE *p=fopen("/tmp/nx-lut-lite/menu.pid","r"); long pid;
            if (p) { if (fscanf(p,"%ld",&pid)==1 && pid>1) kill((pid_t)pid,SIGTERM); fclose(p); }
        }
        return 0;
    }
    const char *base=getenv("NX_LUT_ROOT");
    if (!base || base[0]!='/' || strlen(base)>1000) return 2;
    snprintf(engine,sizeof engine,"%s/lut.sh",base);
    signal(SIGTERM,stop); signal(SIGINT,stop); signal(SIGHUP,stop); signal(SIGCHLD,SIG_DFL);
    if(video_shortcut) {
        video_enabled=access("/tmp/nx-lut-lite/session/video_enabled",F_OK)==0 ||
                      access("/tmp/nx-lut-lite/session/video_pending",F_OK)==0;
        FILE *current=fopen("/tmp/nx-lut-lite/session/current","r");
        if(current) {if(!fgets(active_name,sizeof active_name,current))active_name[0]=0;fclose(current);}
        action=4;int rc=run_action();close(lock);return rc;
    }
    FILE *p=fopen("/tmp/nx-lut-lite/menu.pid","w");
    if (p) { fprintf(p,"%ld\n",(long)getpid()); fclose(p); }
    load_raw_status();
    load_progress_status();
    load_names();
    if(NXL_LENS_TEST_UI)lens_menu_start(0);
    if(argc>1&&!strcmp(argv[1],"--task-result")){left_panel=1;srw_row=3;snprintf(notice,sizeof notice,"%s",raw_status.valid?nxl_raw_status_hint(&raw_status):"SESSION STATUS UNAVAILABLE");}
    if (quitting) {launch_event("MENU_START_CANCELLED",argc>1?argv[1]:"open",0,0);unlink("/tmp/nx-lut-lite/menu.pid"); return 0; }
    struct pollfd fds[3]={{-1,POLLIN,0},{nxl_input(0),POLLIN,0},{nxl_input(1),POLLIN,0}};
    raw_available=fds[1].fd>=0 && fds[2].fd>=0;
    menu_lock_fd=lock;menu_input_fd[0]=fds[1].fd;menu_input_fd[1]=fds[2].fd;
    if (raw_available) {
        int held=0;
        for (int tries=0;tries<100;++tries) {
            held=0;
            for (int i=1;i<=2;++i)
                if (nxl_held(fds[i].fd,NXL_EV)==1 || nxl_held(fds[i].fd,NXL_AEL)==1) held=1;
            if (!held || quitting) break;
            poll(NULL,0,20);
        }
        if (held || quitting) { fputs("MENU_SKIP: EV/AEL still held or cancelled\n",stderr); return 4; }
        poll(NULL,0,80);
        for (int i=1;i<=2;++i) { struct nxl_event e; while (read(fds[i].fd,&e,sizeof e)==sizeof e) {} }
    } else {
        for (int i=1;i<=2;++i) { if (fds[i].fd>=0) close(fds[i].fd); fds[i].fd=-1; }
        menu_input_fd[0]=menu_input_fd[1]=-1;
    }
    dpy=XOpenDisplay(NULL);
    if (!dpy) { fputs("Cannot open camera X display\n",stderr); return 3; }
    XSetErrorHandler(ignore_error); XGetInputFocus(dpy,&previous,&previous_revert);
    int screen=DefaultScreen(dpy);
    if (DisplayWidth(dpy,screen)<720 || DisplayHeight(dpy,screen)<480) { XCloseDisplay(dpy); return 3; }
    palette();
    XSetWindowAttributes attr; memset(&attr,0,sizeof attr);
    attr.override_redirect=True; attr.background_pixel=bg;
    attr.event_mask=ExposureMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|FocusChangeMask;
    win=XCreateWindow(dpy,RootWindow(dpy,screen),0,0,720,480,0,CopyFromParent,InputOutput,CopyFromParent,
        CWOverrideRedirect|CWBackPixel|CWEventMask,&attr);
    XStoreName(dpy,win,"NX LUT Lite");
    unsigned long opaque=0xffffffffUL;
    XChangeProperty(dpy,win,XInternAtom(dpy,"_NET_WM_WINDOW_OPACITY",False),6,32,PropModeReplace,(unsigned char *)&opaque,1);
    canvas=win;gc=XCreateGC(dpy,win,0,NULL); XSetGraphicsExposures(dpy,gc,False);
    XMapRaised(dpy,win); XSync(dpy,False); XSetInputFocus(dpy,win,RevertToPointerRoot,CurrentTime);
    fds[0].fd=ConnectionNumber(dpy); nxl_help_reset(&help,ticks()); draw();launch_event("MENU_WINDOW_SHOWN",argc>1?argv[1]:"open",0,0);
    for(;;){while (!quitting) {
        while (XPending(dpy) && !quitting) {
            XEvent ev; XNextEvent(dpy,&ev);
            if (ev.type==Expose) draw();
            if(lut_picker_pointer(&ev)){if(!quitting)draw();continue;}
            if ((ev.type==KeyPress || ev.type==KeyRelease) && !raw_available && ev.xkey.keycode>=8) {
                key(ev.xkey.keycode-8,ev.type==KeyPress ? 1 : 0); if (!quitting) draw();
            }
            if (ev.type==FocusOut) {launch_event("MENU_FOCUS_OUT",argc>1?argv[1]:"open",ev.xfocus.mode,ev.xfocus.detail);action=0;quitting=1;}
            if (ev.type==ButtonPress) {
                pointer_down=1;pointer_cell=-1;
                if(ev.xbutton.button==4 || ev.xbutton.button==5) {
                    int step=ev.xbutton.button==4?-1:1;
                    if(left_panel==2)video_navigate(video_row+step);
                    else if(left_panel==1)srw_navigate((srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row))+step);
                    else navigate(selected+step);
                }
                else if(ev.xbutton.button==1) {
                    int x=ev.xbutton.x,y=ev.xbutton.y;
                    int cursor=left_panel==2?video_row:left_panel==1?(srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row)):selected;
                    int hy=(cursor%5)<=2?270:100;
                    if(x>=19 && x<95 && nxl_sidebar_at(y)>=0)pointer_cell=30+nxl_sidebar_at(y);
                    else if(!left_panel && y>=56 && y<92 && x>=24 && x<696)pointer_cell=10+(x-24)/84;
                    else if(help.visible && x>=100 && x<692 && y>=hy && y<hy+144) {
                        if(x>=540 && y<hy+48)pointer_cell=1;
                    } else if(x>=96 && x<687 && y>=104 && y<384) {
                        int i=left_panel==2?(y-104)/40:(cursor/5)*5+(y-104)/56;
                        if(left_panel==2){if(i<7)video_navigate(i);}
                        else if(left_panel==1){if(srw_file_mode){if(i<srw_count)srw_navigate(i);}else{int hit=nxl_srw_ui_touch(cursor,y);if(hit>=0)srw_navigate(hit);}}
                        else if(i<count+2)navigate(i);
                    } else if(!left_panel && y>=395 && y<433 && x<150)pointer_cell=20;
                    else if(!left_panel && y>=395 && y<433 && x<330)pointer_cell=21;
                    else if(!left_panel && y>=395 && y<433 && x<590)pointer_cell=22;
                    else if(y>=435 && x>=310 && x<500)pointer_cell=2;
                    else if((y>=435 && x>=530) || (y<60 && x>=540))pointer_cell=3;
                }
                draw();
            }
            if(ev.type==ButtonRelease) {
                pointer_down=0;
                if(ev.xbutton.button==1) {
                    int x=ev.xbutton.x,y=ev.xbutton.y;
                    int cursor=left_panel==2?video_row:left_panel==1?(srw_file_mode?srw_cursor:nxl_srw_ui_index(srw_row)):selected,hy=(cursor%5)<=2?270:100;
                    if(pointer_cell>=30 && pointer_cell<=32 && x>=19 && x<95 && nxl_sidebar_at(y)>=0 && pointer_cell==30+nxl_sidebar_at(y))switch_panel(pointer_cell-30);
                    if(pointer_cell>=10 && pointer_cell<18 && y>=56 && y<92 && x>=24 && x<696 && pointer_cell==10+(x-24)/84) {
                        browser_group(pointer_cell-10);notice[0]=0;nxl_help_reset(&help,ticks());
                    }
                    if(pointer_cell>=20 && pointer_cell<=22 && y>=395 && y<433)
                        ui_control(pointer_cell==20?1:pointer_cell==21?0:2);
                    if(pointer_cell==1 && help.visible && x>=540 && y>=hy && y<hy+48)nxl_help_close(&help);
                    if(pointer_cell==2 && y>=435 && x>=310 && x<500){if(left_panel==2)video_confirm();else if(left_panel==1)srw_confirm();else if(video_child>0&&video_command)strcpy(notice,"VIDEO UPDATE RUNNING - PLEASE WAIT");else if(rearm_child>0)strcpy(notice,"RESET CHECK RUNNING - PLEASE WAIT");else {action=confirm_action();quitting=1;}}
                    if(pointer_cell==3 && ((y>=435 && x>=530)||(y<60 && x>=540))) {
                        if(help.visible)nxl_help_close(&help);
                        else if(left_panel==1 && srw_file_mode){srw_file_mode=0;nxl_help_reset(&help,ticks());}
                        else quitting=1;
                    }
                }
                pointer_cell=-1;if(!quitting)draw();
            }
        }
        if (quitting) break;
        if(poll_menu_rearm())draw();
        int held=pointer_down;
        for(unsigned k=0;k<sizeof down;++k)if(down[k])held=1;
        if(nxl_help_tick(&help,ticks(),held))draw();
        if(lens_menu_poll())draw();
        if(video_menu_poll())draw();
        if(left_panel==1 && srw_row==6 && help.visible && ticks()-progress_polled>=1000UL) {
            struct nxl_progress_status old=progress_status;load_progress_status();
            if(memcmp(&old,&progress_status,sizeof old))draw();
        }
        int rc=poll(fds,3,50); if (rc<0 && errno!=EINTR) break;
        for (int i=1;i<=2 && !quitting;++i) {
            if (fds[i].revents&(POLLERR|POLLHUP|POLLNVAL)) { quitting=1; action=0; break; }
            struct nxl_event e;
            while (fds[i].fd>=0 && read(fds[i].fd,&e,sizeof e)==sizeof e) {
                if (e.type==1) { key(e.code,e.value); if (!quitting) draw(); }
                if (quitting) break;
            }
        }
    }
    if(action==6||action==12){
        int was_auto=action==12;(void)memory_background(lock,was_auto?5:0);action=0;quitting=0;pointer_down=0;pointer_cell=-1;memset(down,0,sizeof down);combo_block=0;
        if(was_auto){strcpy(notice,task_auto_result==1?"AUTO INITIALIZATION COMPLETE":"INITIALIZATION STOPPED - CHECK LOGS");if(NXL_LENS_TEST_UI)lens_menu_start(0);}
        /* Inputs received during a blocking native stage must not be replayed
         * as another RUN or MENU action when the task returns. */
        for(unsigned i=1;i<=2;i++)if(fds[i].fd>=0){struct nxl_event old;for(unsigned n=0;n<128&&read(fds[i].fd,&old,sizeof old)==sizeof old;n++){} }
        XSetInputFocus(dpy,win,RevertToPointerRoot,CurrentTime);XSync(dpy,False);
        for(unsigned n=0;n<512&&XPending(dpy);n++){XEvent old;XNextEvent(dpy,&old);}
        load_raw_status();draw();launch_event("TASK_RETURNED_TO_EXISTING_MENU","same window and focus retained",0,0);continue;
    }break;}
    for (int i=1;i<=2;++i) if (fds[i].fd>=0) close(fds[i].fd);
    if(!browser_save())fputs("UI_STATE_SAVE_FAILED\n",stderr);
    restore_and_close();
    int rc=action==3 ? trace_background(lock) : action==6 ? memory_background(lock,0) : action==7 ? memory_background(lock,1) : action==9 ? memory_background(lock,2) : action==10 ? memory_background(lock,3) : action==11 ? memory_background(lock,4) : action ? run_action() : 0;
    unlink("/tmp/nx-lut-lite/menu.pid"); close(lock); return rc;
}
