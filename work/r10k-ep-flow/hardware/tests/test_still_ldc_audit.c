#define NXL_FAST_PIPELINE
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include "../src/bitb_job.h"
#include "../src/ldc_engine.h"
#include "../src/pipeline_proof.h"
static struct ldc_config_audit ldc_audit;
static struct ldc_snapshot ldc_saved;
static struct j422_proof proof;
static void fast_lh_array(FILE*f,const char*name,const uint32_t*v,unsigned n){fprintf(f,"\"%s\":[",name);for(unsigned i=0;i<n;i++)fprintf(f,"%s%u",i?",":"",v[i]);fprintf(f,"]");}
static int fast_ldc_hw_save(const char*root,const struct bitb_request*q,unsigned mapped_bytes){
 char path[1200];snprintf(path,sizeof path,"%s/ldc-hardware-audit.json",root);FILE*f=fopen(path,"wx");if(!f)return 0;
 const struct ldc_config_audit*a=&ldc_audit;
 fprintf(f,"{\"schema\":1,\"version\":\"R9a\",\"atomic\":false,\"request_crc\":%u,\"pa\":%u,\"nonce\":%u,\"source_sha256\":\"%s\",\"ldc_mapped_bytes\":%u,\"checkpoint_mask\":%u,\"checkpoints\":%u,\"flags_define\":{\"TOP_OK\":1,\"CONFIG_OK\":2,\"DEBUG_OK\":4,\"CLOCK_STABLE\":8,\"GATED\":16,\"READ_FAILED\":32,\"CLOCK_CHANGED\":64,\"DEBUG_OUTSIDE_MAPPING\":128},",q->crc,q->pa,full_workspace_original.nonce,full_workspace_original.source_sha256,mapped_bytes,a->hw_checkpoints,(unsigned)__builtin_popcount(a->hw_checkpoints));
 fast_lh_array(f,"top_offsets",lh_top_offsets,LH_TOP_COUNT);fprintf(f,",");fast_lh_array(f,"config_offsets",lh_config_offsets,LH_CONFIG_COUNT);fprintf(f,",");fast_lh_array(f,"debug_offsets",lh_debug_offsets,LH_DEBUG_COUNT);fprintf(f,",\"snapshots\":[");
 for(unsigned k=0;k<4;k++){const struct lh_snapshot*s=a->hw+k;fprintf(f,"%s{\"phase\":%u,\"flags\":%u,",k?",":"",s->phase,s->flags);fast_lh_array(f,"clock_before",s->clock_before,2);fprintf(f,",");fast_lh_array(f,"clock_after",s->clock_after,2);fprintf(f,",");fast_lh_array(f,"top",s->top,LH_TOP_COUNT);fprintf(f,",");fast_lh_array(f,"config",s->config,LH_CONFIG_COUNT);fprintf(f,",");fast_lh_array(f,"debug",s->debug,LH_DEBUG_COUNT);fprintf(f,"}");}
 fprintf(f,"],\"output_witness\":{\"is_completion_proof\":false,\"unchanged_does_not_prove_no_DMA\":true,\"bytes\":384,\"availability_bits\":%u,\"changed_bytes\":%u,\"before_crc\":%u,\"after_crc\":%u,\"before_hex\":\"",a->witness_available,a->witness_changed_bytes,a->witness_crc[0],a->witness_crc[1]);
 for(unsigned i=0;i<384;i++)fprintf(f,"%02x",a->witness[0][i]);fprintf(f,"\",\"after_hex\":\"");for(unsigned i=0;i<384;i++)fprintf(f,"%02x",a->witness[1][i]);fprintf(f,"\"}}\n");
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static int fast_ldc_audit_save(const char*root,const struct bitb_request*q){
 char path[1200];snprintf(path,sizeof path,"%s/ldc-config-audit.json",root);FILE*f=fopen(path,"wx");if(!f)return 0;
 const struct ldc_config_audit*a=&ldc_audit;
 fprintf(f,"{\"schema\":5,\"trial_scope\":\"NATIVE_M2M_SINGLE_FULL_FRAME\",\"full_frame_completed\":%s,\"start_function\":\"d5_ep_top_start_standalone\",\"completion_source\":\"CORE5_WDMA0_EOF\",\"processing_block\":[480,4320],\"overlap\":[0,0],\"output_window\":[0,0,6479,4319],\"ldc_context\":0,\"DMA_context\":0,\"ldc_table_base\":512,\"rdma0_ctrl2_offset\":4880,\"wdma0_ctrl2_offset\":5904,\"version\":\"R9a\",\"atomic\":false,\"request_crc\":%u,\"pa\":%u,\"nonce\":%u,\"source_sha256\":\"%s\",\"configured\":%u,\"committed\":%u,\"mismatches\":%u,\"first_offset\":%u,\"expected_value\":%u,\"actual_value\":%u,\"mask\":%u,\"dma_submitted\":%u,\"worker_error\":%u,\"top_offsets\":[4128,4132,4096,4168],",proof.ldc_stage.fresh_completion&&a->stop_verified&&proof.ldc_stage.restore_verified&&!proof.ldc_stage.error?"true":"false",q->crc,q->pa,full_workspace_original.nonce,full_workspace_original.source_sha256,a->configured,a->committed,a->mismatches,a->first_offset,a->expected_value,a->actual_value,a->mask,proof.ldc_stage.submission_attempted,proof.ldc_stage.error);
 const char*names[]={"top_before","top_after","expected","before_commit","after_commit"};
 fprintf(f,"\"start_trigger_readback_required\":false,\"start_update_mode_mask\":3146499,\"start_update_mode_expected\":2097666,\"start_TOP1008_expected_pulses\":[1048577,65536],\"start_success_authority\":\"NATIVE_RETURN_AND_MANUAL_MODE_THEN_FRESH_CORE5\",");
 fprintf(f,"\"rdma0_ctrl2_expected\":%u,\"rdma0_ctrl2_mask\":%u,",LDC_RDMA_CTRL2_EXPECTED,LDC_RDMA_CTRL2_MASK);
 const uint32_t*values[]={a->top_before,a->top_after,a->expected,a->before,a->after};
 for(unsigned k=0;k<5;k++){fprintf(f,"\"%s\":[",names[k]);unsigned count=k<2?4:66;for(unsigned i=0;i<count;i++)fprintf(f,"%s%u",i?",":"",values[k][i]);fprintf(f,"],");}
 fprintf(f,"\"guard_offsets\":[");for(unsigned i=0;i<66;i++)fprintf(f,"%s%u",i?",":"",ldc_guard_offset(i));
 uint32_t start_words[9];memcpy(start_words,&ldc_saved.start,sizeof start_words);
 fprintf(f,"],\"start_fields\":[\"module\",\"rdma0\",\"rdma1\",\"rdma2\",\"wdma0\",\"wdma1\",\"wdma2\",\"wdma3\",\"module_option\"],\"start_parameters\":[");
 for(unsigned i=0;i<9;i++)fprintf(f,"%s%u",i?",":"",start_words[i]);fprintf(f,"],\"dma_checkpoints\":%u,\"dma_offsets\":[",a->dma_checkpoints);
 for(unsigned i=0;i<LDC_DMA_AUDIT_WORDS;i++)fprintf(f,"%s%u",i?",":"",ldc_dma_audit_offsets[i]);fprintf(f,"],");
 const char*dn[]={"before_start","after_start","after_wait","after_stop"};const uint32_t*dv[]={a->dma_before,a->dma_started,a->dma_after_wait,a->dma_stopped};
 for(unsigned k=0;k<4;k++){fprintf(f,"\"%s\":[",dn[k]);for(unsigned i=0;i<LDC_DMA_AUDIT_WORDS;i++)fprintf(f,"%s%u",i?",":"",dv[k][i]);fprintf(f,"]%s",k==3?"":",");}fprintf(f,",\"auto_stopped\":%u,\"dma_stop_mask\":1025,\"dma_stop_request_before\":%u,\"dma_stop_ack_before\":%u,\"dma_stop_ack\":%u,\"dma_stop_ack_confirm\":%u,\"dma_stop_request_after\":%u,\"dma_stop_ms\":%u,\"dma_stop_verified\":%u}\n",a->auto_stopped,a->stop_request_before,a->stop_ack_before,a->stop_ack,a->stop_ack_confirm,a->stop_request_after,a->stop_ms,a->stop_verified);
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}

int main(void){
 struct bitb_request q={0};q.crc=0xffffffffu;q.pa=0xa0000000u;
 memset(full_workspace_original.source_sha256,'c',64);full_workspace_original.source_sha256[64]=0;full_workspace_original.nonce=0xffffffffu;
 memset(&ldc_audit,0xff,sizeof ldc_audit);ldc_audit.hw_checkpoints=15;
 for(unsigned k=0;k<4;k++){ldc_audit.hw[k].phase=k+1;ldc_audit.hw[k].flags=15;}
 ldc_audit.witness_available=3;ldc_audit.witness_changed_bytes=1;ldc_audit.witness[1][383]=0;
 ep_single_ldc_m2m_start(&ldc_saved.start);
 assert(fast_ldc_hw_save("/mnt/mmc",&q,4096)&&fast_ldc_audit_save("/mnt/mmc",&q));return 0;
}
