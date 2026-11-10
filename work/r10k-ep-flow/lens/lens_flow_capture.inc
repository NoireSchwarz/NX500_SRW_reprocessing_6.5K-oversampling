/* SPDX-License-Identifier: AGPL-3.0-only. No MakerNote decoding or fitting. */
#include "lens-flow-core.h"
static uint64_t flow_samples,flow_records,flow_failures,flow_unstable,flow_last,flow_max_gap,flow_record_at;
static uint32_t flow_previous[2+sizeof(struct lf_sample)/4];
static unsigned flow_hardware_active;
static int lens_flow_reader(void*ctx,uint32_t at,void*out,unsigned bytes){(void)ctx;return mapped_read(at,out,bytes,0);}
static void lens_flow_sample(unsigned sequence){
 uint64_t begin=now();if(flow_last&&begin-flow_last>flow_max_gap)flow_max_gap=begin-flow_last;flow_last=begin;flow_samples++;
 struct {int32_t result;uint32_t bytes;struct lf_sample data;} sample;
 sample.bytes=sizeof sample.data;sample.result=lf_capture(lens_flow_reader,NULL,&sample.data);
 flow_hardware_active=sample.result==1&&sample.data.status==3&&sample.data.ldc_count>0;
 /* Inactive observations contain no followed candidates. Omit their trailing
  * zero padding; the reader zero-extends the recorded prefix to lf_bytes. */
 if(sample.data.status!=3){const unsigned char*p=(const unsigned char*)&sample.data;while(sample.bytes>12&&p[sample.bytes-1]==0)sample.bytes--;sample.bytes=(sample.bytes+3u)&~3u;}
 if(sample.result<0)flow_failures++;if(!sample.result)flow_unstable++;
 if(!flow_record_at||memcmp(flow_previous,&sample,sizeof sample)||begin-flow_record_at>=1000000000ULL){
  if(frame(context_file,9,sequence,begin,now(),&sample,8u+sample.bytes))flow_records++;
  memcpy(flow_previous,&sample,sizeof sample);flow_record_at=begin;
 }
}
static void lens_flow_summary(void){
 fprintf(events,"{\"event\":\"lens_flow_sampling\",\"mono_ns\":%llu,\"samples\":%llu,\"records\":%llu,\"read_failures\":%llu,\"bracket_changes\":%llu,\"max_gap_us\":%llu,\"guard_faults\":%d,\"target_us\":2000,\"atomic\":false}\n",(unsigned long long)now(),(unsigned long long)flow_samples,(unsigned long long)flow_records,(unsigned long long)flow_failures,(unsigned long long)flow_unstable,(unsigned long long)(flow_max_gap/1000),(int)lens_ro_faults);
}
