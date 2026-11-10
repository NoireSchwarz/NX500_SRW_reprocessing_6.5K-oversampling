#define NXL_FAST_PIPELINE
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include "../src/bitb_job.h"
#include "../src/ldc_engine.h"
static struct ldc_config_audit ldc_audit;
static void fast_lh_array(FILE*f,const char*name,const uint32_t*v,unsigned n){fprintf(f,"\"%s\":[",name);for(unsigned i=0;i<n;i++)fprintf(f,"%s%u",i?",":"",v[i]);fprintf(f,"]");}
static int fast_ldc_hw_save(const char*root,const struct bitb_request*q,unsigned mapped_bytes){
 char path[1200];snprintf(path,sizeof path,"%s/ldc-hardware-audit.json",root);FILE*f=fopen(path,"wx");if(!f)return 0;
 const struct ldc_config_audit*a=&ldc_audit;
 fprintf(f,"{\"schema\":1,\"version\":\"R8r\",\"atomic\":false,\"request_crc\":%u,\"pa\":%u,\"nonce\":%u,\"source_sha256\":\"%s\",\"ldc_mapped_bytes\":%u,\"checkpoint_mask\":%u,\"checkpoints\":%u,\"flags_define\":{\"TOP_OK\":1,\"CONFIG_OK\":2,\"DEBUG_OK\":4,\"CLOCK_STABLE\":8,\"GATED\":16,\"READ_FAILED\":32,\"CLOCK_CHANGED\":64,\"DEBUG_OUTSIDE_MAPPING\":128},",q->crc,q->pa,full_workspace_original.nonce,full_workspace_original.source_sha256,mapped_bytes,a->hw_checkpoints,(unsigned)__builtin_popcount(a->hw_checkpoints));
 fast_lh_array(f,"top_offsets",lh_top_offsets,LH_TOP_COUNT);fprintf(f,",");fast_lh_array(f,"config_offsets",lh_config_offsets,LH_CONFIG_COUNT);fprintf(f,",");fast_lh_array(f,"debug_offsets",lh_debug_offsets,LH_DEBUG_COUNT);fprintf(f,",\"snapshots\":[");
 for(unsigned k=0;k<4;k++){const struct lh_snapshot*s=a->hw+k;fprintf(f,"%s{\"phase\":%u,\"flags\":%u,",k?",":"",s->phase,s->flags);fast_lh_array(f,"clock_before",s->clock_before,2);fprintf(f,",");fast_lh_array(f,"clock_after",s->clock_after,2);fprintf(f,",");fast_lh_array(f,"top",s->top,LH_TOP_COUNT);fprintf(f,",");fast_lh_array(f,"config",s->config,LH_CONFIG_COUNT);fprintf(f,",");fast_lh_array(f,"debug",s->debug,LH_DEBUG_COUNT);fprintf(f,"}");}
 fprintf(f,"],\"output_witness\":{\"is_completion_proof\":false,\"unchanged_does_not_prove_no_DMA\":true,\"bytes\":384,\"availability_bits\":%u,\"changed_bytes\":%u,\"before_crc\":%u,\"after_crc\":%u,\"before_hex\":\"",a->witness_available,a->witness_changed_bytes,a->witness_crc[0],a->witness_crc[1]);
 for(unsigned i=0;i<384;i++)fprintf(f,"%02x",a->witness[0][i]);fprintf(f,"\",\"after_hex\":\"");for(unsigned i=0;i<384;i++)fprintf(f,"%02x",a->witness[1][i]);fprintf(f,"\"}}\n");
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}

int main(void){
 struct bitb_request q={0};q.crc=0xffffffffu;q.pa=0xa0000000u;
 memset(full_workspace_original.source_sha256,'c',64);full_workspace_original.source_sha256[64]=0;full_workspace_original.nonce=0xffffffffu;
 memset(&ldc_audit,0xff,sizeof ldc_audit);ldc_audit.hw_checkpoints=15;
 for(unsigned k=0;k<4;k++){ldc_audit.hw[k].phase=k+1;ldc_audit.hw[k].flags=15;}
 ldc_audit.witness_available=3;ldc_audit.witness_changed_bytes=1;ldc_audit.witness[1][383]=0;
 assert(fast_ldc_hw_save("/mnt/mmc",&q,4096));return 0;
}
