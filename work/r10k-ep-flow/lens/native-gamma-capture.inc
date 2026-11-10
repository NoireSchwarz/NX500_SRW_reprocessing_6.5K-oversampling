/* SPDX-License-Identifier: AGPL-3.0-only. Observer owns no camera resources.
 * Extra data is metadata only. Parameter DDR is already in kind12, no new
 * MMIO mapping, RTOS code, IRQ, allocation or hardware path is introduced. */
#include "../common/native_gamma.h"
static uint64_t gamma_last_poll,gamma_last_record,gamma_samples,gamma_records,gamma_races,gamma_max_read_ns;
static uint32_t gamma_previous_hash;
static struct gc_sample gamma_observation;
static int gamma_linux_read(void*unused,uint32_t at,void*out,unsigned bytes){(void)unused;return mapped_read(at,out,bytes,0);}
static void native_gamma_sample(unsigned sequence){
 uint64_t begin=now();if(gamma_last_poll&&begin-gamma_last_poll<20000000ULL)return;gamma_last_poll=begin;
 (void)gc_capture(gamma_linux_read,NULL,&gamma_observation);gamma_samples++;
 if(gamma_observation.status==4||gamma_observation.channel[0].status==4||gamma_observation.channel[1].status==4)gamma_races++;
 uint64_t end=now();if(end-begin>gamma_max_read_ns)gamma_max_read_ns=end-begin;
 uint32_t h=gamma_observation.checksum;
 if(!gamma_last_record||h!=gamma_previous_hash||begin-gamma_last_record>=1000000000ULL){
  if(frame(context_file,14,sequence,begin,end,&gamma_observation,sizeof gamma_observation)){gamma_last_record=begin;gamma_previous_hash=h;gamma_records++;}
 }
}
static void native_gamma_summary(void){
 fprintf(events,"{\"event\":\"native_gamma_observer\",\"samples\":%llu,\"records\":%llu,\"races\":%llu,\"max_read_ns\":%llu,\"native_calls\":0,\"device_writes\":0,\"atomic\":false,\"hardware_consumption_proven\":false}\n",(unsigned long long)gamma_samples,(unsigned long long)gamma_records,(unsigned long long)gamma_races,(unsigned long long)gamma_max_read_ns);
}

static void native_gamma_schema(FILE*f){
 fprintf(f,"{\"format\":\"NX_NATIVE_GAMMA1\",\"kind\":14,\"bytes\":%u,\"channel_bytes\":%u,\"channel_offset\":%u,\"manager\":%u,\"fixed_parameter_bases\":[\"0xbb71b300\",\"0xbb71d300\"],\"parameter_bases_bracketed\":true,\"interval_us\":20000,\"heartbeat_us\":1000000,\"ddr_kind\":12,\"ddr_bank_offset\":%u,\"ddr_bytes\":%u,\"tail_u32\":[\"effective_parameter_word\",\"effective_x19\",\"effective_y19\",\"custom_toggle\",\"custom_x19\",\"custom_y19\"],\"statuses\":{\"1\":\"brackets_and_repeated_data_equal\",\"2\":\"unconstructed_or_unknown_type\",\"3\":\"read_unavailable\",\"4\":\"changed_during_read\",\"5\":\"outside_ordinary_RAM\",\"6\":\"unsupported_parameter_shape_or_bank_mapping\"},\"native_calls\":0,\"device_writes\":0,\"atomic\":false,\"hardware_consumption_proven\":false}\n",(unsigned)sizeof(struct gc_sample),(unsigned)sizeof(struct gc_channel),(unsigned)offsetof(struct gc_sample,channel),GC_MANAGER,GC_DDR_OFFSET,GC_DDR_BYTES);
}
