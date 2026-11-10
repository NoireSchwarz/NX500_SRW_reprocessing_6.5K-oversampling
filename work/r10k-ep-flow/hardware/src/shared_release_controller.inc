/* SPDX-License-Identifier: AGPL-3.0-only. Free once, only after retirement.
 * Native one-shot dispatcher801c9c50 clears each slot after callback return
 * and then clears its count. Code bytes and all active roots are checked here.
 * No freed CAP alias is read after the release call. */
#include "../../common/shared_retirement.h"
#include "../../core/src/front_layout.h"
#include "../../core/src/batch_contract.h"
#include "../../core/src/stop_contract.h"
#include "../../common/physical_ro.h"
#include "../../common/progress_emit.h"
#include "../../common/cmdq_evidence.h"
#include "../../common/allocator_observation.h"
#include "../../common/allocator_fresh_read.h"
static unsigned shared_release_samples,shared_released;
static struct pr_reader shared_reader;
static int shared_phys(uint32_t at,void*out,size_t bytes){shared_reader.fd=devfd;int ok=pr_read(&shared_reader,at,out,(unsigned)bytes);
 if(!ok&&log_file){fprintf(log_file,"SHARED_RETIREMENT_READ_FAILED address=0x%08x bytes=%u errno=%d signal=%d\n",at,(unsigned)bytes,shared_reader.last_errno,shared_reader.last_signal);fflush(log_file);}return ok;}
static int shared_retirement_sample(struct sw_front_retirement*s){
 memset(s,0,sizeof *s);unsigned char code[132];uint32_t word;
 if(!shared_phys(0x801c9c50u,code,sizeof code))return 0;s->dispatch_hash=sw_checksum(code,sizeof code);
 if(!shared_phys(0x8100f7b0u,s->slot,8)||!shared_phys(0x8153e6d4u,&s->task_vtable,4)||
    !shared_phys(0x8153e6d4u+3364u,&word,4))return 0;s->task_state=word&255u;
 if(!shared_phys(0x8153e6d4u+3204u,s->counts,8)||!shared_phys(0x8153e6d4u+3284u,s->counts+2,8)||
    !shared_phys(0x8153e6d4u+4u,s->callbacks,160)||!shared_phys(0x8153e6d4u+0x644u,s->callbacks+40,160))return 0;
 uint32_t handoff[64];memcpy(handoff,full_prefix+P48_IDLE_HANDOFF_OFFSET,sizeof handoff);
 if(!p48_idle_handoff_valid(handoff,shared_lease.nonce,1))return 0;
 for(unsigned i=0;i<14;i++){if(!shared_phys(p39_stop_addresses[i],s->current_idle+i,4))return 0;s->completed_idle[i]=handoff[8+i];}
 s->prefix_unchanged=!memcmp((const void*)shared_alias,full_prefix,SW_CONTROL);
 return sw_front_retired(s,&shared_lease);
}
static int shared_release_gate(const char*dir){
 nxl_live_progress("PIPE_RETIRE","RUNNING","Checking callbacks and DMA before native return");
 if(!shared_borrowed||!shared_front_live()||api_inflight||!trace_stopped||native_signal||
    (p7_go_sent&&(!p7_worker_exited||!p7_result_valid||!p7_safe_to_release||p7_quarantined)))return 0;
 if(!p7_shared->camera_paused&&!p7_pause())return 0;
 /* Only idle/finished work can enter this path. Guardian may safely resume
  * the UI on parent loss, while unknown native/DMA work remains held. */
 p7_shared->worker_done=1;__sync_synchronize();
 char path[1200];struct sw_front_retirement sample={0};
 for(unsigned i=0;i<16;i++){
  if(!shared_front_live()||!shared_retirement_sample(&sample)){
   snprintf(path,sizeof path,"%s/front-retirement-failed.bin",dir);p7_save(path,&sample,sizeof sample);return 0;
  }
  snprintf(path,sizeof path,"%s/front-retirement-%02u.bin",dir,i);if(!p7_save(path,&sample,sizeof sample))return 0;
  shared_release_samples++;if(i<15)pause_ms(20);
 }
 nxl_live_progress("PIPE_CALLBACKS_QUIET","RUNNING","16 callback and DMA retirement samples passed");
 uint32_t before,after;if(!pool_head(&before))return 0;
 if(!ar_copy_words(baseline_again,manager_map+0x908,AR_TABLE_WORDS))return 0;
 if(!pool_head(&after)||before!=after||!sw_allocation_in_table(&shared_lease,baseline_again,ALLOC_BLOCKS,after))return 0;
 snprintf(path,sizeof path,"%s/allocator-pre-release.bin",dir);if(!p7_save(path,baseline_again,ALLOC_BLOCKS*16u))return 0;
 snprintf(path,sizeof path,"%s/workspace-retire-ready.bin",dir);return p7_save(path,&shared_lease,sizeof shared_lease)&&shared_front_live();
}
static int shared_release_once(const char*dir){
 int ready=!release_done&&shared_release_gate(dir);pr_close(&shared_reader);
 if(!ready){nxl_live_progress("PIPE_HELD","HELD","Retirement unverified; workspace safely retained");reason="SHARED_FRONT_CALLBACK_OR_DMA_RETIREMENT_UNPROVEN";return 0;}
 /* Retirement is proven and the shell thread remains ptrace-stopped. End
  * our process-wide SIGSTOP before executing a native API: its locks and
  * mappings may need other camera threads, and our pending stop/continue
  * signals must be consumed by the normal resumed-call boundary. */
 if(!p7_resume()){reason="SHARED_RELEASE_CAMERA_RESUME_FAILED";return 0;}
 struct sw_front_retirement sample={0};
 for(unsigned i=0;i<3;i++){
  if(!shared_front_live()||!still_owned()||!shared_retirement_sample(&sample)){
   char path[1200];snprintf(path,sizeof path,"%s/front-retirement-failed.bin",dir);p7_save(path,&sample,sizeof sample);
   pr_close(&shared_reader);reason="SHARED_RETIREMENT_CHANGED_AFTER_RESUME";return 0;
  }
  if(i<2)pause_ms(20);
 }
 pr_close(&shared_reader);
 nxl_live_progress("PIPE_NATIVE_RETURN","RUNNING","Calling the original allocator once after retirement");
 event("SHARED160_RELEASE_THREADS_RUNNING");event("SHARED160_NATIVE_RELEASE_BEGIN");trace_phase="shared_release_return";
 if(!invoke(cap_base+ALLOC_RELEASE,response[0],8000)){
  fprintf(log_file,"SHARED_RELEASE_BOUNDARY trace_error=%s status=0x%08x signal=%d pc=0x%08x api_inflight=%u\n",trace_error?trace_error:"UNAVAILABLE",(unsigned)stop_status,stop_signal,stop_regs_valid?stop_regs.r[15]:0,api_inflight);fflush(log_file);
  reason="SHARED_RELEASE_CALL_NOT_CONFIRMED";return 0;
 }release_done=1;
 struct alloc_regs regs;if(!get_regs(&regs)||regs.r[0]){reason="SHARED_RELEASE_API_FAILED";return 0;}
 return 1;
}
struct ar_return_probe {
 uint32_t attempt,consecutive,header_mask,copied_a,copied_b,tables_equal,first_changed_word,counts[3];
 uint32_t hint_checked,hint_matches_old,bulk_skipped;
 uint64_t elapsed_ns;struct sw_return_observation range;
};
static int ar_probe_report(const char*dir,const char*name,const struct ar_return_probe*p,unsigned verified,unsigned clock_failed){
 char path[1200];snprintf(path,sizeof path,"%s/%s",dir,name);FILE*f=fopen(path,"wx");if(!f)return 0;
 const struct sw_return_observation*r=&p->range;
 fprintf(f,"{\"format\":\"NXALLOCATORRETURN1\",\"read_only\":true,\"native_release_calls\":1,\"native_release_retried\":false,\"word_read_bytes\":4,\"fresh_page_mappings\":true,\"wait_limit_ms\":3000,\"attempt\":%u,\"consecutive\":%u,\"elapsed_ms\":%llu,\"hint_checked\":%u,\"hint_matches_old\":%u,\"bulk_skipped\":%u,\"header_mask\":%u,\"copied_a\":%u,\"copied_b\":%u,\"free_a\":%u,\"free_b\":%u,\"free_c\":%u,\"tables_equal\":%u,\"first_changed_word\":%u,\"strict_range_code\":%u,\"strict_range_index\":%u,\"strict_range_kind\":%u,\"strict_range_span\":%u,\"free_counted\":%u,\"free_reported\":%u,\"lease_zero_metadata_blocks\":%u,\"old_head_matches\":%u,\"old_head_observed\":[%u,%u,%u,%u],\"return_verified\":%u,\"clock_failed\":%u}\n",p->attempt,p->consecutive,(unsigned long long)(p->elapsed_ns/1000000ULL),p->hint_checked,p->hint_matches_old,p->bulk_skipped,p->header_mask,p->copied_a,p->copied_b,p->counts[0],p->counts[1],p->counts[2],p->tables_equal,p->first_changed_word,r->code,r->index,r->kind,r->span,r->free_seen,r->free_reported,r->lease_zero_blocks,r->head_matches_old,r->head_words[0],r->head_words[1],r->head_words[2],r->head_words[3],verified,clock_failed);
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static void ar_save_pair(const char*dir,const char*which,const struct ar_return_probe*p){
 char path[1200];if(p->copied_a){snprintf(path,sizeof path,"%s/allocator-return-%s-a.bin",dir,which);(void)p7_save(path,baseline,ALLOC_BLOCKS*16u);}
 if(p->copied_b){snprintf(path,sizeof path,"%s/allocator-return-%s-b.bin",dir,which);(void)p7_save(path,baseline_again,ALLOC_BLOCKS*16u);}
}
static int ar_take_full_probe(struct ar_return_probe*p){
 if(af_pool_head(devfd,p->counts))p->header_mask|=1;
 if(p->header_mask&1)p->copied_a=(unsigned)af_read_words(devfd,AF_TABLE_PA,baseline,AR_TABLE_WORDS);
 if(af_pool_head(devfd,p->counts+1))p->header_mask|=2;
 if(p->header_mask&2)p->copied_b=(unsigned)af_read_words(devfd,AF_TABLE_PA,baseline_again,AR_TABLE_WORDS);
 if(af_pool_head(devfd,p->counts+2))p->header_mask|=4;
 if(p->copied_a&&p->copied_b){p->tables_equal=!memcmp(baseline,baseline_again,ALLOC_BLOCKS*16u);
  if(!p->tables_equal)for(unsigned i=0;i<AR_TABLE_WORDS;i++)if(baseline[i]!=baseline_again[i]){p->first_changed_word=i;break;}}
 int range_ok=p->copied_a&&sw_return_observe(&shared_lease,baseline,ALLOC_BLOCKS,p->counts[2],&p->range);
 return p->header_mask==7&&p->copied_a&&p->copied_b&&p->counts[0]==p->counts[1]&&p->counts[1]==p->counts[2]&&p->tables_equal&&range_ok;
}
static int shared_release_confirm(const char*dir){
 if(!release_done||api_inflight){reason="SHARED_RELEASE_CALL_INCOMPLETE";return 0;}
 if(!sw_valid(&shared_lease)||(uint64_t)shared_lease.index+SW_BYTES/16384u>ALLOC_BLOCKS){reason="SHARED_RELEASE_RECEIPT_INVALID";return 0;}
 /* The native call has completed. The CAP alias must not be dereferenced
  * again, including its footer or former code prefix. Inspect allocator only. */
 nxl_live_progress("PIPE_ALLOCATOR_VERIFY","RUNNING","Checking stable allocator tables after native return");
 unsigned stable=0,clock_failed=0;char path[1200];uint64_t begin=ns(),previous=begin;
 struct ar_return_probe probe={0};snprintf(path,sizeof path,"%s/allocator-return.csv",dir);FILE*audit=fopen(path,"wx");
 if(audit)fputs("attempt,elapsed_ms,hint_matches_old,bulk_skipped,header_mask,free_a,free_b,free_c,tables_equal,first_changed_word,range_pass,range_code,range_index,zero_metadata_blocks,old_head_matches,consecutive\n",audit);
 if(!begin)clock_failed=1;
 for(unsigned attempt=0;!clock_failed&&!stop_requested&&attempt<AR_SAMPLE_LIMIT;attempt++){
  uint64_t before=ns();if(before<previous||(attempt&&before==previous)){clock_failed=1;break;}if(before-begin>=AR_WAIT_NS)break;previous=before;
  memset(&probe,0,sizeof probe);probe.attempt=attempt+1;probe.first_changed_word=UINT32_MAX;probe.range.code=SW_RETURN_INPUT_INVALID;probe.range.index=UINT32_MAX;
  uint32_t hint[4],count=0;
  if(af_pool_head(devfd,&count))probe.hint_checked=(unsigned)af_read_words(devfd,AF_TABLE_PA+shared_lease.index*16u,hint,4);
  probe.hint_matches_old=probe.hint_checked&&!memcmp(hint,shared_lease.metadata,16);
  /* An unchanged old head is only a negative hint. It can skip expensive
   * repeated scans, but can never certify a return. The first and terminal
   * full snapshots are always retained for diagnosis. */
  probe.bulk_skipped=attempt&&probe.hint_matches_old;
  if(probe.bulk_skipped){probe.header_mask=1;probe.counts[0]=count;probe.range.head_matches_old=1;memcpy(probe.range.head_words,hint,16);}
  int same=!probe.bulk_skipped&&ar_take_full_probe(&probe);
  int range_ok=probe.copied_a&&probe.range.code==SW_RETURN_OK;
  stable=same?stable+1:0;probe.consecutive=stable;
  uint64_t now=ns();if(now<previous){clock_failed=1;break;}previous=now;probe.elapsed_ns=now-begin;
  if(audit)fprintf(audit,"%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",probe.attempt,(unsigned long long)(probe.elapsed_ns/1000000ULL),probe.hint_matches_old,probe.bulk_skipped,probe.header_mask,probe.counts[0],probe.counts[1],probe.counts[2],probe.tables_equal,probe.first_changed_word,(unsigned)range_ok,probe.range.code,probe.range.index,probe.range.lease_zero_blocks,probe.range.head_matches_old,stable);
  if(attempt==0){ar_save_pair(dir,"first",&probe);(void)ar_probe_report(dir,"allocator-return-first.json",&probe,0,clock_failed);}
  if(stable>=3&&probe.elapsed_ns>=100000000ULL){free_after=probe.counts[2];return_verified=1;break;}
  if(probe.elapsed_ns>=AR_WAIT_NS)break;
  /* Failed full views back off; successful proof samples keep their short
   * spacing. An absent old head must not cause hundreds of full rescans. */
  pause_ms(probe.bulk_skipped?100:stable?50:attempt<8?250:500);
 }
 if(!return_verified&&probe.attempt==AR_SAMPLE_LIMIT)clock_failed=1;
 if(!return_verified){unsigned attempted=probe.attempt;memset(&probe,0,sizeof probe);probe.attempt=attempted+1;probe.first_changed_word=UINT32_MAX;probe.range.code=SW_RETURN_INPUT_INVALID;probe.range.index=UINT32_MAX;(void)ar_take_full_probe(&probe);uint64_t last=ns();if(last>=begin)probe.elapsed_ns=last-begin;
  if(audit)fprintf(audit,"%u,%llu,0,0,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,0\n",probe.attempt,(unsigned long long)(probe.elapsed_ns/1000000ULL),probe.header_mask,probe.counts[0],probe.counts[1],probe.counts[2],probe.tables_equal,probe.first_changed_word,(unsigned)(probe.copied_a&&probe.range.code==SW_RETURN_OK),probe.range.code,probe.range.index,probe.range.lease_zero_blocks,probe.range.head_matches_old);}
 if(audit){(void)fflush(audit);(void)fsync(fileno(audit));(void)fclose(audit);}
 (void)ar_probe_report(dir,"allocator-return-final.json",&probe,return_verified,clock_failed);
 fprintf(log_file,"ALLOCATOR_RETURN_PROOF attempts=%u elapsed_ms=%llu header_mask=%u free=%u,%u,%u tables_equal=%u range_code=%u range_index=%u zero_metadata_blocks=%u old_head_matches=%u consecutive=%u verified=%u clock_failed=%u\n",probe.attempt,(unsigned long long)(probe.elapsed_ns/1000000ULL),probe.header_mask,probe.counts[0],probe.counts[1],probe.counts[2],probe.tables_equal,probe.range.code,probe.range.index,probe.range.lease_zero_blocks,probe.range.head_matches_old,probe.consecutive,return_verified,clock_failed);fflush(log_file);
 (void)cq_capture(dir,"AFTER_RELEASE_CHECK");
 if(!return_verified){ar_save_pair(dir,"last",&probe);reason="SHARED_ALLOCATOR_RETURN_NOT_VERIFIED";return 0;}
 snprintf(path,sizeof path,"%s/allocator-returned.bin",dir);if(!p7_save(path,baseline,ALLOC_BLOCKS*16u)){reason="SHARED_RETURN_AUDIT_SAVE_FAILED";return 0;}
 unsigned phase=shared_lease.phase;if(!sw_transition(&shared_lease,phase,SW_RELEASED)){reason="SHARED_RELEASE_RECEIPT_INVALID";return 0;}
 snprintf(path,sizeof path,"%s/workspace-released.bin",dir);if(!p7_save(path,&shared_lease,sizeof shared_lease)){reason="SHARED_RELEASE_RECEIPT_SAVE_FAILED";return 0;}
 shared_released=1;(void)cq_capture(dir,"AFTER_RELEASE");nxl_live_progress("PIPE_RELEASED","RUNNING","Original allocator return verified");event("SHARED160_ALLOCATOR_RETURN_VERIFIED");return 1;
}
