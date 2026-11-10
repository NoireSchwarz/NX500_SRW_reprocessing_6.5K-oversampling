/* Fixed RO MMIO only. No driver fd, ioctl, native calls or image pointers. */
#include "../common/ldc_observation.h"
#include "../common/lens_hw_yield.h"
static struct {volatile unsigned char*page[2];unsigned attempted,yielded;uint64_t poll,record,control_poll;unsigned active;char acknowledged[33];struct lh_snapshot previous;} lh_lens;
static int lens_ldc_hardware_end(void){
 int ok=1;for(unsigned i=0;i<2;i++)if(lh_lens.page[i]){if(munmap((void*)lh_lens.page[i],4096))ok=0;else lh_lens.page[i]=NULL;}return ok;
}
static void lens_ldc_hardware_control(void){
 uint64_t stamp=now();if(lh_lens.control_poll&&stamp-lh_lens.control_poll<20000000u)return;lh_lens.control_poll=stamp;
 char token[33];if(!lhy_read(directory,"hardware-yield.request",token)||!strcmp(token,lh_lens.acknowledged))return;
 /* Terminal for this recorder lifetime. Never remap during a held job. */
 lh_lens.yielded=1;
 if(lens_ldc_hardware_end()&&lhy_publish(directory,"hardware-yield.ack",token))memcpy(lh_lens.acknowledged,token,33);
}
static int lh_lens_read(void*unused,unsigned bank,uint32_t off,void*out,unsigned bytes){
 (void)unused;if(lh_lens.yielded||dev<0||!lh_known_span(bank,off,bytes))return 0;
 if(!(lh_lens.attempted&(1u<<bank))){
  lh_lens.attempted|=1u<<bank;uint32_t pa=bank?0x20823000u:0x20821000u;
  void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,dev,(off_t)(uint64_t)pa);if(p!=MAP_FAILED)lh_lens.page[bank]=p;
 }
 if(!lh_lens.page[bank])return 0;unsigned in_page=bank?off:off-0x1000u;
 if(in_page+bytes>4096)return 0;return lens_ro_load(lh_lens.page[bank]+in_page,out,bytes);
}
static void lens_ldc_hardware_sample(unsigned sequence,unsigned active){
 if(lh_lens.yielded)return;
 uint64_t begin=now();uint64_t period=active?2000000u:20000000u;
 if(active==lh_lens.active&&lh_lens.poll&&begin-lh_lens.poll<period)return;lh_lens.poll=begin;
 struct lh_snapshot s;lh_capture(lh_lens_read,NULL,0x1000,active?10:0,&s);
 int changed=memcmp(s.config,lh_lens.previous.config,sizeof s.config)||s.flags!=lh_lens.previous.flags||memcmp(s.clock_before,lh_lens.previous.clock_before,8);
 int all_changed=memcmp(&s,&lh_lens.previous,sizeof s);
 if(!lh_lens.record||changed||(active&&all_changed)||active!=lh_lens.active||begin-lh_lens.record>=1000000000u){
  if(frame(context_file,15,sequence,begin,now(),&s,sizeof s)){lh_lens.previous=s;lh_lens.record=begin;}
 }
 lh_lens.active=active;
}
static void lens_ldc_hardware_schema(FILE*f){
 fprintf(f,"{\"format\":\"NX_LDC_HW_OBSERVATION1\",\"kind\":15,\"bytes\":%u,\"u32_header\":[\"schema\",\"bytes\",\"phase\",\"flags\",\"clock_before[2]\",\"clock_after[2]\"],\"top_count\":%u,\"config_count\":%u,\"debug_count\":%u,\"top_offsets\":[",(unsigned)sizeof(struct lh_snapshot),(unsigned)LH_TOP_COUNT,(unsigned)LH_CONFIG_COUNT,(unsigned)LH_DEBUG_COUNT);
 for(unsigned i=0;i<LH_TOP_COUNT;i++)fprintf(f,"%s%u",i?",":"",lh_top_offsets[i]);fprintf(f,"],\"config_offsets\":[");for(unsigned i=0;i<LH_CONFIG_COUNT;i++)fprintf(f,"%s%u",i?",":"",lh_config_offsets[i]);fprintf(f,"],\"debug_offsets\":[");for(unsigned i=0;i<LH_DEBUG_COUNT;i++)fprintf(f,"%s%u",i?",":"",lh_debug_offsets[i]);
 fprintf(f,"],\"flags\":{\"TOP_OK\":1,\"CONFIG_OK\":2,\"DEBUG_OK\":4,\"CLOCK_STABLE\":8,\"GATED\":16,\"READ_FAILED\":32,\"CLOCK_CHANGED\":64,\"DEBUG_OUTSIDE_MAPPING\":128},\"phases\":{\"0\":\"background RO observation\",\"10\":\"typed EP Still LDC request observed; timeline association only\"},\"target_poll_us_active\":2000,\"target_poll_us_background\":20000,\"background_heartbeat_ms\":1000,\"atomic\":false,\"device_writes\":0,\"native_calls\":0,\"image_pointer_following\":false,\"debug_address_evidence\":\"original _udd_ep_ldc_print_debug_info 0x33f14\"}\n");
}
