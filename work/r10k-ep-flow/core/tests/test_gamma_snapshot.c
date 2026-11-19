/* Exact production lease and menu parser bodies, modeled file endpoints. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
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
static int devfd=31,shadow_maps,read_maps,output_fault,output_fault_mode;
static const char*output_leaf;
static unsigned char pages[6][4096];static uint32_t shadow[2][NG_WORDS];
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
static int fx_fsync(int fd){if(fd>=40){if(output_fault_mode==4&&fd==40+output_fault){errno=EIO;return -1;}return 0;}assert(fd==20);if(fault==13){errno=EIO;return -1;}return 0;}
static int fx_fstat(int fd,struct stat*out){assert(fd==12);memset(out,0,sizeof*out);out->st_mode=S_IFREG|0600;out->st_nlink=fault==9?2:1;out->st_size=(off_t)active_size;return 0;}
static int fx_lstat(const char*p,struct stat*out){assert(!strcmp(p,"/tmp/nx-lut-lite/gamma-r11h.lease"));if(!exists){errno=ENOENT;return -1;}return fx_fstat(12,out);}
static int fx_rename(const char*a,const char*b){assert(strstr(a,"gamma-r11h.lease.new.")&&strstr(b,"gamma-r11h.lease"));assert(staged_exists&&!exists);if(fault==14){errno=EIO;return -1;}memcpy(active,staged,staged_size);active_size=staged_size;exists=1;staged_exists=0;return 0;}
static void*fx_mmap(void*a,size_t n,int prot,int flags,int fd,off_t at){
 (void)a;assert(prot==PROT_READ&&flags==MAP_SHARED);read_maps++;
 if(fd==30){if(fault==20){errno=EACCES;return MAP_FAILED;}assert((uint64_t)at>=0x95000000u&&(uint64_t)at+n<=0x95010000u);return memory+(uint32_t)at-0x95000000u;}
 assert(fd==devfd&&n==4096);unsigned i;
 switch((uint32_t)at){
 case 0x8062f000u:i=0;if(fault==21){errno=EACCES;return MAP_FAILED;}memcpy(pages[i]+0x6c8,callback,4);memcpy(pages[i]+0x600,callback+1,4);memcpy(pages[i]+0xd78,callback+3,4);break;
 case 0x8063e000u:i=1;memcpy(pages[i]+0x350,callback+2,4);break;
 case 0x81002000u:i=2;*(uint32_t*)(pages[i]+0xedc)=fault==16?0xbb700402u:0xbb701402u;break;
 case 0x813eb000u:i=3;*(uint32_t*)(pages[i]+0xf50)=fault==17&&shadow_maps?0xbb70b300u:0xbb71b300u;*(uint32_t*)(pages[i]+0xf54)=0xbb71d300u;break;
 case 0xbb71d000u:case 0xbb71f000u:{unsigned bank=(uint32_t)at==0xbb71f000u;if(fault==(int)(18+bank)){errno=EACCES;return MAP_FAILED;}i=4+bank;shadow_maps++;memcpy(pages[i]+0x228,shadow[bank],NG_BYTES);break;}
 case 0x82000000u:return pages[0];
 default:assert(!"Unexpected production mmap address");return MAP_FAILED;
 }return pages[i];
}
static int fx_munmap(void*a,size_t n){(void)a;(void)n;return 0;}
struct output_file {char path[256];unsigned char data[8192];size_t bytes;int closed;};
static struct output_file output[32];static unsigned outputs;
static int is_fault(const char*path){return output_leaf&&strstr(path,output_leaf)!=NULL;}
static FILE*fx_fopen(const char*path,const char*mode){
 assert(!strcmp(mode,"wx"));if(output_fault_mode==1&&is_fault(path)){errno=EACCES;return NULL;}
 for(unsigned i=0;i<outputs;i++)if(!strcmp(output[i].path,path)){errno=EEXIST;return NULL;}
 assert(outputs<32&&strlen(path)<sizeof output[0].path);struct output_file*f=output+outputs;memset(f,0,sizeof*f);strcpy(f->path,path);if(is_fault(path))output_fault=(int)outputs;outputs++;return(FILE*)f;
}
static size_t fx_fwrite(const void*p,size_t size,size_t count,FILE*file){
 struct output_file*f=(void*)file;assert(f>=output&&f<output+outputs&&!f->closed&&size==1);
 if(output_fault_mode==2&&is_fault(f->path)){errno=ENOSPC;return 0;}assert(f->bytes+count<=sizeof f->data);memcpy(f->data+f->bytes,p,count);f->bytes+=count;return count;
}
static int fx_fflush(FILE*file){struct output_file*f=(void*)file;if(output_fault_mode==3&&is_fault(f->path)){errno=EIO;return -1;}return 0;}
static int fx_fileno(FILE*file){return 40+(int)((struct output_file*)(void*)file-output);}
static int fx_fclose(FILE*file){struct output_file*f=(void*)file;f->closed=1;if(output_fault_mode==5&&is_fault(f->path)){errno=EIO;return -1;}return 0;}
static int fx_fprintf(FILE*f,const char*format,...){char text[4096];va_list args;va_start(args,format);int n=vsnprintf(text,sizeof text,format,args);va_end(args);assert(n>=0&&(unsigned)n<sizeof text);return fx_fwrite(text,1,(unsigned)n,f)==(unsigned)n?n:-1;}
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
#define fopen fx_fopen
#define fwrite fx_fwrite
#define fflush fx_fflush
#define fileno fx_fileno
#define fclose fx_fclose
#define fprintf fx_fprintf
#include "../src/gamma_reload_read.inc"
#include "../src/gamma_reload_host.inc"
#undef fopen
#undef fwrite
#undef fflush
#undef fileno
#undef fclose
#undef fprintf
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

static void output_reset(void){memset(output,0,sizeof output);outputs=0;output_leaf=NULL;output_fault_mode=0;output_fault=-1;gamma_restore_audit_bypass=0;}
static struct output_file*find_output(const char*leaf){for(unsigned i=0;i<outputs;i++)if(strstr(output[i].path,leaf))return output+i;return NULL;}
static void prepare(void){
 reset(0);memset(pages,0,sizeof pages);output_reset();read_maps=shadow_maps=0;
 assert(gamma_save_lease());struct ng_state*g=(void*)(memory+NG_STATE);g->magic=NG_MAGIC;g->schema=NG_SCHEMA;g->cap=gamma_lease.cap;g->bytes=VFW_CAP_BYTES;g->mode=NG_LIFT;g->epoch=g->queued_epoch=1;g->queues=g->applies=g->qualified=1;g->applied_object=0x82000000;
 g->video_epoch=1;g->video_queues=9;g->video_object=0x82010000;g->video_parent=0x82020000;g->video_driver_modes=23|(23<<8);
 for(unsigned k=0;k<2;k++)for(unsigned i=0;i<NG_WORDS;i++)shadow[k][i]=(k+1)*1000u+i;
}
static void assert_report(const char*stage,int success){
 struct output_file*f=find_output("before-snapshot.json");assert(f&&f->closed&&f->bytes);char needle[120];snprintf(needle,sizeof needle,"\"stage\":\"%s\"",stage);assert(strstr((char*)f->data,needle));assert(strstr((char*)f->data,success?"\"ok\":true":"\"ok\":false"));
 printf("SNAPJSON %s\n",f->data);
}
int main(void){unsigned cases=0;uint32_t out[NG_WORDS];
 prepare();int maps=read_maps;assert(!physical_read(NULL,NG_DDR0,out,NG_BYTES)&&read_maps==maps);cases++;
 for(unsigned bank=0;bank<2;bank++){assert(gamma_shadow_read(bank?NG_DDR1:NG_DDR0,out,NG_BYTES)&&!memcmp(out,shadow[bank],NG_BYTES));cases++;}
 const uint32_t bad_at[]={NG_DDR0+4,NG_DDR0-4,NG_DDR1+NG_BYTES,0x20810000,0x95000000,0xffffff00};
 for(unsigned i=0;i<sizeof bad_at/sizeof*bad_at;i++){maps=read_maps;assert(!gamma_shadow_read(bad_at[i],out,NG_BYTES)&&errno==EINVAL&&read_maps==maps);cases++;}
 const unsigned bad_n[]={0,4,NG_BYTES-1,NG_BYTES+1,8192};for(unsigned i=0;i<sizeof bad_n/sizeof*bad_n;i++){maps=read_maps;assert(!gamma_shadow_read(NG_DDR0,out,bad_n[i])&&errno==EINVAL&&read_maps==maps);cases++;}
 maps=read_maps;assert(!gamma_shadow_read(NG_DDR0,NULL,NG_BYTES)&&read_maps==maps);cases++;
 for(unsigned mode=0;mode<3;mode++){
  prepare();struct ng_state*g=(void*)(memory+NG_STATE);g->mode=mode;g->epoch=g->queued_epoch=mode+1;g->detached=mode==0;
  assert(gamma_before_ready("healthy",0));assert_report("COMPLETE",1);struct output_file*f=find_output("before-DDR-bank0-bank1.bin");assert(f&&f->bytes==864&&!memcmp(f->data,shadow,sizeof shadow));assert(find_output("before-state.bin")->bytes==sizeof*g);assert(strstr((char*)find_output("before-vlog8-reference.json")->data,"NX500_R11N_WORKING_EXPOSURE_NOT_PHYSICAL_GRAY"));assert(strstr((char*)find_output("before-vlog8-reference.json")->data,"\"native_working_anchor\":2965"));assert(strstr((char*)find_output("before-vlog8-reference.json")->data,"\"offline_input_scale_required\":false"));assert(strstr((char*)find_output("before-vlog8-reference.json")->data,"\"linear_input_gain\":2.5203589432187403"));assert(strstr((char*)find_output("before-vlog8-reference.json")->data,"\"working_anchor_target_code8\":132"));assert(gamma_before_snapshot.state_consistent&&gamma_before_snapshot.object_available);
  gamma_result(1,0,"healthy");assert(gamma_after_snapshot.ok);f=find_output("result.json");assert(f&&strstr((char*)f->data,"\"before_snapshot_ok\":true")&&strstr((char*)f->data,"\"after_snapshot_ok\":true"));printf("RESULTJSON %s\n",f->data);cases++;
 }
 for(int f=16;f<=21;f++){prepare();fault=f;assert(!gamma_dump("read-fault","before"));const char*stage=f==19?"READ_DDR_BANK1":f==20?"READ_CAP_STATE":f==21?"READ_CALLBACKS":"READ_DDR_BANK0";assert_report(stage,0);cases++;}
 const char*leaf[]={"lease.bin","state.bin","DDR-bank0-bank1.bin","callbacks.bin","video-scope.json","object.bin","vlog8-reference.json","color-factory.bin","color-target.bin","video-color-factory.bin","video-color-target.bin","native-input-context.bin","video-native-input-context.bin","binding.json"};
 const char*stage[]={"WRITE_LEASE","WRITE_STATE","WRITE_DDR","WRITE_CALLBACKS","WRITE_VIDEO_SCOPE","WRITE_OBJECT","WRITE_VLOG8_SCOPE","WRITE_COLOR_FACTORY","WRITE_COLOR_TARGET","WRITE_VIDEO_COLOR_FACTORY","WRITE_VIDEO_COLOR_TARGET","WRITE_NATIVE_INPUTS","WRITE_VIDEO_NATIVE_INPUTS","WRITE_BINDING"};
 for(unsigned i=0;i<14;i++)for(int mode=1;mode<=5;mode++){
  prepare();output_leaf=leaf[i];output_fault_mode=mode;assert(!gamma_dump("write-fault","before"));assert_report(stage[i],0);assert(gamma_before_snapshot.error);cases++;
 }
 for(int mode=1;mode<=5;mode++){prepare();output_leaf="snapshot.json";output_fault_mode=mode;assert(!gamma_dump("report-fault","before"));assert(!strcmp(gamma_before_snapshot.stage,"WRITE_SNAPSHOT_REPORT"));cases++;}
 prepare();((struct ng_state*)(void*)(memory+NG_STATE))->busy=1;assert(gamma_dump("unqualified","before")&&!gamma_before_snapshot.state_consistent);assert(strstr((char*)find_output("before-video-scope.json")->data,"\"state_consistent\":false"));cases++;
 for(unsigned command=0;command<4;command++){
  prepare();gamma_requested=command==0?NG_OFF:command==1?NG_LIFT:command==2?NG_LOG:NG_OFF;fault=18;int read_only=command==0;int accepted=gamma_before_ready("restore-gate",read_only);assert(accepted==(command==3));assert(gamma_restore_audit_bypass==(command==3));assert(((struct ng_state*)(void*)(memory+NG_STATE))->mode==NG_LIFT&&shadow[0][0]==1000);cases++;
 }
 prepare();struct ng_state*g=(void*)(memory+NG_STATE);g->fault=32;g->binding_reason=7;g->binding_input_raw=0;g->binding_input_resolved=0;
 assert(!gamma_result(1,0,"failed-native"));struct output_file*rf=find_output("result.json");assert(rf&&strstr((char*)rf->data,"\"ok\":false")&&strstr((char*)rf->data,"\"phase\":\"FAILED\""));cases++;
 prepare();g=(void*)(memory+NG_STATE);g->mode=NG_FACTORY;g->detach_requested=1;g->detached=0;g->fault=32;
 assert(!gamma_result(1,0,"failed-restore"));rf=find_output("result.json");assert(rf&&strstr((char*)rf->data,"\"ok\":false")&&strstr((char*)rf->data,"\"phase\":\"FAILED\""));cases++;
 prepare();g=(void*)(memory+NG_STATE);g->binding_reason=7;g->binding_parent=0x82003000;g->binding_input_raw=0;g->binding_input_resolved=0x82003010;
 assert(gamma_dump("binding","before"));rf=find_output("before-binding.json");assert(rf&&strstr((char*)rf->data,"\"reason\":7")&&strstr((char*)rf->data,"\"input_raw\":0")&&strstr((char*)rf->data,"\"input_resolved\":2181050384"));cases++;
 printf("PASS production Gamma snapshot/read path cases=%u\n",cases);return 0;
}
