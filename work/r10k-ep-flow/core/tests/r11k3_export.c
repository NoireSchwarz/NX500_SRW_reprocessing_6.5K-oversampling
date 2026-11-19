
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>
#include "../src/front_layout.h"
#include "../src/sha256.h"
#include "../../common/shared_workspace_io.h"
#include "../../common/resource_ledger.h"
static volatile unsigned char*cap_alias;
static unsigned frames_complete,p49_native_done,queued_status_sent,liveness_attempted,nonce,index_owned;
static uint32_t response[5],owned_meta[4];
static struct {struct {unsigned ready;}readiness;}front_handoff_last;
static struct {char source_sha256[65];}capture_source;
static __typeof__(capture_source)*p48_source=&capture_source;
static FILE*log_file;
static volatile sig_atomic_t stop_requested;
static uint64_t ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000000000u+(unsigned)t.tv_nsec;}
static void pause_ms(unsigned ms){struct timespec t={(time_t)(ms/1000u),(long)(ms%1000u)*1000000};nanosleep(&t,NULL);}
static unsigned owned_calls,owner_ok=1;
static int front_handoff_released(const char*d,const char*b){(void)d;(void)b;front_handoff_last.readiness.ready=1;return 1;}
static int slot_original(void){return 1;}
static int still_owned(void){owned_calls++;return owner_ok;}
static int p48_file_sha(const char*p,char out[65]){FILE*f=fopen(p,"rb");if(!f)return 0;struct sha256 h;sha256_init(&h);unsigned char b[1024],d[32];size_t n;while((n=fread(b,1,sizeof b,f)))sha256_update(&h,b,n);int ok=!ferror(f)&&!fclose(f);if(!ok)return 0;sha256_final(&h,d);digest_hex(d,out);return 1;}
#include "../src/front_workspace.inc"
int main(int argc,char**argv){
 if(argc<3)return 2;struct nr_resource r;assert(nr_read("/captured-resource.journal",&r)&&r.phase==NR_STOPPED&&r.done&&r.ownership_verified&&!r.api_inflight&&!r.paused);
 memcpy(response,r.response,sizeof response);response[0]=(uint32_t)strtoul(argv[1],NULL,0);memcpy(owned_meta,r.metadata,sizeof owned_meta);response[2]=SW_BYTES;owned_meta[1]=SW_BYTES/16384u;nonce=r.nonce;index_owned=r.index;
 shared_owner_mode=2;frames_complete=p49_native_done=r.done;queued_status_sent=liveness_attempted=1;log_file=fopen("/front-export.log","wx");assert(log_file);FILE*f=fopen("/source.sha256","rb");assert(f&&fread(capture_source.source_sha256,1,64,f)==64&&!fclose(f));
 if(argc==4){if(!strcmp(argv[3],"bad-pa"))response[1]+=4096;else if(!strcmp(argv[3],"bad-index"))index_owned++;else if(!strcmp(argv[3],"bad-nonce"))nonce=0;else if(!strcmp(argv[3],"bad-source"))capture_source.source_sha256[0]='g';else if(!strcmp(argv[3],"ownership-lost"))owner_ok=0;else return 3;}
 cap_alias=mmap(NULL,SW_BYTES,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(cap_alias!=MAP_FAILED);memset((void*)cap_alias,0,SW_CONTROL);
 const char*dir="/mnt/mmc/NXDEMO51/JOB_REPLAY/front_1/front";int ok=front_workspace_export(dir);struct stat st;char p[512];snprintf(p,sizeof p,"%s/shared-workspace.bin",dir);int exported_file=!lstat(p,&st)&&st.st_size==512;
 printf("{\"exported\":%d,\"lease_file\":%d,\"owned_check_calls\":%u,\"va\":%u,\"pa\":%u,\"va_mod16K\":%u,\"va_mod4K\":%u,\"captured_resource_valid\":true}\n",ok,exported_file,owned_calls,response[0],response[1],response[0]&16383u,response[0]&4095u);
 return ok==atoi(argv[2])&&exported_file==ok?0:5;
}
