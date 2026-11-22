/* SPDX-License-Identifier: AGPL-3.0-only
 * EV+AEL / double-EV / EV+Wi-Fi listener. Passive X11 Wi-Fi grab only.
 */
#include "common.h"
#include "ev_triple.h"
#include "wifi_xgate.h"
#include <sys/syscall.h>
#include <time.h>
static unsigned long key_ticks(void){struct timespec ts;if(syscall(263,CLOCK_MONOTONIC,&ts))return 0;return(unsigned long)ts.tv_sec*1000UL+(unsigned long)ts.tv_nsec/1000000UL;}
static int menu_active(void){int f=nxl_lock("/tmp/nx-lut-lite/menu.lock");if(f<0)return 1;close(f);return 0;}
struct wifi_press {
    int active, input;
    struct nxl_event down;
    unsigned long local;
};
static int wifi_duration(const struct wifi_press *press, int input,
                         const struct nxl_event *up, unsigned long now) {
    if (!press->active || press->input != input) return -1;
    const struct nxl_event *down = &press->down;
    if (up->sec >= down->sec && up->usec >= 0 && up->usec < 1000000 &&
        down->usec >= 0 && down->usec < 1000000 && (down->sec || down->usec)) {
        unsigned long seconds = (unsigned long)up->sec - (unsigned long)down->sec;
        if (seconds < 86400) {
            long ms = (long)(seconds * 1000) + up->usec / 1000 - down->usec / 1000;
            if (ms >= 0) return (int)ms;
        }
    }
    unsigned long elapsed = now - press->local;
    return now && press->local && elapsed < 86400000UL ? (int)elapsed : -1;
}
static int wifi_state_absent(const char *path){
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return errno==ENOENT;
    close(fd);
    return 0;
}
static int wifi_native_idle(void){
    return !menu_active() && wifi_state_absent("/tmp/nx-lut-lite/wifi-service.pid")
        && wifi_state_absent("/tmp/nx-lut-lite/wifi-hotspot.owner");
}
static int wifi_shortcut(struct nxl_shortcut *shortcut, struct wifi_press *press,
                         int input, const struct nxl_event *event, unsigned long now) {
    if (event->value == 1) wifi_cancel_click();
    if (event->code == NXL_WIFI && event->value == 1 && !shortcut->wifi) {
        shortcut->wifi_owned = menu_active();
        *press = (struct wifi_press){1, input, *event, now};
    }
    int command = nxl_activate(shortcut, event->code, event->value);
    if (event->code != NXL_WIFI || event->value != 0) return command;
    int duration = wifi_duration(press, input, event, now);
    press->active = 0;
    if (command != NXL_OPEN_WIFI) return command;
    if (duration < 0) {
        event_log("WIFI_NATIVE_UNPAIRED", input, 0);
        return 0;
    }
    if (duration < 1200) {
        event_log("WIFI_SHORT_MS", duration, input);
        return NXL_OPEN_WIFI;
    }
    event_log("WIFI_NATIVE_HOLD_MS", duration, input);
    wifi_request_click(now);
    return 0;
}
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping = 1; }
static int debug;
static void event_log(const char *kind, int a, int b) {
    if (!debug && strncmp(kind,"WIFI_NATIVE_",12)) return;
    fprintf(stderr,"%s %d %d\n",kind,a,b); fflush(stderr);
    if (access("/mnt/mmc/NXDEMO51/logs",R_OK)==0) {
        FILE *f=fopen("/mnt/mmc/NXDEMO51/logs/startup.log","a");
        if (f) { fprintf(f,"%s %d %d\n",kind,a,b); fclose(f); }
    }
}
#define NXL_RVF_LEGACY_LIBC 1
#include "wifi_rvf_gui.inc"
int main(int argc, char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")) {
        puts("NX_LUT_KEYS_VERSION=0.4.0-RC6.14; EV_DOUBLE_CURRENT_LUT; WIFI_NATIVE_RVF; CHORD_PRIORITY");return 0;
    }
    if (argc != 2 || !nxl_supported()) {
        fputs("nxl-keys: requires NX500 firmware 1.12 and a UI executable path\n", stderr);
        return 1;
    }
    int lock = nxl_lock("/tmp/nx-lut-lite/keys.lock");
    if (lock < 0) return 0;
    signal(SIGTERM, stop); signal(SIGINT, stop);
    signal(SIGHUP, SIG_IGN); signal(SIGCHLD, SIG_DFL);
    debug=getenv("NXL_INPUT_DEBUG") && !strcmp(getenv("NXL_INPUT_DEBUG"),"1");
    event_log("LISTENER_RC6132",NXL_EV,NXL_WIFI);
    int gate_failed=0;
    int announced=0;
    int open_failed[2]={0,0};
    struct pollfd p[3] = {{-1,POLLIN,0},{-1,POLLIN,0},{-1,POLLIN,0}};
    struct nxl_shortcut shortcut = {0};
    struct wifi_press press = {0};
    struct nxl_ev_taps taps={0};
    while (!stopping) {
        for (int i = 0; i < 2; ++i) if (p[i].fd < 0) {
            p[i].fd=nxl_input(i);
            if (p[i].fd<0) {
                if (!open_failed[i]) event_log("INPUT_OPEN_FAILED",i,errno);
                open_failed[i]=1;
            } else open_failed[i]=0;
        }
        if(p[0].fd>=0&&p[1].fd>=0&&!wifi_display){
            if(wifi_gate_open()){p[2].fd=ConnectionNumber(wifi_display);gate_failed=0;}
            else if(!gate_failed){event_log("WIFI_GRAB_UNAVAILABLE",wifi_xerror,0);gate_failed=1;}
        }
        if (!announced && p[0].fd>=0 && p[1].fd>=0) {
            FILE *f=fopen("/tmp/nx-lut-lite/listener.ready","w");
            if (f) { fprintf(f,"%ld\n",(long)getpid()); fclose(f); }
            announced=1; event_log("INPUTS_OPEN",p[0].fd,p[1].fd);
        }
        int status; pid_t done;
        while ((done=waitpid(-1,&status,WNOHANG))>0)
            event_log("MENU_EXIT",(int)done,WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        if(wifi_display&&!wifi_gate_drain()){wifi_gate_close();p[2].fd=-1;}
        int rc = poll(p,3,wifi_display?50:500);
        if(p[2].revents&(POLLERR|POLLHUP|POLLNVAL))break;
        if(wifi_display&&!wifi_gate_drain()){wifi_gate_close();p[2].fd=-1;}
        if (rc < 0 && errno != EINTR) break;
        for (int i = 0; i < 2; ++i) {
            if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close(p[i].fd); p[i].fd = -1;press.active=0;
                wifi_gate_close();p[2].fd=-1;
                memset(&shortcut,0,sizeof shortcut);memset(&taps,0,sizeof taps);
                announced=0; unlink("/tmp/nx-lut-lite/listener.ready");
            }
            if (!(p[i].revents & POLLIN)) continue;
            struct nxl_event e;
            while (read(p[i].fd, &e, sizeof e) == sizeof e) {
                if (e.type == 0 && e.code == 3){wifi_cancel_click();press.active=0;memset(&shortcut,0,sizeof shortcut);memset(&taps,0,sizeof taps);} /* SYN_DROPPED */
                if (e.type==1 && (e.code==NXL_EV || e.code==NXL_AEL || e.code==NXL_WIFI) && e.value!=2)
                    event_log("KEY",e.code,e.value);
                if(e.type!=1)continue;
                /* A Wi-Fi release can reach this passive reader after the menu has
                 * already stopped and released its lock during AP handoff.
                 * Decide ownership at PRESS, not at the later release. */
                unsigned long now=key_ticks();
                int command=wifi_shortcut(&shortcut,&press,i,&e,now);
                int quick=nxl_ev_feed(&taps,e.code,e.value,now);
                if(shortcut.chord.ael||shortcut.wifi||shortcut.pending||command)memset(&taps,0,sizeof taps);
                if(quick&&!command&&!menu_active())command=3;
                if(!command)continue;
                if(command==NXL_OPEN_WIFI&&!wifi_display)continue;
                if((command==NXL_TOGGLE_VLOG||command==NXL_OPEN_WIFI)&&menu_active())continue;
                event_log(command==NXL_TOGGLE_VLOG?"VLOG_LAUNCH":"MENU_LAUNCH",0,0);
                if(wifi_display&&!wifi_gate_drain())continue;
                pid_t child = fork();
                if (!child) {
                    if(wifi_display)close(ConnectionNumber(wifi_display));
                    execl(argv[1], argv[1], command==3?"--quick-lut":command==NXL_TOGGLE_VLOG?"--vlog-toggle":command==NXL_OPEN_WIFI?"--wifi":"--toggle", (char *)0);
                    _exit(127);
                }
            }
        }
        if(wifi_display&&!wifi_deliver_click(key_ticks())){wifi_gate_close();p[2].fd=-1;}
        if(wifi_display&&!rvf_ui_tick(key_ticks())){wifi_gate_close();p[2].fd=-1;}
    }
    for (int i = 0; i < 2; ++i) if (p[i].fd >= 0) close(p[i].fd);
    unlink("/tmp/nx-lut-lite/listener.ready");
    wifi_gate_close();
    close(lock);
    return 0;
}
