/* SPDX-License-Identifier: AGPL-3.0-only. Two EP stages in the front-owned CAP. */
static void j422_stage_init(struct bitb_result*r,const struct bitb_request*q,const struct native_context*c){
 memset(r,0,sizeof *r);r->magic=BITB_RESULT_MAGIC;r->version=BITB_WIRE_VERSION;r->bytes=sizeof *r;r->request_crc=q->crc;r->request_bytes=q->bytes;r->pa=q->pa;r->alias_va=c->alias_base;r->udd_base=c->udd;
 r->ready=r->safe_to_release=1;r->paused_contract=camera_stopped(q->camera_pid);r->code_guards=NXL_EP_NATIVE_CODE_GUARDS;r->rtos_guards=NXL_EP_RTOS_CODE_GUARDS;
}
static int j422_owner(struct native_context*c,struct bitb_result*r,const char*dir){
 char path[1200];uint32_t rt[136];unsigned char k[EP_KERNEL_STATE_BYTES+12];for(unsigned i=0;i<136;i++)rt[i]=c->rtos[0xaac/4+i];
 int ok=kernel_read(c,EP_KERNEL_STATE_VA,k,EP_KERNEL_STATE_BYTES)&&kernel_read(c,EP_KERNEL_MUTEX_VA,k+EP_KERNEL_STATE_BYTES,12);
 r->owner_records=2;r->owner_stable=!memcmp(rt,irq_before,sizeof rt);r->owner_crc=bitb_crc(rt,sizeof rt);r->kernel_restored=ok&&!memcmp(k,kernel_before,sizeof k);
 if(!r->owner_stable||!r->kernel_restored){r->safe_to_release=0;r->quarantine_reason=3;r->error=BJ_CONTEXT;}
 snprintf(path,sizeof path,"%s/rtos_after.bin",dir);if(!save_bytes(path,rt,sizeof rt))return 0;
 if(ok){snprintf(path,sizeof path,"%s/kernel_after.bin",dir);if(!save_bytes(path,k,sizeof k))return 0;}return 1;
}
#ifdef NXL_FAST_PIPELINE
#include "fast_ldc_worker.inc"
#else
static int j422_image_file(const char*root,const char*name,unsigned bytes){
 char path[1200],temp[1200];snprintf(path,sizeof path,"%s/%s",root,name);snprintf(temp,sizeof temp,"%s/.%s.tmp",root,name);
 FILE*f=fopen(temp,"wb");if(!f)return 0;int ok=fwrite(full_pad,1,bytes,f)==bytes&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok&&!rename(temp,path);
}
static int shared_stage_files(const char*dir,const struct bitb_result*r){
 char path[1200];snprintf(path,sizeof path,"%s/memory_before.bin",dir);if(!save_bytes(path,&full_before,sizeof full_before))return 0;
 snprintf(path,sizeof path,"%s/memory_after.bin",dir);if(!save_bytes(path,&full_after,sizeof full_after))return 0;
 snprintf(path,sizeof path,"%s/result.bin",dir);return save_bytes(path,r,sizeof *r);
}
static int p23_run_stages(struct native_context*c,const struct bitb_request*q,struct bitb_result*all,volatile unsigned char*memory,const char*root){
 memset(&proof,0,sizeof proof);proof.magic=J422_PROOF_MAGIC;proof.version=2;proof.bytes=sizeof proof;proof.request_crc=q->crc;proof.pa=q->pa;proof.alias_va=c->alias_base;proof.candidate_format=1;
 proof.protected_prefix_bytes=SW_CONTROL;for(unsigned i=0;i<3;i++)proof.expected_sampling[i]=i?0x11u:0x21u;
 memcpy(proof.input_sha256,full_input_sha256,sizeof proof.input_sha256);memcpy(proof.lut_table_sha256,full_table_sha256,sizeof proof.lut_table_sha256);memcpy(proof.prefix_sha256,full_workspace_original.prefix_sha256,sizeof proof.prefix_sha256);
 struct bitb_result*lr=&proof.lut_stage,*jr=&proof.stage;char dir[1200],path[1200];int rc=1;
 uint32_t ep=all->ep_fd,sma=all->sma_fd,flags=all->sma_open_flags;
 j422_stage_init(lr,q,c);proof.started=1;c->stage=1;c->lut_mode=1;c->origin=millis(c);c->last_wait_errno=c->note_overflow=0;
 memset(&lut_steps,0,sizeof lut_steps);lut_steps.magic=P12_STEP_MAGIC;lut_steps.version=1;lut_steps.record_bytes=sizeof(struct p12_step);
 snprintf(dir,sizeof dir,"%s/00_lut",root);fprintf(c->log,"SHARED160_STAGE_BEGIN stage=LUT format=NV16 source=%u destination=%u table=%s\n",TEST_IN,TEST_OUT,full_table_sha256);
 struct p11_ops lo={c,rd_lut,wr_lut,call_lut,ctl,state,idle,millis,delay,barrier,checkpoint_lut};
 rc=p11_run(q,lr,memory,&lut_saved,&lo);if(c->last_wait_errno)lr->wait_errno=c->last_wait_errno;
 lut_steps.crc=bitb_crc(&lut_steps,offsetof(struct p12_capture,crc));lr->route_samples=lut_steps.count;lr->route_crc=lut_steps.crc;lr->route_verified=lut_steps.count&&!c->note_overflow;lr->route_flags=c->note_overflow;
 snprintf(path,sizeof path,"%s/steps.bin",dir);if(!save_bytes(path,&lut_steps,sizeof lut_steps))rc=1;
 snprintf(path,sizeof path,"%s/dma_parameters.bin",dir);if(!save_bytes(path,lut_saved.rdma,sizeof lut_saved.rdma+sizeof lut_saved.wdma+sizeof lut_saved.start))rc=1;
 snprintf(path,sizeof path,"%s/lut_registers.bin",dir);if(!save_bytes(path,lut_saved.lut_before,40))rc=1;
 if(!j422_owner(c,lr,dir))rc=1;if(rc&&!lr->error)lr->error=BJ_SAVE;lr->phase=9;lr->crc=bitb_crc(lr,offsetof(struct bitb_result,crc));
 if(!shared_stage_files(dir,lr))rc=1;if(rc){proof.failed=1;goto done;}proof.completed=1;
 memcpy(proof.lut_output_sha256,full_lut_sha256,sizeof proof.lut_output_sha256);
 /* The complete first stage must have relinquished DMA/IRQs before its output
  * is moved and the same destination region becomes JPEG scratch. */
 if(!lr->safe_to_release||!lr->restore_verified||!lr->kernel_restored||!lr->owner_stable){rc=1;proof.failed=1;goto done;}
 proof.started=2;c->stage=2;c->lut_mode=0;c->origin=millis(c);c->last_wait_errno=c->note_overflow=0;
 j422_stage_init(jr,q,c);struct jpeg_meta*j=&proof.jpeg;j->magic=JPEG_META_MAGIC;j->version=1;j->bytes=sizeof *j;j->width=TEST_W;j->height=TEST_H;j->format=422;
 memset(&steps,0,sizeof steps);steps.magic=0x31534a4e;steps.version=1;snprintf(dir,sizeof dir,"%s/01_jpeg",root);
 struct jpeg_ops jo={c,rd,wr,call,ctl,state,idle,barrier,checkpoint,kernel_word};
 rc=jpeg_run_input(q,jr,memory,&saved,j,&jo,TEST_OUT,1);if(full_initialized==2)proof.direct_handoff=1;
 steps.crc=bitb_crc(&steps,offsetof(struct jpeg_capture,crc));jr->route_samples=steps.count;jr->route_crc=steps.crc;jr->route_verified=steps.count&&!c->note_overflow;jr->route_flags=c->note_overflow;
 snprintf(path,sizeof path,"%s/steps.bin",dir);if(!save_bytes(path,&steps,sizeof steps))rc=1;
 snprintf(path,sizeof path,"%s/native_packet.bin",dir);if(!save_bytes(path,&saved.task,sizeof saved.task))rc=1;
 if(full_pad_bytes){snprintf(path,sizeof path,"%s/padded_stream.bin",dir);if(!save_bytes(path,full_pad,full_pad_bytes))rc=1;}
 struct jpeg_info observed;memset(&observed,0,sizeof observed);
 if(full_pad&&j->stream_bytes>=32&&j->stream_bytes<=JPEG_CAP-JPEG_PAD&&jpeg_parse_info(full_pad,j->stream_bytes,&observed)){
  for(unsigned i=0;i<3;i++)proof.observed_sampling[i]=observed.sampling[i];j->structural=1;j->format=observed.format;
  proof.stream_saved=j422_image_file(root,"native-actual.jpg",j->stream_bytes);j->saved=proof.stream_saved;
  if(!proof.stream_saved||observed.width!=TEST_W||observed.height!=TEST_H||observed.format!=422||
     !j->source_ok||!j->guards_ok||jr->error||!j422_image_file(root,"native-422.jpg",j->stream_bytes))rc=1;
 }
 if(c->last_wait_errno)jr->wait_errno=c->last_wait_errno;if(!j422_owner(c,jr,dir))rc=1;
 if(rc&&!jr->error)jr->error=BJ_SAVE;jr->phase=9;jr->crc=bitb_crc(jr,offsetof(struct bitb_result,crc));j->crc=bitb_crc(j,offsetof(struct jpeg_meta,crc));
 snprintf(path,sizeof path,"%s/jpeg_meta.bin",dir);if(!save_bytes(path,j,sizeof *j))rc=1;if(!shared_stage_files(dir,jr))rc=1;
 proof.source_crc=jr->source_crc;if(rc)proof.failed=2;else proof.completed=2;
done:
 *all=proof.started==2?*jr:*lr;all->ep_fd=ep;all->sma_fd=sma;all->sma_open_flags=flags;
 if(proof.started==2){all->engine_jobs+=lr->engine_jobs;all->lut_hardware_jobs=lr->lut_hardware_jobs;all->table_operations=lr->table_operations;all->table_completions=lr->table_completions;all->table_loaded=lr->table_loaded;
  all->local_reset_calls+=lr->local_reset_calls;all->translations+=lr->translations;all->matches+=lr->matches;
  all->source_ok&=lr->source_ok;all->target_ok&=lr->target_ok;all->guards_ok&=lr->guards_ok;all->owner_stable&=lr->owner_stable;all->restore_verified&=lr->restore_verified;}
 all->failed_stage=proof.failed;if(rc&&!all->error)all->error=BJ_SAVE;
 fprintf(c->log,"SHARED160_PIPELINE_END completed=%u failed=%u jobs=%u lut_jobs=%u allocation_requests=0 handoff_bytes=%u safe=%u\n",proof.completed,proof.failed,all->engine_jobs,all->lut_hardware_jobs,proof.handoff_bytes,all->safe_to_release);
 return rc;
}

#endif
