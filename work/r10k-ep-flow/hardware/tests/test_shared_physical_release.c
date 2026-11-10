#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include "../src/alloc_core.h"
#include "../../common/shared_workspace_io.h"
#include "../../core/src/sha256.h"
#include "../../core/src/batch_contract.h"
static struct sw_lease shared_lease;
static unsigned char prefix[SW_CONTROL],alias[SW_CONTROL];
static unsigned char*full_prefix=prefix;static volatile unsigned char*shared_alias=alias;
static unsigned char*manager_map;static uint32_t *baseline,*baseline_again,response[5],cap_base=0x10000000;
static unsigned mode,shared_borrowed=1,api_inflight,trace_stopped=1,native_signal,p7_go_sent=1,p7_worker_exited=1,p7_result_valid=1,p7_safe_to_release=1,p7_quarantined;
static unsigned release_done,return_verified,free_after,stop_requested,native_count,model_free,model_pauses;static int devfd;
static FILE*log_file;static const char *reason,*trace_phase;
static struct {unsigned camera_paused,worker_done;} shared_state={1,0},*p7_shared=&shared_state;
static int shared_front_live(void){return mode!=10;}
static int p7_pause(void){model_pauses++;p7_shared->camera_paused=1;return 1;}
static int p7_save(const char*p,const void*b,size_t n){FILE*f=fopen(p,"wb");if(!f)return 0;int ok=fwrite(b,1,n,f)==n;if(fclose(f))ok=0;return ok;}
static int pool_head(uint32_t*out){*out=model_free-(mode==11);return 1;}
static void pause_ms(unsigned n){(void)n;}
static void event(const char*s){printf("EVENT %s\n",s);}
static int invoke(uint32_t fn,uint32_t arg,unsigned timeout){assert(fn==cap_base+ALLOC_RELEASE&&arg==shared_lease.va&&timeout==8000);native_count++;if(mode==14)return 0;if(mode!=15){memset(manager_map+0x908+shared_lease.index*16,0,shared_lease.metadata[1]*16);model_free=ALLOC_BLOCKS;}return 1;}
static int get_regs(struct alloc_regs*r){memset(r,0,sizeof *r);r->r[0]=mode==15;return 1;}
#include "../src/shared_release_controller.inc"
int main(int argc,char**argv){assert(argc==2);mode=(unsigned)atoi(argv[1]);log_file=stdout;
 assert(sw_file_read("/mnt/mmc/lease.bin",&shared_lease,sizeof shared_lease)&&sw_valid(&shared_lease));shared_lease.phase=SW_BACKEND_DONE;sw_seal(&shared_lease);
 uint32_t*h=(uint32_t*)(prefix+P48_IDLE_HANDOFF_OFFSET);h[0]=P48_IDLE_HANDOFF_MAGIC;h[1]=1;h[2]=shared_lease.nonce;h[3]=h[4]=h[6]=1;h[5]=0x81900000;h[62]=p48_batch_crc(h,248);h[63]=p48_batch_token(shared_lease.nonce,1,h[0],h[62]);assert(p48_idle_handoff_valid(h,shared_lease.nonce,1));
 memcpy(alias,prefix,sizeof alias);if(mode==7)alias[0]^=1;
 manager_map=calloc(1,0xb2000);baseline=calloc(ALLOC_BLOCKS,16);baseline_again=calloc(ALLOC_BLOCKS,16);assert(manager_map&&baseline&&baseline_again);
 memcpy(manager_map+0x908+shared_lease.index*16,shared_lease.metadata,16);if(mode==12)manager_map[0x908+shared_lease.index*16]^=1;
 model_free=ALLOC_BLOCKS-shared_lease.metadata[1];response[0]=shared_lease.va;
 if(mode==9)p7_safe_to_release=0;devfd=open("/dev/mem",O_RDONLY);assert(devfd>=0);unsigned char code[132];assert(pread(devfd,code,sizeof code,(off_t)(uint64_t)0x801c9c50u)<0&&errno==EFAULT);
 int released=shared_release_once("/mnt/mmc/proof");
 if(mode==0){assert(released&&native_count==1&&shared_release_samples==16&&release_done&&shared_reader.count==0);assert(shared_release_confirm("/mnt/mmc/proof")&&return_verified&&shared_released&&shared_lease.phase==SW_RELEASED);}
 else{assert(!released&&!return_verified&&!shared_released);assert(native_count==(unsigned)(mode==14||mode==15));}
 printf("PASS physical retirement mode=%u samples=%u native_release_calls=%u return_verified=%u\n",mode,shared_release_samples,native_count,return_verified);return 0;
}
