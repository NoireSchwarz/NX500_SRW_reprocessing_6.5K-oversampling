/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_WIFI_RVF_UI_H
#define NXL_WIFI_RVF_UI_H

#include <stdint.h>
#include <sys/stat.h>

#define NXL_RVF_UI_REQUEST "/tmp/nx-lut-lite/rvf-ui.request"
#define NXL_RVF_UI_SERVER "/opt/usr/nx-lut-lite/bin/nxl-wifi-server"
#define NXL_RVF_UI_CAMERA "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
#define NXL_RVF_GUI_MOBILE 0x004c7658u
#define NXL_RVF_GUI_VIEWFINDER 0x004b1b34u

enum { NXL_RVF_UI_OPEN=1, NXL_RVF_UI_EXIT=2 };
struct nxl_rvf_ui_request {
    int32_t owner,camera,command;
    uint32_t window;
    uint64_t owner_tick,camera_tick,issued;
};
struct nxl_rvf_mobile_menu {
    uint32_t view;
    int32_t row;
    unsigned char busy_and_disabled[3];
    int valid;
};

static unsigned long long nxl_rvf_process_tick(pid_t pid){
    char name[64],record[1024];
    snprintf(name,sizeof name,"/proc/%ld/stat",(long)pid);
    int f=open(name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(f<0)return 0;
    ssize_t size=read(f,record,sizeof record-1);close(f);
    if(size<=0)return 0;
    record[size]=0;
    char *at=strrchr(record,')');
    if(!at)return 0;
    at++;
    for(unsigned i=3;i<22;i++){
        while(*at==' ')at++;
        while(*at&&*at!=' ')at++;
    }
    return strtoull(at,NULL,10);
}

static int nxl_rvf_process(pid_t pid,uint64_t tick,const char *exe){
    if(pid<=1||!tick||nxl_rvf_process_tick(pid)!=tick)return 0;
    char path[64],target[256];
    snprintf(path,sizeof path,"/proc/%ld/exe",(long)pid);
    ssize_t n=readlink(path,target,sizeof target-1);
    if(n<=0||(size_t)n>=sizeof target-1)return 0;
    target[n]=0;
    return !strcmp(target,exe)&&nxl_rvf_process_tick(pid)==tick;
}

static int nxl_rvf_mobile_fields(int fd,struct nxl_rvf_mobile_menu *menu){
    return pread(fd,&menu->view,4,NXL_RVF_GUI_MOBILE+0x10)==4&&
        pread(fd,menu->busy_and_disabled,sizeof menu->busy_and_disabled,NXL_RVF_GUI_MOBILE+0x1c)==sizeof menu->busy_and_disabled&&
        pread(fd,&menu->row,4,NXL_RVF_GUI_MOBILE+0x24)==4;
}

static int nxl_rvf_ui_snapshot(pid_t pid,uint64_t tick,uint32_t *gui,uint32_t *rvf_mode,
        struct nxl_rvf_mobile_menu *menu){
    if(!nxl_rvf_process(pid,tick,NXL_RVF_UI_CAMERA))return 0;
    char path[64];snprintf(path,sizeof path,"/proc/%ld/mem",(long)pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return 0;
    /* CSAFStateMachine::getCurrentState and UI_RVF_Is_Active, NX500 1.12. */
    static const unsigned char getter[]={0x08,0x00,0x90,0xe5,0x1e,0xff,0x2f,0xe1};
    static const unsigned char active[]={0xa8,0x35,0x04,0xe3,0x4a,0x30,0x40,0xe3,0x24,0x06,0x93,0xe5,
        0x02,0x00,0x50,0xe3,0x00,0x00,0xa0,0x13,0x01,0x00,0xa0,0x03,0x1e,0xff,0x2f,0xe1};
    unsigned char code[sizeof active];uint32_t a[2],b[2];
    int ok=pread(fd,code,sizeof getter,0x303e54)==sizeof getter&&!memcmp(code,getter,sizeof getter)&&
        pread(fd,code,sizeof active,0x185288)==sizeof active&&!memcmp(code,active,sizeof active)&&
        pread(fd,a,4,0x4b17e0)==4&&pread(fd,a+1,4,0x4a4bcc)==4;
    if(menu){
        memset(menu,0,sizeof *menu);
        if(ok&&a[0]==NXL_RVF_GUI_MOBILE&&a[1]==0){
            /* Original MOBILE dispatch, Down index, OK on row 1, and row flags. */
            static const struct {off_t offset;unsigned char bytes[16];} branch[]={
                {0x2d5e8c,{0x10,0x30,0x96,0xe5,0x00,0x00,0x53,0xe3,0x04,0x00,0x00,0x1a,0x06,0x00,0xa0,0xe1}},
                {0x2d5644,{0x54,0x36,0x07,0xe3,0x4c,0x30,0x40,0xe3,0x28,0x30,0x93,0xe5,0x01,0x00,0x73,0xe3}},
                {0x2d58dc,{0xeb,0x03,0x00,0xe3,0x00,0x10,0xa0,0xe3,0x01,0x20,0xa0,0xe1,0x01,0x30,0xa0,0xe1}},
                {0x2d3958,{0x1d,0x20,0xc4,0xe5,0x1e,0x20,0xc4,0xe5,0x1f,0x20,0xc4,0xe5,0x20,0x20,0xc4,0xe5}}
            };
            int exact=1;
            for(unsigned i=0;i<sizeof branch/sizeof *branch;i++){
                size_t n=sizeof branch[i].bytes;
                if(pread(fd,code,n,branch[i].offset)!=(ssize_t)n||memcmp(code,branch[i].bytes,n)){exact=0;break;}
            }
            struct nxl_rvf_mobile_menu again={0};
            menu->valid=exact&&nxl_rvf_mobile_fields(fd,menu)&&nxl_rvf_mobile_fields(fd,&again)&&
                menu->view==again.view&&menu->row==again.row&&
                !memcmp(menu->busy_and_disabled,again.busy_and_disabled,sizeof again.busy_and_disabled);
        }
    }
    ok=ok&&pread(fd,b,4,0x4b17e0)==4&&pread(fd,b+1,4,0x4a4bcc)==4&&!memcmp(a,b,sizeof a)&&a[1]<=2;
    close(fd);
    if(!ok||nxl_rvf_process_tick(pid)!=tick)return 0;
    *gui=a[0];*rvf_mode=a[1];return 1;
}

static int nxl_rvf_ui_observe(pid_t pid,uint64_t tick,uint32_t *gui,uint32_t *mode){
    return nxl_rvf_ui_snapshot(pid,tick,gui,mode,NULL);
}

static int nxl_rvf_ui_read(struct nxl_rvf_ui_request *r){
    int fd=open(NXL_RVF_UI_REQUEST,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
#ifdef NXL_RVF_LEGACY_LIBC
    /* The firmware's glibc exports the legacy fstat64 ABI. */
    extern int __fxstat64(int,int,struct stat64 *);
    struct stat64 st;
    int stat_rc=__fxstat64(3,fd,&st);
#else
    struct stat st;
    int stat_rc=fstat(fd,&st);
#endif
    int ok=!stat_rc&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&(st.st_mode&077)==0&&
        st.st_size==(off_t)sizeof *r&&read(fd,r,sizeof *r)==sizeof *r;
    close(fd);
    return ok&&r->window>1&&(r->command==NXL_RVF_UI_OPEN||r->command==NXL_RVF_UI_EXIT)&&
        nxl_rvf_process(r->owner,r->owner_tick,NXL_RVF_UI_SERVER);
}

static int nxl_rvf_ui_write(const struct nxl_rvf_ui_request *r){
    char temp[112];snprintf(temp,sizeof temp,NXL_RVF_UI_REQUEST ".%ld",(long)getpid());
    int fd=open(temp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    int ok=write(fd,r,sizeof *r)==sizeof *r;
    if(close(fd))ok=0;
    if(ok)ok=rename(temp,NXL_RVF_UI_REQUEST)==0;
    if(!ok)unlink(temp);
    return ok;
}

#endif
