/* SPDX-License-Identifier: AGPL-3.0-only. Export only after verified front cleanup. */
#include "../../common/shared_workspace_io.h"
#include "../../hardware/src/lut_cpu.h"
static struct sw_lease front_workspace;
_Static_assert(P39_CAP_BYTES==SW_BYTES&&P39_TARGET_OFFSET==SW_INPUT&&P39_TARGET_STRIDE==SW_FRONT_STRIDE&&P39_UV_OFFSET==SW_FRONT_UV,"shared strided ISP layout");
static unsigned shared_owner_mode;
static int front_workspace_export(const char *dir){
 if(!shared_owner_mode)return 1;
 if(!cap_alias||frames_complete!=1||!front_handoff_released(dir,"export"))return 0;
 struct sw_lease s;memset(&s,0,sizeof s);s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;
 s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;
 s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;
 s.pa=response[1];s.va=response[0];s.nonce=nonce;s.owner_pid=(uint32_t)getpid();s.index=index_owned;
 s.front_return_barrier=frames_complete==1&&p49_native_done&&queued_status_sent&&liveness_attempted;
 s.frame_callbacks_retired=front_handoff_last.readiness.ready;s.controller_slot_restored=slot_original();
 if(!sw_owner_ticks(s.owner_pid,&s.owner_start_ticks))return 0;memcpy(s.metadata,owned_meta,sizeof s.metadata);
 if(!p48_source)return 0;memcpy(s.source_sha256,p48_source->source_sha256,64);
 struct sha256 hash;unsigned char digest[32];sha256_init(&hash);sha256_update(&hash,(const void*)cap_alias,SW_CONTROL);sha256_final(&hash,digest);digest_hex(digest,s.prefix_sha256);
 char path[1200];if(snprintf(path,sizeof path,"%s/srw-lut.bin",dir)>=(int)sizeof path)return 0;
 unsigned char table[LUT_SLOT_BYTES];struct stat st;
 if(lstat(path,&st)){if(errno!=ENOENT)return 0;lut_make_table(table,1);if(!sw_file_new(path,table,sizeof table))return 0;}
 if(!sw_file_read(path,table,sizeof table)||!lut_table_valid(table,sizeof table)||!p48_file_sha(path,s.lut_sha256))return 0;
 sw_seal(&s);if(!sw_valid(&s)||!still_owned())return 0;
 if(!sw_owner_proof_publish(&s)){fprintf(log_file,"FRONT_OWNER_PROOF_PUBLISH_FAILED\n");return 0;}
 /* The native front only uses the first128MiB. Its code/control prefix stays
  * allocated and unchanged even after image regions are handed to EP. */
 for(unsigned i=0;i<16384;i++)cap_alias[SW_LEASE+i]=0;
 for(unsigned i=0;i<sizeof s;i++)cap_alias[SW_LEASE+i]=((unsigned char*)&s)[i];__sync_synchronize();
 struct sw_lease seen;memcpy(&seen,(const void*)(cap_alias+SW_LEASE),sizeof seen);if(memcmp(&seen,&s,sizeof s))return 0;
 if(snprintf(path,sizeof path,"%s/shared-workspace.bin",dir)>=(int)sizeof path||!sw_file_new(path,&s,sizeof s))return 0;
 char proof_path[160];struct sw_owner_proof proof;
 if(!sw_owner_path(&s,proof_path)||!sw_file_read(proof_path,&proof,sizeof proof)||!sw_owner_proof_valid(&proof,&s)||
    snprintf(path,sizeof path,"%s/owner-proof.bin",dir)>=(int)sizeof path||!sw_file_new(path,&proof,sizeof proof))return 0;
 front_workspace=s;return 1;
}
static int front_workspace_wait(const char *dir){
 char path[1200],release[1200],expected_release[80];
 if(!sw_valid(&front_workspace)||snprintf(path,sizeof path,"%s/workspace-owner.ready",dir)>=(int)sizeof path||
    !sw_file_new(path,&front_workspace,sizeof front_workspace)||
    snprintf(release,sizeof release,"%s/WORKSPACE_OWNER_RELEASE",dir)>=(int)sizeof release)return 1;
 int length=snprintf(expected_release,sizeof expected_release,"NXSWRELEASE1 %u\n",front_workspace.nonce);
 uint64_t until=ns()+1800000000000ULL;
 while(!stop_requested&&ns()<until){
  char observed[80];struct stat st;if(!lstat(release,&st)){
   return st.st_size==length&&sw_file_read(release,observed,(size_t)length)&&!memcmp(observed,expected_release,(size_t)length)?0:1;
  }
  if(errno!=ENOENT)return 1;pause_ms(100);
 }
 /* Never free an allocation whose RTOS code/data or DMA retirement is not
  * established. Owner loss invalidates every subsequent borrowing attempt. */
 return 1;
}
