/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_BITB_JOB_H
#define NXL_BITB_JOB_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ep_sync_core.h"
#include "lut_cpu.h"
#include "test_geometry.h"
#ifdef NXL_FAST_PIPELINE
#define BITB_WIRE_VERSION 12u
#else
#define BITB_WIRE_VERSION 10u
#endif
#define BITB_BYTES TEST_BYTES
#define BITB_Y_BYTES 2048u
#define BITB_C_BYTES 1024u
#define BITB_CONFIG_COUNT 30u
#define BITB_REQUEST_MAGIC 0x3751424eu
#define BITB_RESULT_MAGIC 0x3752424eu
struct bitb_request {uint32_t magic,version,pa,bytes,nonce,camera_pid,owner_va,reserved,crc;};
enum bitb_error {BJ_OK=0,BJ_REQUEST,BJ_PREPARE,BJ_CONTEXT,BJ_LOCK,BJ_LOCK_STATE,BJ_BUSY,BJ_CLOCK,BJ_RESET,
 BJ_QUEUE,BJ_ENABLE,BJ_TRANSLATION,BJ_COPY,BJ_WAIT,BJ_COMPARE,BJ_DISABLE,BJ_RESTORE,BJ_UNLOCK,BJ_SAVE,BJ_CANCEL,BJ_LUT,BJ_TILE_FENCE,BJ_TILE_COVERAGE,BJ_DMA_DRAIN};
enum bitb_native {BN_CLOCK,BN_RESET,BN_INTR,BN_COPY,BN_UPDATE,BN_VTOP};
struct bitb_result {
 uint32_t magic,version,bytes,request_crc,phase,error,pa,alias_va,udd_base,ep_fd,sma_fd,sma_open_flags;
 uint32_t ready,paused_contract,code_guards,translations,matches,lock_acquired,lock_released;
 uint32_t queue_initialized,queue_fresh,irq_enabled,irq_disabled,submission_attempted,engine_jobs;
 int32_t copy_return,wait_return,wait_errno;
 uint32_t pending_before,pending_cleared,queue_empty_before_submit,pending_after_reset,fresh_completion,source_ok,target_ok,guards_ok,source_crc,target_crc,expected_target_crc;
 uint32_t mmio_modified,initialized_bytes,initialized_verified,reset_after,registers_restored,clocks_restored,kernel_restored,safe_to_release;
 uint32_t clock_before[2],clock_after[2],control_before,control_after,status_after;
 uint32_t config_before[BITB_CONFIG_COUNT],config_after[BITB_CONFIG_COUNT];
 uint32_t lut_cpu_attempted,lut_identity_verified,lut_cpu_verified,lut_changed_bytes,lut_input_crc,lut_identity_crc,lut_cpu_crc,lut_table_crc,lut_cpu_passes,lut_hardware_jobs;
 uint32_t route_samples,route_verified,route_crc,route_flags,rtos_guards;
 uint32_t request_bytes,photo_pixel_access,kernel_writes;
 uint32_t source_pa,source_copies,source_stable,table_source_crc,owner_records,owner_stable,owner_crc,failed_stage;
 uint32_t idle_verified,bank_saved,table_loaded,table_readback,bank_restored,table_operations,table_completions,load_poll_completions,pixel_format,pixel_bytes,output_changed,restore_verified,quarantine_reason;
 uint32_t p11_stage;
 uint32_t local_reset_calls,cold_restart_required,inactive_start_verified,backup_skipped,ldc_hardware_jobs,crc;
};
struct bitb_snapshots {unsigned char before[BITB_BYTES],after[BITB_BYTES];uint32_t params[22],addresses[8];unsigned char lut_input[LUT_FRAME_BYTES],lut_identity[LUT_FRAME_BYTES],lut_cpu[LUT_FRAME_BYTES],lut_table[LUT_SLOT_BYTES];};
struct bitb_ops {
 void *context;
 int (*ioctl)(void*,uint32_t,void*);
 int (*native)(void*,enum bitb_native,uintptr_t,uint32_t);
 uint32_t (*read)(void*,unsigned,uint32_t);
 void (*write)(void*,unsigned,uint32_t,uint32_t);
 int (*kernel)(void*,struct ep_sync_observation*);
 int (*last_errno)(void*);
 void (*barrier)(void*);
};
static const uint32_t bitb_config_offsets[BITB_CONFIG_COUNT]={8,12,20,24,28,36,44,52,60,68,32,72,104,108,112,116,120,124,128,76,80,84,88,92,96,100,40,48,56,64};
/* Last four registers are zero-plane addresses: preserve them as well. */
static uint32_t bitb_crc_step(uint32_t c,const void *data,size_t n){
 static uint32_t table[4][256];static int ready;
 if(!ready){
  for(unsigned i=0;i<256;i++){uint32_t v=i;for(unsigned j=0;j<8;j++)v=(v>>1)^(0xedb88320u&-(v&1u));table[0][i]=v;}
  for(unsigned k=1;k<4;k++)for(unsigned i=0;i<256;i++){uint32_t v=table[k-1][i];table[k][i]=table[0][v&255]^(v>>8);}ready=1;
 }
 const unsigned char *p=data;
 while(n>=4){uint32_t v;memcpy(&v,p,4);v^=c;c=table[3][v&255]^table[2][(v>>8)&255]^table[1][(v>>16)&255]^table[0][v>>24];p+=4;n-=4;}
 while(n--)c=table[0][(c^*p++)&255]^(c>>8);return c;
}
static uint32_t bitb_crc(const void *data,size_t n){return ~bitb_crc_step(~0u,data,n);}
static int bitb_request_valid(const struct bitb_request *q){return q->magic==BITB_REQUEST_MAGIC&&q->version==BITB_WIRE_VERSION&&q->bytes==BITB_BYTES&&
 q->pa>=0x94000000u&&!(q->pa&0x3fffu)&&(uint64_t)q->pa+BITB_BYTES<=0xbb6cc000u&&q->camera_pid>1&&q->owner_va&&
 !q->reserved&&q->crc==bitb_crc(q,offsetof(struct bitb_request,crc));}
static unsigned char bitb_pattern(uint32_t i,uint32_t nonce){return (unsigned char)((i*73u+(i>>2)*19u+(nonce>>((i&3)*8)))^(i>>7)^0xa7u);}
static int bitb_kernel_quiet(const struct ep_sync_observation *s,int locked){struct ep_sync_observation x=*s;
 if(locked){if(x.mutex[0]!=0||x.mutex[1]!=EP_KERNEL_MUTEX_VA+4||x.mutex[2]!=EP_KERNEL_MUTEX_VA+4)return 0;x.mutex[0]=1;}
 return ep_sync_observation_error(&x)==NULL;
}

#endif
