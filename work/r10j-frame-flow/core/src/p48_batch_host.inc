/* Linux-only sequential driver evidence; no scheduler or boot inference. */
#include <sys/statvfs.h>
static int p48_space_preflight(const char *dir){struct statvfs s;if(statvfs(dir,&s))return 0;uint64_t block=s.f_frsize?s.f_frsize:s.f_bsize;if(!block)return 0;
#ifdef NXL_FAST_PIPELINE
 /* Control records live on tmpfs; final JPEG capacity is checked by main. */
 return (uint64_t)s.f_bavail>=(8388608ULL+block-1)/block;
#else
 return (uint64_t)s.f_bavail>=(536870912ULL+block-1)/block;
#endif
}
static int p48_file_sha(const char *path,char out[65]){
 FILE *f=fopen(path,"rb");if(!f)return 0;struct sha256 s;sha256_init(&s);unsigned char block[65536],d[32];size_t n;
 while((n=fread(block,1,sizeof block,f))){if(stop_requested){fclose(f);return 0;}sha256_update(&s,block,n);}int ok=!ferror(f);if(fclose(f))ok=0;
 if(ok){sha256_final(&s,d);digest_hex(d,out);}return ok;
}
static int p48_fsync_dir(const char *dir){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 0;int ok=!fsync(fd);if(close(fd))ok=0;return ok;}
static int p48_atomic_text(const char *dir,const char *name,const char *text){
 char path[1200],pending[1200];if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path||snprintf(pending,sizeof pending,"%s/%s.pending",dir,name)>=(int)sizeof pending)return 0;
 FILE *f=fopen(pending,"w");if(!f)return 0;int ok=fputs(text,f)>=0&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;
 if(!ok||rename(pending,path)){unlink(pending);return 0;}return p48_fsync_dir(dir);
}
static int p48_progress(const char *stage){
 char text[256];if(!batch_directory||p48_case_index>=P49_FRAME_COUNT)return 0;
 snprintf(text,sizeof text,"NXP48PROGRESS1\nframe_index=%u\nframes_total=1\nstage=%s\n",p48_case_index,stage);return p48_atomic_text(batch_directory,"P48_PROGRESS",text);
}
static int p48_preflight_assets(void){
 const char *selected=getenv("NX51_SOURCE_SRW");
 if(selected&&*selected){if(!selected_prepare(selected,getenv("NX51_SOURCE_SHA")))return 0;selected_settings_error="SOURCE_SETTINGS_EVIDENCE_SAVE_FAILED";int ok=selected_report(batch_directory);if(ok)selected_settings_error="";return ok;}
 
#ifdef NXL_FAST_PIPELINE
 selected_settings_error="SELECT_CAMERA_SRW_FOR_NATIVE_SETTINGS";return 0;
#endif
 char path[600],hash[65];for(unsigned i=0;i<P48_SOURCE_COUNT;i++){
  snprintf(path,sizeof path,"/mnt/mmc/nx-lut-demo/assets/%s",p48_sources[i].asset_file);struct stat st;
  if(stat(path,&st)||!S_ISREG(st.st_mode)||(uint64_t)st.st_size!=p48_sources[i].file_bytes||!p48_file_sha(path,hash)||strcmp(hash,p48_sources[i].source_sha256))return 0;
#ifndef NXL_FAST_PIPELINE
  snprintf(path,sizeof path,"/mnt/mmc/nx-lut-demo/assets/%s",p48_sources[i].row_file);
  if(stat(path,&st)||!S_ISREG(st.st_mode)||st.st_size!=138768||!p48_file_sha(path,hash)||strcmp(hash,p48_sources[i].rows_sha256))return 0;
#endif
 }return 1;
}
static int p48_read_reply(unsigned phase){
 if(!cap_alias||p48_case_index>=P49_FRAME_COUNT)return 0;uint32_t a[64],b[64];
 const volatile uint32_t *at=(const volatile uint32_t*)(cap_alias+P48_BATCH_CONTROL_OFFSET+0x100);
 for(unsigned i=0;i<64;i++)a[i]=at[i];__sync_synchronize();for(unsigned i=0;i<64;i++)b[i]=at[i];
 if(memcmp(a,b,sizeof a)||!p48_batch_reply_valid(a,nonce,p48_case_index+1,P49_FRAME_COUNT)||a[P48_R_PHASE]!=phase||a[P48_R_STALE_IRQ_COUNT]||a[P48_R_FOREIGN_IRQ_COUNT])return 0;
 if(a[P48_R_REQUEST_CRC]!=p48_request[P48_Q_CRC])return 0;
 if(phase==P48_PHASE_DONE&&(a[P48_R_SOURCE_FNV]!=source_fnv||a[P48_R_TARGET_FNV]!=observed_result.w[23]))return 0;
 memcpy(p48_reply,a,sizeof a);return a[P48_R_LISTENER_PA]==p48_batch_listener(response[1],p48_case_index)&&a[P48_R_SOURCE_ID]==p48_case->source&&a[P48_R_PLAN_FLAGS]==(p48_case->wb|p48_case->cc<<1);
}
static int p48_prepare_query(void){
 memset(p48_request,0,sizeof p48_request);unsigned seq=p48_case_index+1;uint32_t *q=p48_request;
 q[0]=P48_BATCH_REQUEST_MAGIC;q[1]=1;q[2]=nonce;q[3]=seq;q[4]=seq-1;q[5]=P49_FRAME_COUNT;q[6]=p48_case->source;q[7]=p48_case->wb|p48_case->cc<<1;q[8]=source_fnv;q[9]=P39_SOURCE_BYTES;
 q[10]=p48_batch_crc((const void*)(cap_alias+P41_OFFSET),256);q[11]=p48_batch_crc((const void*)(cap_alias+P48_CC_OFFSET),256);
 for(unsigned i=0;i<32;i++){unsigned x=0;if(sscanf(p48_source->source_sha256+2*i,"%2x",&x)!=1)return 0;((unsigned char*)(q+12))[i]=(unsigned char)x;}
 q[20]=P39_TARGET_BYTES;q[30]=p48_batch_crc(q,120);q[31]=0;
 volatile uint32_t *at=(volatile uint32_t*)(cap_alias+P48_BATCH_CONTROL_OFFSET);
 at[31]=0;__sync_synchronize();for(unsigned i=0;i<31;i++)at[i]=q[i];__sync_synchronize();return 1;
}
static int p48_snapshot_valid(const unsigned char *prefix){
 if(!prefix||p48_case_index>=P49_FRAME_COUNT)return 0;
 const uint32_t *initial=(const uint32_t*)(prefix+P48_INITIAL_STOP_OFFSET);
 const uint32_t *q=(const uint32_t*)(prefix+P48_BATCH_CONTROL_OFFSET),*saved=q+32,*reply=q+64,*abort_area=q+128;
 const uint32_t *wb=(const uint32_t*)(prefix+P41_OFFSET),*cc=(const uint32_t*)(prefix+P48_CC_OFFSET),*frozen=(const uint32_t*)(prefix+P48_BATCH_FROZEN_OFFSET);
 if(memcmp(reply,p48_reply,256)||memcmp(q,p48_request,128)||memcmp(wb,p41_info,256)||memcmp(cc,expected+P48_CC_OFFSET,256)||memcmp(saved,p48_saved,128))return 0;
 if(!p48_batch_request_valid(q,nonce,p48_case_index+1,P49_FRAME_COUNT,p48_case->source,p48_case->wb|p48_case->cc<<1)||!p48_batch_reply_valid(reply,nonce,p48_case_index+1,P49_FRAME_COUNT)||reply[P48_R_PHASE]!=P48_PHASE_DONE||reply[P48_R_STALE_IRQ_COUNT]||reply[P48_R_FOREIGN_IRQ_COUNT])return 0;
 for(unsigned i=0;i<16;i++)if(abort_area[i])return 0;
 for(unsigned i=144;i<256;i++)if(q[i])return 0;
 for(unsigned i=P48_CC_SNAPSHOT_START+P48_CC_SNAPSHOT_WORDS*P48_CC_SNAPSHOTS;i<P48_CC_WORDS;i++)if(cc[i])return 0;
 if(!p48_batch_frozen_valid(frozen,nonce)||(p48_case_index&&memcmp(frozen,expected+P48_BATCH_FROZEN_OFFSET,256)))return 0;
 if(frozen[P48_F_INITIAL_STOP_PA]!=response[1]+P48_INITIAL_STOP_OFFSET||frozen[P48_F_INITIAL_STOP_CRC]!=p48_batch_crc(initial,P48_INITIAL_STOP_BYTES)||!p48_stop_record_valid((const uint32_t*)(prefix+P39_STOP_OFFSET),initial,nonce,response[1],p48_case_index+1))return 0;
 const uint32_t *h=(const uint32_t*)(prefix+P48_IDLE_HANDOFF_OFFSET),*prev=(const uint32_t*)(prefix+P48_PREVIOUS_IDLE_OFFSET),*stop=(const uint32_t*)(prefix+P39_STOP_OFFSET);
 if(!p48_idle_handoff_valid(h,nonce,p48_case_index+1)||h[62]!=reply[P48_R_IDLE_HANDOFF_CRC]||h[5]!=initial[5]||h[6]!=initial[6]||h[7]!=frozen[P48_F_INITIAL_STOP_CRC])return 0;
 if(!p48_case_index){for(unsigned i=0;i<64;i++)if(prev[i])return 0;if(reply[P48_R_PREVIOUS_IDLE_CRC])return 0;}
 else{
  if(memcmp(prev,p48_expected_previous_idle,256))return 0;
  if(!p48_idle_handoff_valid(prev,nonce,p48_case_index)||prev[62]!=reply[P48_R_PREVIOUS_IDLE_CRC]||stop[20]!=prev[62]||prev[5]!=initial[5]||prev[6]!=initial[6]||prev[7]!=frozen[P48_F_INITIAL_STOP_CRC])return 0;
  for(unsigned i=0;i<14;i++)if(stop[32+i]!=prev[8+i]||stop[48+i]!=prev[8+i])return 0;
 }
 if(p48_case_index&&memcmp(initial,expected+P48_INITIAL_STOP_OFFSET,P48_INITIAL_STOP_BYTES))return 0;
 return p41_audit_valid(wb,nonce)&&p48_cc_audit_valid(cc,nonce)&&cc[3]==p48_case->cc&&
  (cc[3]?p49_pd_single_audit_valid((const uint32_t*)(prefix+P49_PD_OFFSET),nonce):p49_pd_factory_audit_valid((const uint32_t*)(prefix+P49_PD_OFFSET),nonce));
}
static int p48_publish_query(void){
 if(!camera_paused||!camera_threads_stopped()||!p48_read_reply(P48_PHASE_READY))return 0;
 p48_request[31]=p48_case_index+1;if(!p48_batch_request_valid(p48_request,nonce,p48_case_index+1,P49_FRAME_COUNT,p48_case->source,p48_case->wb|p48_case->cc<<1))return 0;
 ((volatile uint32_t*)(cap_alias+P48_BATCH_CONTROL_OFFSET))[31]=p48_case_index+1;__sync_synchronize();return 1;
}
static void p48_abort(unsigned reason_code){
 if(!cap_alias||p48_case_index>=P49_FRAME_COUNT||!nonce)return;uint32_t a[16]={P48_BATCH_ABORT_MAGIC,1,nonce,p48_case_index+1,reason_code};
 a[14]=p48_batch_crc(a,56);a[15]=p48_batch_token(nonce,p48_case_index+1,0,a[14]);
 volatile uint32_t *at=(volatile uint32_t*)(cap_alias+P48_BATCH_CONTROL_OFFSET+0x200);for(unsigned i=0;i<15;i++)at[i]=a[i];__sync_synchronize();at[15]=a[15];__sync_synchronize();
}
static int p48_publish_saved(const char *dir){
 if(!p48_snapshot_valid((const unsigned char*)cap_alias)||!p48_read_reply(P48_PHASE_DONE)||memcmp(p48_reply,p48_control_copy+0x100,256)||!p48_batch_request_valid(p48_request,nonce,p48_case_index+1,P49_FRAME_COUNT,p48_case->source,p48_case->wb|p48_case->cc<<1))return 0;
 uint32_t *s=p48_saved;memset(s,0,128);s[0]=P48_BATCH_SAVED_MAGIC;s[1]=1;s[2]=nonce;s[3]=p48_case_index+1;s[4]=p48_case_index;s[5]=source_fnv;s[6]=target_fnv;s[7]=1;s[8]=p48_request[30];s[9]=p48_reply[62];s[30]=p48_batch_crc(s,120);s[31]=p48_case_index+1;
 if(!p48_batch_saved_valid(s,nonce,p48_case_index+1,p48_request,p48_reply))return 0;
 char path[1200];snprintf(path,sizeof path,"%s/p48_saved.bin",dir);FILE *f=fopen(path,"wx");if(!f)return 0;int ok=fwrite(s,1,128,f)==128&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;if(!ok||!p48_fsync_dir(dir))return 0;
 volatile uint32_t *at=(volatile uint32_t*)(cap_alias+P48_BATCH_CONTROL_OFFSET+0x80);for(unsigned i=0;i<31;i++)at[i]=s[i];__sync_synchronize();at[31]=s[31];__sync_synchronize();
 for(unsigned i=0;i<32;i++)if(at[i]!=s[i])return 0;memcpy(expected+P48_BATCH_CONTROL_OFFSET+0x80,s,128);return 1;
}
