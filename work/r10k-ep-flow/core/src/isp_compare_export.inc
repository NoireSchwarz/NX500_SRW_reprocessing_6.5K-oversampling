#include "../../common/isp_compare.h"
static int isp_compare_export(const char *dir){
 char path[1200];snprintf(path,sizeof path,"%s/isp-config-stages.bin",dir);FILE *f=fopen(path,"wx");if(!f)return 0;
 struct ic_sample s;int ok=1;unsigned complete=0;
 for(unsigned i=0;i<IC_STAGES;i++){
  memcpy(&s,(const void*)(cap_alias+IC_CAP_OFFSET+i*sizeof s),sizeof s);__sync_synchronize();
  if(!ic_valid(&s,nonce,i+1)){ok=0;break;}if(s.stable&&s.mask==31u)complete|=1u<<i;
  if(fwrite(&s,1,sizeof s,f)!=sizeof s){ok=0;break;}
 }
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;if(!ok)return 0;
 snprintf(path,sizeof path,"%s/isp-config-schema.json",dir);f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"schema\":1,\"release\":\"R8j\",\"record_bytes\":%u,\"stage_count\":6,\"complete_mask\":%u,\"stages\":[\"BEFORE_CONFIG\",\"AFTER_CONFIG\",\"BEFORE_START\",\"AFTER_START\",\"AFTER_COMPLETE\",\"AFTER_RESTORE\"],\"header_words\":16,\"wb_word\":16,\"bank0_word\":34,\"bank1_word\":1506,\"bank_bytes\":5888,\"native_recorder_kind\":12,\"shadow_is_dma_proof\":false}\n",(unsigned)sizeof s,complete);
 ok=!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
