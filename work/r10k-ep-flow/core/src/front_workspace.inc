/* SPDX-License-Identifier: AGPL-3.0-only. Export only after verified front cleanup. */
#include "../../common/shared_workspace_io.h"
#include "../../hardware/src/lut_cpu.h"
static struct sw_lease front_workspace;
_Static_assert(P39_CAP_BYTES==SW_BYTES&&P39_TARGET_OFFSET==SW_INPUT&&P39_TARGET_STRIDE==SW_FRONT_STRIDE&&P39_UV_OFFSET==SW_FRONT_UV,"shared strided ISP layout");
static unsigned shared_owner_mode;
static int front_export_reject(const char*stage,int error){
 if(log_file){fprintf(log_file,"FRONT_WORKSPACE_EXPORT result=REJECT stage=%s errno=%d va=0x%08x pa=0x%08x capacity=%u va_mod4K=%u pa_mod16K=%u nonce=%u frames=%u native_done=%u\n",stage,error,response[0],response[1],SW_BYTES,response[0]&4095u,response[1]&16383u,nonce,frames_complete,p49_native_done);fflush(log_file);}
 return 0;
}
static int front_workspace_export(const char *dir){
 if(!shared_owner_mode)return 1;
#define FW_REQUIRE(c,stage) do{errno=0;if(!(c))return front_export_reject(stage,errno);}while(0)
 FW_REQUIRE(cap_alias&&frames_complete==1,"FRAME_CONTEXT");
 FW_REQUIRE(front_handoff_released(dir,"export"),"FRONT_RETIREMENT");
 struct sw_lease s;memset(&s,0,sizeof s);s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;
 s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;
 s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;
 s.pa=response[1];s.va=response[0];s.nonce=nonce;s.owner_pid=(uint32_t)getpid();s.index=index_owned;
 s.front_return_barrier=frames_complete==1&&p49_native_done&&queued_status_sent&&liveness_attempted;
 s.frame_callbacks_retired=front_handoff_last.readiness.ready;s.controller_slot_restored=slot_original();
 FW_REQUIRE(sw_owner_ticks(s.owner_pid,&s.owner_start_ticks),"OWNER_START_TICKS");memcpy(s.metadata,owned_meta,sizeof s.metadata);
 FW_REQUIRE(p48_source,"SOURCE_IDENTITY");memcpy(s.source_sha256,p48_source->source_sha256,64);
 struct sha256 hash;unsigned char digest[32];sha256_init(&hash);sha256_update(&hash,(const void*)cap_alias,SW_CONTROL);sha256_final(&hash,digest);digest_hex(digest,s.prefix_sha256);
 char path[1200];FW_REQUIRE(snprintf(path,sizeof path,"%s/srw-lut.bin",dir)<(int)sizeof path,"LUT_PATH");
 unsigned char table[LUT_SLOT_BYTES];struct stat st;errno=0;
 if(lstat(path,&st)){if(errno!=ENOENT)return front_export_reject("LUT_STATUS",errno);lut_make_table(table,1);FW_REQUIRE(sw_file_new(path,table,sizeof table),"LUT_CREATE");}
 FW_REQUIRE(sw_file_read(path,table,sizeof table)&&lut_table_valid(table,sizeof table),"LUT_FORMAT");
 FW_REQUIRE(p48_file_sha(path,s.lut_sha256),"LUT_HASH");
 sw_seal(&s);FW_REQUIRE(sw_valid(&s),"LEASE_CONTRACT");FW_REQUIRE(still_owned(),"ALLOCATION_OWNERSHIP");
 FW_REQUIRE(sw_owner_proof_publish(&s),"OWNER_PROOF_PUBLISH");
 /* The code/control prefix and native returned VA remain unchanged. The
  * allocation is handed off only after all original retirement gates. */
 for(unsigned i=0;i<16384;i++)cap_alias[SW_LEASE+i]=0;
 for(unsigned i=0;i<sizeof s;i++)cap_alias[SW_LEASE+i]=((unsigned char*)&s)[i];__sync_synchronize();
 struct sw_lease seen;memcpy(&seen,(const void*)(cap_alias+SW_LEASE),sizeof seen);FW_REQUIRE(!memcmp(&seen,&s,sizeof s),"LEASE_MEMORY_READBACK");
 FW_REQUIRE(snprintf(path,sizeof path,"%s/shared-workspace.bin",dir)<(int)sizeof path&&sw_file_new(path,&s,sizeof s),"LEASE_RECORD");
 char proof_path[160];struct sw_owner_proof proof;
 FW_REQUIRE(sw_owner_path(&s,proof_path)&&sw_file_read(proof_path,&proof,sizeof proof)&&sw_owner_proof_valid(&proof,&s),"OWNER_PROOF_READBACK");
 FW_REQUIRE(snprintf(path,sizeof path,"%s/owner-proof.bin",dir)<(int)sizeof path&&sw_file_new(path,&proof,sizeof proof),"OWNER_RECORD");
 front_workspace=s;
 if(log_file){fprintf(log_file,"FRONT_WORKSPACE_EXPORT result=COMMITTED stage=COMPLETE va=0x%08x pa=0x%08x capacity=%u nonce=%u\n",s.va,s.pa,s.capacity,s.nonce);fflush(log_file);}
#undef FW_REQUIRE
 return 1;
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
