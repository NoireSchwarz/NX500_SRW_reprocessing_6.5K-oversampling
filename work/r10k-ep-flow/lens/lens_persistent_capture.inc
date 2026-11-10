/* SPDX-License-Identifier: AGPL-3.0-only. Original typed LDC LUT packet and
 * its two retained RTOS banks. Ordinary RAM only; no coefficient analysis,
 * native calls, ISP registers, image buffers or dependence on EP state2..4.
 * A complete read is not an atomic snapshot or proof of a hardware write.
 */
#define LPR_INPUT 0x8142eea4u
#define LPR_STORAGE 0x8142efc8u
#define LPR_CTRL0 0x8142f434u /* Original getter: guard at f430, object at f434. */
#define LPR_CTRL1 0x8142f450u
struct lpr_sample {
 uint32_t schema,status,available_mask,source_address,source_bytes,storage_address,storage_bytes;
 uint32_t controls_before[2],controls_after[2],source_type_before,source_type_after;
 uint32_t source[68],storage[282];
};
_Static_assert(sizeof(struct lpr_sample)==1452,"fixed persistent LDC ABI");
static uint64_t persistent_samples,persistent_records,persistent_failures,persistent_changed,persistent_last_record;
static struct lpr_sample persistent_previous;
static int lens_persistent_read(struct lpr_sample*s){
 memset(s,0,sizeof *s);s->schema=1;s->source_address=LPR_INPUT;s->source_bytes=272;s->storage_address=LPR_STORAGE;s->storage_bytes=1128;
 if(!block(LPR_CTRL0,s->controls_before,1)||!block(LPR_CTRL1,s->controls_before+1,1)||!block(LPR_INPUT,&s->source_type_before,1)){s->status=3;return -1;}
 if(s->controls_before[0]!=0x80582d10u||s->controls_before[1]!=0x80582da8u||s->source_type_before!=0x80713a18u){s->status=2;return 1;}
 if(block(LPR_INPUT,s->source,68))s->available_mask|=1;
 if(block(LPR_STORAGE,s->storage,282))s->available_mask|=2;
 if(!block(LPR_CTRL0,s->controls_after,1)||!block(LPR_CTRL1,s->controls_after+1,1)||!block(LPR_INPUT,&s->source_type_after,1)||s->available_mask!=3){s->status=3;return -1;}
 if(memcmp(s->controls_before,s->controls_after,8)||s->source_type_before!=s->source_type_after){s->status=4;return 0;}
 s->status=1;return 1;
}
static void lens_persistent_sample(unsigned sequence){
 uint64_t begin=now();struct lpr_sample sample;int result=lens_persistent_read(&sample);persistent_samples++;
 if(result<0)persistent_failures++;if(!result)persistent_changed++;
 if(!persistent_last_record||memcmp(&sample,&persistent_previous,sizeof sample)||begin-persistent_last_record>=1000000000ULL){
  if(frame(context_file,10,sequence,begin,now(),&sample,sizeof sample))persistent_records++;
  persistent_previous=sample;persistent_last_record=begin;
 }
}
static void lens_persistent_summary(void){
 fprintf(events,"{\"event\":\"lens_persistent_sampling\",\"samples\":%llu,\"records\":%llu,\"read_failures\":%llu,\"guard_changes\":%llu,\"source_bytes\":272,\"retained_banks\":2,\"bank_bytes\":288,\"atomic\":false}\n",(unsigned long long)persistent_samples,(unsigned long long)persistent_records,(unsigned long long)persistent_failures,(unsigned long long)persistent_changed);
}
