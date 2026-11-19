/* Exact production lease and menu parser bodies, modeled file endpoints. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "../../common/video_fullwidth_contract.h"
#include "../../common/gamma_reload.h"
#include "../src/video_fullwidth_guards.h"
#include "../src/rtos_gamma_reload_blob.h"
#include "../src/gamma_reload_guards.h"
static uint32_t response[5],owned_meta[4],index_owned,nonce=1234;
static int pid=123,cap_released,rtos_entered,rtos_retired,ui_thawed=1;
static const char*reason="TEST";static struct vfw_reply receipt;
static unsigned char memory[65536],active[512],staged[512];static size_t active_size,staged_size;
static int exists,staged_exists,fault,positions[40],write_calls;static uint32_t current_meta[4],callback[4];
static const char boot[]="01234567-0123-0123-0123-0123456789ab\n";
static const char statline[]="123 (camera) S 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 5000 0\n";
static void video_publish_barrier(void){__asm__ volatile("dmb sy":::"memory");}
static void pause_ms(unsigned n){(void)n;}
static int video_entry(unsigned i,uint32_t*out){if(i!=1024)return 0;memcpy(out,current_meta,16);return 1;}
static int physical_read(void*c,uint32_t at,void*out,unsigned n){(void)c;if(n!=4)return 0;unsigned i=at==NG_GAMMA_SLOT?0:at==NG_PARENT_SLOT0?1:at==NG_PARENT_SLOT1?2:at==NG_COLOR_SLOT?3:4;if(i==4)return 0;memcpy(out,callback+i,4);return 1;}
static int fx_open(const char*p,int flags,...){
 int fd=-1;if(!strcmp(p,"/proc/sys/kernel/random/boot_id"))fd=10;else if(!strcmp(p,"/proc/123/stat"))fd=11;
 else if(!strcmp(p,"/tmp/nx-lut-lite/gamma-r11h.lease")){if(!exists){errno=ENOENT;return -1;}if(fault==8){errno=ELOOP;return -1;}assert(!(flags&3));fd=12;}
 else if(strstr(p,"/tmp/nx-lut-lite/gamma-r11h.lease.new.")==p){assert(flags&O_EXCL);staged_exists=1;staged_size=0;fd=20;}
 else if(!strcmp(p,"/dev/d5_sma")){assert(!(flags&3));fd=30;}
 if(fd<0){errno=ENOENT;return -1;}positions[fd]=0;return fd;
}
static ssize_t fx_read(int fd,void*out,size_t n){const void*src;size_t size;
 if(fd==10){src=boot;size=37;}else if(fd==11){src=statline;size=strlen(statline);}else{assert(fd==12);src=active;size=active_size;}
 size_t left=size-(size_t)positions[fd];if(n>left)n=left;memcpy(out,(const unsigned char*)src+positions[fd],n);positions[fd]+=(int)n;return (ssize_t)n;
}
static ssize_t fx_write(int fd,const void*in,size_t n){assert(fd==20);write_calls++;if(fault==10){errno=EIO;return -1;}if(fault==11&&write_calls==1){errno=EINTR;return -1;}if(fault==12&&n>7)n=7;assert(staged_size+n<=sizeof staged);memcpy(staged+staged_size,in,n);staged_size+=n;return (ssize_t)n;}
static int fx_close(int fd){(void)fd;return 0;}
static int fx_fsync(int fd){assert(fd==20);if(fault==13){errno=EIO;return -1;}return 0;}
static int fx_fstat(int fd,struct stat*out){assert(fd==12);memset(out,0,sizeof*out);out->st_mode=S_IFREG|0600;out->st_nlink=fault==9?2:1;out->st_size=(off_t)active_size;return 0;}
static int fx_lstat(const char*p,struct stat*out){assert(!strcmp(p,"/tmp/nx-lut-lite/gamma-r11h.lease"));if(!exists){errno=ENOENT;return -1;}return fx_fstat(12,out);}
static int fx_rename(const char*a,const char*b){assert(strstr(a,"gamma-r11h.lease.new.")&&strstr(b,"gamma-r11h.lease"));assert(staged_exists&&!exists);if(fault==14){errno=EIO;return -1;}memcpy(active,staged,staged_size);active_size=staged_size;exists=1;staged_exists=0;return 0;}
static void*fx_mmap(void*a,size_t n,int prot,int flags,int fd,off_t at){(void)a;(void)prot;(void)flags;assert(fd==30&&(uint64_t)at>=0x95000000u&&(uint64_t)at+n<=0x95010000u);return memory+(uint32_t)at-0x95000000u;}
static int fx_munmap(void*a,size_t n){(void)a;(void)n;return 0;}
static int gamma_shadow_read(uint32_t at,void*out,unsigned n){(void)at;(void)out;(void)n;errno=EIO;return 0;} /* Lease-only suite; snapshot suite executes the real reader. */
#define open fx_open
#define read fx_read
#define write fx_write
#define close fx_close
#define fsync fx_fsync
#define fstat fx_fstat
#define lstat fx_lstat
#define rename fx_rename
#define mmap fx_mmap
#define munmap fx_munmap
#include "../src/gamma_reload_host.inc"
#undef open
#undef read
#undef write
#undef close
#undef fsync
#undef fstat
#undef lstat
#undef rename
#undef mmap
#undef munmap
#include "../../base/nx-lut-lite/src/gamma_reload_state.h"
static void reset(int f){
 memset(&gamma_lease,0,sizeof gamma_lease);gamma_lease_exists=gamma_retained=0;fault=f;write_calls=0;exists=staged_exists=0;active_size=staged_size=0;memset(memory,0,sizeof memory);memcpy(memory,vfw_code,0x4000);response[1]=0x95000000u;index_owned=1024;
 owned_meta[0]=0x23;owned_meta[1]=128;owned_meta[2]=9;owned_meta[3]=response[1];memcpy(current_meta,owned_meta,16);callback[0]=NG_CALCULATE;callback[1]=callback[2]=NG_PARENT_UPDATE;callback[3]=NG_COLOR_CALCULATE;
}
int main(void){unsigned cases=0;
 reset(0);assert(gamma_load_lease()&&!gamma_lease_exists);cases++;
 for(int f=0;f<=14;f++){
  reset(f);if(f<10){assert(gamma_save_lease());assert(gamma_retained&&exists&&!staged_exists&&active_size==sizeof gamma_lease);gamma_lease_exists=gamma_retained=0;
   struct gamma_lease*q=(void*)active;
   if(f==1)q->hash^=1;if(f==2){q->boot[0]='f';q->hash=vfw_hash(q,offsetof(struct gamma_lease,hash));}if(f==3){q->pid++;q->hash=vfw_hash(q,offsetof(struct gamma_lease,hash));}
   if(f==4){q->start_lo++;q->hash=vfw_hash(q,offsetof(struct gamma_lease,hash));}if(f==5)current_meta[2]++;if(f==6)memory[0]^=1;if(f==7)callback[0]=0x12345678;
   int result=gamma_load_lease();assert(result==(f==0));assert(write_calls==1);printf("lease case=%d accepted=%d\n",f,result);
  }else{int saved=gamma_save_lease();assert(saved==(f==11||f==12));assert(gamma_retained==saved&&exists==saved);if(!saved)assert(!gamma_lease_exists);printf("lease write case=%d saved=%d calls=%d\n",f,saved,write_calls);}
  cases++;
 }
 for(unsigned i=0;i<4;i++){reset(0);assert(gamma_save_lease());gamma_lease_exists=0;callback[i]=0x12345678;assert(!gamma_load_lease());cases++;}
 reset(0);assert(gamma_save_lease());gamma_lease_exists=0;struct gamma_lease*old=(void*)active;old->schema=1;old->hash=vfw_hash(old,offsetof(struct gamma_lease,hash));assert(!gamma_load_lease());cases++;
 const char*valid="NXL_GAMMA_RELOAD1\nmode=LOG\nphase=NATIVE_QUEUED\nreason=GAMMA_NATIVE_QUEUED_HARDWARE_PENDING\nepoch=2\nqueued_epoch=2\nqualified=5\napplies=4\ninvalidations=2\nqueues=5\nfault=0\nend=NXL_GAMMA_RELOAD_END1\n";
 struct nxl_gamma_status gs;assert(nxl_gamma_parse(valid,&gs)&&gs.mode==2&&gs.epoch==2);cases++;
 const char*bad[]={"NXL_GAMMA_RELOAD2\n","NXL_GAMMA_RELOAD1\nmode=LOG\nmode=LOG\n","NXL_GAMMA_RELOAD1\nmode=LOG\nphase=LOADED\n"};for(unsigned i=0;i<3;i++){assert(!nxl_gamma_parse(bad[i],&gs));cases++;}
 char b[1024];strcpy(b,valid);char*q=strstr(b,"queued_epoch=2");q[13]='3';assert(!nxl_gamma_parse(b,&gs));cases++;
 strcpy(b,valid);q=strstr(b,"epoch=2");q[6]='0';assert(!nxl_gamma_parse(b,&gs));cases++;
 strcpy(b,valid);q=strstr(b,"queues=5");q[7]='0';assert(!nxl_gamma_parse(b,&gs));cases++;
 snprintf(b,sizeof b,"%sTRAILING",valid);assert(!nxl_gamma_parse(b,&gs));cases++;
 struct nxl_gamma_status action={0};
 for(int mode=0;mode<3;mode++){
  action.mode=mode;action.phase=3;assert(nxl_gamma_next_command(&action,0)==(mode==2?22:21));cases++;
  assert(nxl_gamma_next_command(&action,1)==22);cases++;
 }
 const int pending[]={1,2,5};for(unsigned i=0;i<3;i++){action.phase=pending[i];assert(nxl_gamma_next_command(&action,0)==22);cases++;}
 printf("PASS exact production lease and menu protocol cases=%u\n",cases);return 0;
}
