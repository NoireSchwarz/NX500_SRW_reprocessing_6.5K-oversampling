/* SPDX-License-Identifier: AGPL-3.0-only. Actual production shellproof(),
 * including its Linux adapters. Only the OS boundary below is modeled. */
#define msgget model_msgget
#define msgctl model_msgctl
#define opendir model_opendir
#define readdir model_readdir
#define closedir model_closedir
#define fopen model_fopen
#define fgets model_fgets
#define fclose model_fclose
#define pread model_pread
#define clock_gettime model_clock_gettime
#define nanosleep model_nanosleep
#define main production_rearm_main_not_run
#ifndef REARM_SOURCE
#define REARM_SOURCE "../src/nxl-session-rearm.c"
#endif
#include REARM_SOURCE
#undef main
#undef msgget
#undef msgctl
#undef opendir
#undef readdir
#undef closedir
#undef fopen
#undef fgets
#undef fclose
#undef pread
#undef clock_gettime
#undef nanosleep
#include <assert.h>

static unsigned serial,duplicate,busy,wrong_lr,read_error,legacy_number;
static uint64_t now;
static char opened[256];
int model_msgget(key_t key,int flags){assert(key==0x8828&&flags==0);return 0x48006;}
int model_msgctl(int id,int command,struct msqid_ds *ds){assert(id==0x48006&&command==IPC_STAT);memset(ds,0,sizeof *ds);ds->msg_perm.uid=getuid();ds->msg_qnum=busy;return 0;}
DIR *model_opendir(const char *path){assert(strstr(path,"/task"));serial=0;return (DIR*)(uintptr_t)1;}
struct dirent *model_readdir(DIR*d){assert(d==(DIR*)(uintptr_t)1);static struct dirent e;memset(&e,0,sizeof e);if(serial++>=1+duplicate)return NULL;strcpy(e.d_name,serial==1?"259":"260");return &e;}
int model_closedir(DIR*d){assert(d==(DIR*)(uintptr_t)1);return 0;}
FILE *model_fopen(const char *path,const char *mode){assert(!strcmp(mode,"r"));assert(strlen(path)<sizeof opened);strcpy(opened,path);return (FILE*)(uintptr_t)2;}
char *model_fgets(char *out,int n,FILE*f){
 assert(f==(FILE*)(uintptr_t)2);if(read_error)return NULL;
 const char *text=strstr(opened,"/comm")?"shell_di_app\n":strstr(opened,"/wchan")?"do_msgrcv\n":
  legacy_number?"302 0x48006 0xafff69b0 0xd0 0 0 0xafff68b0 0xafff6890 0xb32a6c10\n":
  "0 0x48006 0xafff69b0 0xd0 0 0 0xafff68b0 0xafff6890 0xb32a6c10\n";
 assert(strlen(text)<(unsigned)n);strcpy(out,text);return out;
}
int model_fclose(FILE*f){assert(f==(FILE*)(uintptr_t)2);return 0;}
ssize_t model_pread(int fd,void *out,size_t n,off_t address){assert(fd==42&&n==4&&address==0xafff6898u);uint32_t lr=0xb2b27344u+wrong_lr;memcpy(out,&lr,4);return 4;}
int model_clock_gettime(clockid_t c,struct timespec*t){assert(c==CLOCK_MONOTONIC);t->tv_sec=(time_t)(now/1000);t->tv_nsec=(long)(now%1000)*1000000;return 0;}
int model_nanosleep(const struct timespec*t,struct timespec*r){(void)r;now+=(uint64_t)t->tv_sec*1000u+(unsigned)t->tv_nsec/1000000u;return 0;}

int main(void){
 camera_pid=247;libc_base=0xb31e1000u;shell_base=0xb2b20000u;procfd=42;
#ifdef EXPECT_LEGACY_REJECTION
 assert(!shellproof());assert(!parked&&queue_empty);puts("PASS legacy actual production adapter reproduces the syscall=0 false refusal");return 0;
#else
 unsigned checks=0;
#define CHECK(x) do{assert(x);checks++;}while(0)
 CHECK(shellproof()&&parked&&queue_empty);CHECK(shell_report.frame.number==0);
 legacy_number=1;CHECK(shellproof());legacy_number=0;
 duplicate=1;CHECK(!shellproof()&&!parked);duplicate=0;
 wrong_lr=4;CHECK(!shellproof()&&!parked);wrong_lr=0;
 read_error=1;CHECK(!shellproof()&&!parked);read_error=0;
 busy=1;CHECK(!shellproof()&&!parked&&!queue_empty);busy=0;
 CHECK(shell_check(1)&&shell_report.samples==3&&now==40);
 CHECK(!mmio_reads&&!snapshots&&!read_failures);
 printf("PASS shell_adapter_r7c checks=%u; actual production syscall, directory, queue, pread and timed-wait adapters\n",checks);return 0;
#endif
}
