/* SPDX-License-Identifier: AGPL-3.0-only
 * Standalone EV+AEL menu and EV+Wi-Fi video listener. No global input grab.
 */
#include "common.h"
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping = 1; }
static int debug;
static void event_log(const char *kind, int a, int b) {
    if (!debug) return;
    fprintf(stderr,"%s %d %d\n",kind,a,b); fflush(stderr);
    if (access("/mnt/mmc/NXDEMO51/logs",R_OK)==0) {
        FILE *f=fopen("/mnt/mmc/NXDEMO51/logs/startup.log","a");
        if (f) { fprintf(f,"%s %d %d\n",kind,a,b); fclose(f); }
    }
}
int main(int argc, char **argv) {
    if(argc==2 && !strcmp(argv[1],"--version")) {
        puts("NX_LUT_KEYS_VERSION=0.2.1; KEYS_LOGS_FOLDER_R4");return 0;
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
    event_log("LISTENER_0.2.1",NXL_EV,NXL_WIFI);
    int announced=0;
    int open_failed[2]={0,0};
    struct pollfd p[2] = {{-1, POLLIN, 0}, {-1, POLLIN, 0}};
    struct nxl_shortcut shortcut = {0};
    while (!stopping) {
        for (int i = 0; i < 2; ++i) if (p[i].fd < 0) {
            p[i].fd=nxl_input(i);
            if (p[i].fd<0) {
                if (!open_failed[i]) event_log("INPUT_OPEN_FAILED",i,errno);
                open_failed[i]=1;
            } else open_failed[i]=0;
        }
        if (!announced && p[0].fd>=0 && p[1].fd>=0) {
            FILE *f=fopen("/tmp/nx-lut-lite/listener.ready","w");
            if (f) { fprintf(f,"%ld\n",(long)getpid()); fclose(f); }
            announced=1; event_log("INPUTS_OPEN",p[0].fd,p[1].fd);
        }
        int status; pid_t done;
        while ((done=waitpid(-1,&status,WNOHANG))>0)
            event_log("MENU_EXIT",(int)done,WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        int rc = poll(p, 2, 1000);
        if (rc < 0 && errno != EINTR) break;
        for (int i = 0; i < 2; ++i) {
            if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close(p[i].fd); p[i].fd = -1;
                memset(&shortcut, 0, sizeof shortcut);
                announced=0; unlink("/tmp/nx-lut-lite/listener.ready");
            }
            if (!(p[i].revents & POLLIN)) continue;
            struct nxl_event e;
            while (read(p[i].fd, &e, sizeof e) == sizeof e) {
                if (e.type == 0 && e.code == 3) memset(&shortcut, 0, sizeof shortcut); /* SYN_DROPPED */
                if (e.type==1 && (e.code==NXL_EV || e.code==NXL_AEL || e.code==NXL_WIFI) && e.value!=2)
                    event_log("KEY",e.code,e.value);
                if (e.type != 1)continue;
                int command=nxl_activate(&shortcut, e.code, e.value);
                if(!command)continue;
                event_log(command==NXL_TOGGLE_VIDEO?"VIDEO_LAUNCH":"MENU_LAUNCH",0,0);
                pid_t child = fork();
                if (!child) {
                    execl(argv[1], argv[1], command==NXL_TOGGLE_VIDEO?"--video-toggle":"--toggle", (char *)0);
                    _exit(127);
                }
            }
        }
    }
    for (int i = 0; i < 2; ++i) if (p[i].fd >= 0) close(p[i].fd);
    unlink("/tmp/nx-lut-lite/listener.ready");
    close(lock);
    return 0;
}
