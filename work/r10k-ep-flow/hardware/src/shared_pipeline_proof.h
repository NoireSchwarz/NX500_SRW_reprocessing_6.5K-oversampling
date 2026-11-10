/* SPDX-License-Identifier: AGPL-3.0-only. Native NV16 LUT -> JPEG, shared CAP. */
#ifndef NXL_PIPELINE_PROOF_H
#define NXL_PIPELINE_PROOF_H
#include "jpeg_core.h"
#define J422_PROOF_MAGIC 0x3232344au
#define NXL_EP_NATIVE_CODE_GUARDS 26u
#define NXL_EP_RTOS_CODE_GUARDS 3u
struct j422_proof {
 uint32_t magic,version,bytes,request_crc,pa,alias_va,candidate_format,started,completed,failed;
 uint32_t expected_sampling[3],observed_sampling[3],source_crc,stream_saved;
 char input_sha256[68];
 struct bitb_result stage;
 struct jpeg_meta jpeg;
 struct bitb_result lut_stage;
 struct bitb_result ldc_stage;
 char lut_table_sha256[68],lut_output_sha256[68],prefix_sha256[68];
 uint32_t handoff_bytes,direct_handoff,protected_prefix_bytes,allocation_requests,jpeg_quant_verified,crc;
};
static int j422_stage_valid(const struct bitb_result*r,const struct bitb_request*q,uint32_t alias,unsigned lut){
 return r->magic==BITB_RESULT_MAGIC&&r->version==BITB_WIRE_VERSION&&r->bytes==sizeof *r&&r->request_crc==q->crc&&r->pa==q->pa&&r->alias_va==alias&&
 r->crc==bitb_crc(r,offsetof(struct bitb_result,crc))&&!r->error&&r->engine_jobs==1&&r->lut_hardware_jobs==(lut==1)&&r->ldc_hardware_jobs==(lut==2)&&r->table_operations==(lut==1)&&r->table_completions==(lut==1)&&
 r->ready&&r->paused_contract&&r->safe_to_release&&r->fresh_completion&&r->source_ok&&r->target_ok&&r->guards_ok&&r->owner_stable&&r->owner_records==2&&
 r->kernel_restored&&r->restore_verified&&r->registers_restored&&r->clocks_restored&&r->reset_after&&r->lock_acquired&&r->lock_released&&r->irq_disabled&&
 r->idle_verified&&r->initialized_verified&&r->initialized_bytes==BITB_BYTES&&r->route_verified&&!r->route_flags&&r->local_reset_calls==2&&r->code_guards==NXL_EP_NATIVE_CODE_GUARDS&&r->rtos_guards==NXL_EP_RTOS_CODE_GUARDS&&
 r->translations==TEST_TRANSLATIONS&&r->matches==TEST_TRANSLATIONS&&r->request_bytes==BITB_BYTES&&r->source_copies==0&&r->source_pa==q->pa+(lut==2?SW_INPUT:lut==1?TEST_IN:TEST_OUT)&&r->pixel_format==422;
}
static int j422_proof_valid(const struct j422_proof*p,const struct bitb_request*q){
 if(p->magic!=J422_PROOF_MAGIC||p->version!=2||p->bytes!=sizeof *p||p->request_crc!=q->crc||p->pa!=q->pa||p->candidate_format!=1||
 p->started!=2||p->completed!=2||p->failed||!p->stream_saved||p->allocation_requests||p->handoff_bytes||p->direct_handoff!=1||p->protected_prefix_bytes!=SW_CONTROL||
 p->crc!=bitb_crc(p,offsetof(struct j422_proof,crc))||!sw_hash_valid(p->input_sha256)||!sw_hash_valid(p->lut_table_sha256)||
 !sw_hash_valid(p->lut_output_sha256)||!sw_hash_valid(p->prefix_sha256))return 0;
 for(unsigned i=0;i<3;i++)if(p->expected_sampling[i]!=(i?0x11u:0x21u)||p->observed_sampling[i]!=p->expected_sampling[i])return 0;
 if(!j422_stage_valid(&p->stage,q,p->alias_va,0)||!j422_stage_valid(&p->lut_stage,q,p->alias_va,1)||
    !p->lut_stage.table_loaded||!p->lut_stage.output_changed||p->stage.source_crc!=p->lut_stage.target_crc||p->source_crc!=p->stage.source_crc)return 0;
 const struct jpeg_meta*j=&p->jpeg;
 return j->magic==JPEG_META_MAGIC&&j->version==1&&j->bytes==sizeof *j&&j->width==TEST_W&&j->height==TEST_H&&j->format==422&&!j->native_return&&
 j->structural&&j->source_ok&&j->guards_ok&&j->saved&&!j->error_before&&!j->error_after&&j->stream_bytes>=32&&j->stream_bytes<=JPEG_CAP-JPEG_PAD&&
 p->stage.pixel_bytes==j->stream_bytes&&j->stream_bits==j->stream_bytes*8&&j->crc==bitb_crc(j,offsetof(struct jpeg_meta,crc));
}
#endif
