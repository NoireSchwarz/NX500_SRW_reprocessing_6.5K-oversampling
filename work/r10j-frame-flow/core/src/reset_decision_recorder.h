/* SPDX-License-Identifier: AGPL-3.0-only. Read-only observations, no authority
 * to free, unlock, acquire native resources or bypass fresh worker admission. */
#ifndef NX_RESET_DECISION_RECORDER_H
#define NX_RESET_DECISION_RECORDER_H
#include <stdarg.h>
static FILE *decision_stream;
static unsigned decision_failed;
static unsigned long long decision_time(void){
 struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
 return (unsigned long long)t.tv_sec*1000000000u+(unsigned long long)t.tv_nsec;
}
static void decision_event(const char *format,...){
 if(!decision_stream||decision_failed)return;
 va_list args;va_start(args,format);if(vfprintf(decision_stream,format,args)<0)decision_failed=1;va_end(args);
 if(fflush(decision_stream))decision_failed=1;
}
static int decision_begin(void){
 char path[1300];if(snprintf(path,sizeof path,"%s/RESET_TIMELINE.jsonl",output)>=(int)sizeof path)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){decision_failed=1;return 0;}
 decision_stream=fdopen(fd,"w");if(!decision_stream){close(fd);decision_failed=1;return 0;}
 decision_event("{\"format\":\"NX51_RESET_DECISION_R7K2\",\"mono_ns\":%llu,\"job\":\"%s\",\"previous\":\"%s\",\"token\":\"%s\",\"scope\":\"PRIOR_MOD_RESOURCE_RETIREMENT\",\"new_native_job_authorized\":false}\n",decision_time(),job,previous,token);
 return !decision_failed;
}
static void decision_allocator(const uint32_t *table,unsigned pass){
 unsigned overlaps=0,recorded=0;
 decision_event("{\"event\":\"allocator_snapshot\",\"mono_ns\":%llu,\"pass\":%u,\"front_pa\":%u,\"front_bytes\":%u,\"backend_pa\":%u,\"backend_bytes\":%u,\"front_range_free\":%u,\"backend_range_free\":%u,\"ownership_when_occupied\":\"UNPROVEN_WITHOUT_ALLOCATION_GENERATION\",\"atomic\":false}\n",decision_time(),pass,old_pa,r7_front_bytes,backend_pa,r7_backend_bytes,old_free,backend_free);
 decision_event("{\"event\":\"allocation_lifetime\",\"pass\":%u,\"layout_verified\":%u,\"bad_index\":%u,\"front_state\":\"%s\",\"backend_state\":\"%s\",\"allocation_heads\":%u}\n",pass,lifetime.valid,lifetime.bad_index,r7_range_name(lifetime.front_state),r7_range_name(lifetime.backend_state),lifetime.head_count);
 if(!lifetime.valid)return;
 for(unsigned i=0;i<R7_BLOCKS;){
  unsigned index=i;const uint32_t *entry=table+i*4;uint32_t span=entry[1];
  if(!(entry[0]&255u)){i++;continue;}i+=span;
  uint64_t lo=0x94000000ULL+(uint64_t)index*16384u,hi=lo+(uint64_t)span*16384u;
  unsigned f=lo<(uint64_t)old_pa+r7_front_bytes&&hi>old_pa;
  unsigned b=lo<(uint64_t)backend_pa+r7_backend_bytes&&hi>backend_pa;
  if(!f&&!b)continue;overlaps++;
  if(recorded++<32)decision_event("{\"event\":\"overlapping_allocation_head\",\"mono_ns\":%llu,\"pass\":%u,\"index\":%u,\"pa\":%llu,\"blocks\":%u,\"entry_words\":[%u,%u,%u,%u],\"overlaps_front\":%u,\"overlaps_backend\":%u}\n",decision_time(),pass,index,(unsigned long long)lo,span,entry[0],entry[1],entry[2],entry[3],f,b);
 }
 decision_event("{\"event\":\"overlap_summary\",\"pass\":%u,\"overlaps\":%u,\"entry_records\":%u,\"full_table\":\"allocator-%u.bin\"}\n",pass,overlaps,overlaps>32?32:overlaps,pass);
}
static void decision_backend(const struct r7_backend_sample*s,unsigned sample,const char *error,int stable){
 decision_event("{\"event\":\"transaction_observation\",\"mono_ns\":%llu,\"sample\":%u,\"kernel_selector\":%u,\"owner\":%u,\"mutex\":[%u,%u,%u],\"jpeg_activity\":[%u,%u],\"global_rtos\":[%u,%u,%u,%u,%u],\"global_irq_mask\":[%u,%u,%u,%u,%u],\"global_pending\":[%u,%u,%u,%u,%u],\"global_otf\":[%u,%u],\"dma_old_range_mask\":%u,\"latched_jpeg_old_range_mask\":%u,\"background_activity\":%u,\"owned_state_stable\":%u,\"transaction_error\":\"%s\",\"raw_sample\":\"backend-sample-%02u.bin\"}\n",
 decision_time(),sample,ep_sync_word(s->kernel+0x400),s->owner,s->mutex[0],s->mutex[1],s->mutex[2],s->jpeg[0]&3u,s->jpeg[4]&3u,
 s->rtos[0],s->rtos[1],s->rtos[2],s->rtos[3],s->rtos[4],s->mask[0],s->mask[1],s->mask[2],s->mask[3],s->mask[4],
 s->raw[0],s->raw[1],s->raw[2],s->raw[3],s->raw[4],s->otf[0],s->otf[1],r7_backend_dma_refs(s,old_pa,backend_pa),
 r7_backend_latched_jpeg_refs(s,old_pa,backend_pa),r7_backend_background_activity(s),stable?1u:0u,error?error:"NONE",sample);
}
static int decision_finish(int rc){
 if(decision_stream){
  decision_event("{\"event\":\"decision\",\"mono_ns\":%llu,\"verdict\":\"%s\",\"reason\":\"%s\",\"failure_mask\":%u,\"front_range_free\":%u,\"backend_range_free\":%u,\"fresh_front_preflight_required\":true,\"fresh_JPEG_preflight_required\":true,\"new_native_job_authorized\":false}\n",decision_time(),rc?"HELD":"RETIRED_NEEDS_INIT",reason,category_failures,old_free,backend_free);
  if(fflush(decision_stream)||fsync(fileno(decision_stream)))decision_failed=1;
  if(fclose(decision_stream))decision_failed=1;decision_stream=NULL;
 }
 char text[1024];int n=snprintf(text,sizeof text,"NX51RESETDECISION1\nreason=%s\nverdict=%s\nfailure_mask=%u\nend=NX51RESETDECISIONEND1\n",reason,rc?"HELD":"RETIRED_NEEDS_INIT",category_failures);
 if(n<=0||n>=(int)sizeof text||!save("RESET_DECISION.txt",text,(size_t)n))decision_failed=1;
 return !decision_failed;
}
#endif
