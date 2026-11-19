/* SPDX-License-Identifier: AGPL-3.0-only
 * One-shot preview reload adapted from WApaperplane/metol, commit
 * b10ec8413ce5f9a03079288cb9880e4d0fac983f. Original source in SOURCE.
 */
/* NX500 1.12 runtime LUT0 loader. No table writes or firmware persistence.
 * EP RD_FINISH acknowledges the transfer; rendered output is tested separately.
 * Header ioctl definitions: Samsung DRIMe5 GPL EP interface (sysarch/inc).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <dirent.h>
#include <time.h>
#include "lutload_ep_abi.h"

static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping=1; }
#if defined(__clang__)
#pragma clang diagnostic ignored "-Winline-asm"
#endif
static void barrier(void) {
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" :: "r"(0) : "memory");
}
static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1000.0+ts.tv_nsec/1000000.0;
}
static int camera_ok(void) {
    char version[32],model[32];FILE *f=fopen("/etc/version.info","r");
    int ok=f && fgets(version,sizeof(version),f) && fgets(model,sizeof(model),f)
        && !strcmp(version,"1.12\n") && !strcmp(model,"NX500\n");
    if(f)fclose(f);return ok;
}
static void dump(const char *tag,volatile unsigned *r) {
    printf("%s on=%08x cfg=%08x access=%08x lut0=%08x lut1=%08x\n",
           tag,r[0],r[1],r[2],r[3],r[4]);fflush(stdout);
}
/* RSZ OUT1 EOF is available in preview on NX500 1.12 (CORE irq 14).
 * LVR frame interrupts do not arrive in this path. Start the short LUT DMA
 * after EOF instead of at an arbitrary point in the processing frame.
 * The video watcher uses a different pipeline and deliberately skips this. */
static int frame_sync(int fd) {
    int irq=EP_K_INT_CORE_RSZ_OUT1_EOF;
    struct ep_k_core_intr_wait_info q;
    memset(&q,0,sizeof(q));q.timeout_ms=120;q.intr=irq;
    if(ioctl(fd,EP_IOCTL_CORE_INTR_ENABLE,&irq)<0 ||
       ioctl(fd,EP_IOCTL_CORE_INTR_WAIT_QUEUE_INIT,&q)<0)return -1;
    return ioctl(fd,EP_IOCTL_CORE_INTR_WAIT,&q);
}
static int slot_ok(unsigned a){return a==0x810fd100u||a==0x81101e00u||a==0x81106b00u||a==0x81115200u||a==0x81119f00u||a==0x8111ec00u;}
static int source_main(void){
 if(!camera_ok())return 3;
 int f=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(f<0)return 1;
 volatile const unsigned*r=mmap(0,4096,PROT_READ,MAP_SHARED,f,0x2082b000);
 close(f);if(r==MAP_FAILED)return 1;
 unsigned a=r[3];int ok=slot_ok(a);munmap((void*)r,4096);
 if(ok)printf("0x%08x\n",a);return ok?0:2;
}
static int load_main(int argc,char **argv,int sync_preview) {
    int fd=-1,mem=-1,lock=-1,run=-1,rc=1,started=0,wait_only,current=0;
    int irq=EP_K_INT_ACC_3DLUT_RD_FINISH,wr,err;
    unsigned addr=0;char *end;
    volatile unsigned *r=MAP_FAILED;
    struct ep_k_acc_intr_wait_info wq;
    double t;
    if(argc==2 && !strcmp(argv[1],"current")){wait_only=0;current=1;}
    else if(argc==2 && !strcmp(argv[1],"wait-test"))wait_only=1;
    else if(argc==3 && !strcmp(argv[1],"once")) {
        wait_only=0;errno=0;addr=strtoul(argv[2],&end,0);
        /* Six factory/working slots only. A12/A14 are the AdobeRGB view slots,
         * A13 the sRGB+screen-color one, A17 the factory sRGB one; A18/A19 are
         * the working slots lut.sh writes. off() needs A12..A17 so it can kick
         * whichever slot p7 has just selected instead of only the working one. */
        if(errno || !*argv[2] || *end ||
           !(addr==0x810fd100u || addr==0x81101e00u || addr==0x81106b00u ||
             addr==0x81115200u || addr==0x81119f00u || addr==0x8111ec00u)) {
            fprintf(stderr,"REFUSE unsupported slot\n");return 2;
        }
    } else {fprintf(stderr,"usage: nx_lutload.arm once <slot> | wait-test\n");return 2;}
    if(!camera_ok()) {fprintf(stderr,"REFUSE expected NX500 1.12\n");return 3;}
    lock=open("/tmp/nx-lut-lite/lutload.fd",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) {perror("loader lock");rc=4;goto out;}
    /* Take the supervisor's existing SRW lease only in the new hardware
     * loader. The old LUT guard must still permit SRW's own off handoff. */
    run=open("/tmp/nx-lut-lite/run.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(run<0||flock(run,LOCK_EX|LOCK_NB)<0){rc=4;goto out;}
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
    fd=open("/dev/drime5_ep",O_RDWR|O_CLOEXEC);
    if(fd<0) {perror("EP open");goto out;}
    if(ioctl(fd,EP_IOCTL_ACC_INTR_ENABLE,&irq)<0) {perror("IRQ enable");goto out;}
    /* 500 ms was tuned for the video watcher; a preview-mode table load
     * finishes in well under 20 ms. Keeping the timeout short matters because
     * lut.sh retries: with di-camera-app gone the DMA never starts and the
     * interrupt never arrives, so a long timeout turns into visible UI lag. */
    memset(&wq,0,sizeof(wq));wq.timeout_ms=wait_only?120:150;wq.intr=irq;
    if(ioctl(fd,EP_IOCTL_ACC_INTR_WAIT_QUEUE_INIT,&wq)<0) {perror("IRQ queue");goto out;}
    if(!wait_only) {
        mem=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);
        if(mem<0) {perror("MMIO open");goto out;}
        r=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,mem,0x2082b000);
        if(r==MAP_FAILED) {perror("MMIO mmap");goto out;}
        dump("BEFORE",r);
        if(r[1]!=0 && r[1]!=0x100) {fprintf(stderr,"REFUSE unexpected cfg\n");rc=5;goto out;}
        if(stopping) {rc=7;goto out;}
        if(sync_preview && frame_sync(fd)<0) {
            fprintf(stderr,"REFUSE preview frame sync failed\n");rc=9;goto out;
        }
        if(stopping) {rc=7;goto out;}
        if(current)addr=r[3];
        if(!slot_ok(addr)){rc=2;goto out;}
        if(r[1]!=0 && r[1]!=0x100){rc=5;goto out;}
        started=1;r[0]|=1;r[2]&=~1u;r[2]|=1u;
        r[3]=addr;r[1]=(r[1]&~0x30u)|0x100u;barrier();
        r[2]|=0x100u;barrier();r[2]&=~0x100u;barrier();
        /* Do NOT bypass around the transfer: with OnOff=0 the block does not
         * run the DMA at all (2026-11-08 on-device test -- the call reported
         * OK, the finder never changed, and a half-press was still needed to
         * apply the table). Any remaining transition artifact needs visual
         * validation; RD_FINISH alone does not prove a tear-free frame. */
    }
    t=now_ms();errno=0;wr=ioctl(fd,EP_IOCTL_ACC_INTR_WAIT,&wq);err=errno;
    /* Access mode itself disturbs the finder. Leave it immediately after the
     * wait, including timeout/error returns: printf/fflush may block on SD or
     * a slow output sink and must never extend this hardware state. */
    if(started) {r[2]|=1u;r[2]&=~1u;barrier();}
    printf("WAIT rc=%d errno=%d elapsed_ms=%.3f jpeg_err=%u irq=%d trigger=%d\n",
           wr,err,now_ms()-t,wq.jpeg_err,irq,!wait_only);fflush(stdout);
    if(started)dump("PROCESS",r);
    if(wait_only) {rc=(wr<0 && err==ETIMEDOUT)?0:6;goto out;}
    if(wr<0 || wq.jpeg_err || stopping) {rc=6;goto out;}
    if(!(r[0]&1) || r[1]!=0x100 || (r[2]&1) || r[3]!=addr) {rc=8;goto out;}
    puts("OK DMA_RD_FINISH and processing mode verified");rc=0;
out:
    if(started && r!=MAP_FAILED) {r[2]&=~1u;barrier();}
    if(r!=MAP_FAILED)munmap((void*)r,4096);
    if(mem>=0)close(mem);if(fd>=0)close(fd);if(run>=0)close(run);if(lock>=0)close(lock);
    return rc;
}

int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_LUTLOAD_RC4 Metol EOF14 DMA7 one-shot; no watcher");return 0;}
 if(argc==2&&!strcmp(argv[1],"source"))return source_main();
 return load_main(argc,argv,1);
}
