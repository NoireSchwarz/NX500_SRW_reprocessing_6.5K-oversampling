#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define NXL_ALLOCATOR_FRESH_READ_H
#define AF_TABLE_PA 0x81dfc908u
#define AR_TABLE_WORDS (45056u*4u)
static uint32_t field_table[AR_TABLE_WORDS],current_table[AR_TABLE_WORDS];
static uint32_t reported=32071,root_bad,change_count;
static int af_read_words(int fd,uint32_t at,uint32_t*out,unsigned n){
 (void)fd;memset(out,0,n*4u);
 if(at==0x817dc498u&&n==10){out[0]=root_bad?0:0x80574230;out[1]=0x805742ac;out[9]=0x81dfc800;}
 else if(at==0x81dfc800u&&n==66){out[0]=0x80574b60;out[65]=0x80574c48;out[0x78/4]=0xa1c00000;}
 else if(at==0x81eac908u&&n==3){out[0]=0x94000000;out[1]=45056;out[2]=reported;if(change_count)reported++;}
 else if(at==0x81eacda0u&&n==1)*out=0xbb6cc000;
 else if(at>=AF_TABLE_PA&&at+(uint64_t)n*4<=AF_TABLE_PA+sizeof current_table)memcpy(out,(unsigned char*)current_table+at-AF_TABLE_PA,n*4u);
 else assert(0);return 1;
}
#define main unused_video_fullwidth_main
#include "../src/nxl-video-fullwidth.c"
#undef main
static void reset_fixture(void){free(baseline);free(baseline_again);baseline=baseline_again=NULL;memcpy(current_table,field_table,sizeof current_table);root_bad=change_count=0;}
int main(void){
 FILE*f=fopen("/field-allocator.bin","rb");assert(f&&fread(field_table,1,sizeof field_table,f)==sizeof field_table&&!fclose(f));
 log_file=fopen("/audit.log","wx");assert(log_file);devfd=1;started=ns();
 const uint32_t counters[]={32071,68602,69726,85706,122741,168076,213411,267046};
 for(unsigned i=0;i<sizeof counters/sizeof*counters;i++){
  reset_fixture();reported=counters[i];char path[32];snprintf(path,sizeof path,"/case%u",i);assert(!mkdir(path,0700));
  assert(ownership_baseline(path));assert(video_table_free==32071&&video_bank_largest>=VFW_CAP_BLOCKS&&free_before==32071&&video_allocator_head.reported_free==reported);
  assert(!native_calls&&!request_done&&!ownership_verified);
 }
 reset_fixture();root_bad=1;assert(!ownership_baseline("/bad-root"));assert(!strcmp(video_allocator_reason,"VIDEO_ALLOCATOR_IDENTITY_OR_READ_FAILED"));
 reset_fixture();change_count=1;assert(!ownership_baseline("/unstable"));assert(!strcmp(video_allocator_reason,"VIDEO_ALLOCATOR_SNAPSHOT_UNSTABLE"));
 reset_fixture();current_table[14080*4]=0x23;current_table[14080*4+1]=45056;assert(!mkdir("/bad-table",0700));assert(!ownership_baseline("/bad-table"));assert(!strcmp(video_allocator_reason,"VIDEO_ALLOCATOR_PARTITION_INVALID"));
 reset_fixture();unsigned free_seen;assert(vfa_partition(current_table,45056,&free_seen)&&free_seen==32071);
 current_table[14080*4+1]=1;assert(!vfa_partition(current_table,45056,&free_seen));
 reset_fixture();current_table[14080*4]=0x23;current_table[14080*4+1]=128;assert(vfa_partition(current_table,45056,&free_seen)&&free_seen==32071-128);assert(!vfw_returned(current_table,45056,free_seen,14080));
 current_table[14080*4]=current_table[14080*4+1]=0;assert(vfa_partition(current_table,45056,&free_seen)&&vfw_returned(current_table,45056,free_seen,14080));
 assert(!native_calls&&!request_done&&!ownership_verified);fclose(log_file);reset_fixture();free(baseline);free(baseline_again);
 puts("PASS actual field partition with eight observed OEM counters; identity/unstable/invalid partition refusals; owned-block overlap and return; zero counter writes/native calls.");return 0;
}
