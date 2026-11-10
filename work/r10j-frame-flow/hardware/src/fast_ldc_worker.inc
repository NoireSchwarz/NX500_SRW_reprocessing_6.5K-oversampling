/* Native LDC -> LUT -> JPEG. One CAP, no intermediate full-frame files. */
#include "../../common/progress_emit.h"
static void fast_lh_array(FILE*f,const char*name,const uint32_t*v,unsigned n){fprintf(f,"\"%s\":[",name);for(unsigned i=0;i<n;i++)fprintf(f,"%s%u",i?",":"",v[i]);fprintf(f,"]");}
static int fast_ldc_hw_save(const char*root,const struct bitb_request*q,unsigned mapped_bytes){
 char path[1200];snprintf(path,sizeof path,"%s/ldc-hardware-audit.json",root);FILE*f=fopen(path,"wx");if(!f)return 0;
 const struct ldc_config_audit*a=&ldc_audit;
 fprintf(f,"{\"schema\":1,\"version\":\"R9i\",\"atomic\":false,\"request_crc\":%u,\"pa\":%u,\"nonce\":%u,\"source_sha256\":\"%s\",\"ldc_mapped_bytes\":%u,\"checkpoint_mask\":%u,\"checkpoints\":%u,\"flags_define\":{\"TOP_OK\":1,\"CONFIG_OK\":2,\"DEBUG_OK\":4,\"CLOCK_STABLE\":8,\"GATED\":16,\"READ_FAILED\":32,\"CLOCK_CHANGED\":64,\"DEBUG_OUTSIDE_MAPPING\":128},",q->crc,q->pa,full_workspace_original.nonce,full_workspace_original.source_sha256,mapped_bytes,a->hw_checkpoints,(unsigned)__builtin_popcount(a->hw_checkpoints));
 fast_lh_array(f,"top_offsets",lh_top_offsets,LH_TOP_COUNT);fprintf(f,",");fast_lh_array(f,"config_offsets",lh_config_offsets,LH_CONFIG_COUNT);fprintf(f,",");fast_lh_array(f,"debug_offsets",lh_debug_offsets,LH_DEBUG_COUNT);fprintf(f,",\"snapshots\":[");
 for(unsigned k=0;k<4;k++){const struct lh_snapshot*s=a->hw+k;fprintf(f,"%s{\"phase\":%u,\"flags\":%u,",k?",":"",s->phase,s->flags);fast_lh_array(f,"clock_before",s->clock_before,2);fprintf(f,",");fast_lh_array(f,"clock_after",s->clock_after,2);fprintf(f,",");fast_lh_array(f,"top",s->top,LH_TOP_COUNT);fprintf(f,",");fast_lh_array(f,"config",s->config,LH_CONFIG_COUNT);fprintf(f,",");fast_lh_array(f,"debug",s->debug,LH_DEBUG_COUNT);fprintf(f,"}");}
 fprintf(f,"],\"output_witness\":{\"is_completion_proof\":false,\"unchanged_does_not_prove_no_DMA\":true,\"bytes\":384,\"availability_bits\":%u,\"changed_bytes\":%u,\"before_crc\":%u,\"after_crc\":%u,\"before_hex\":\"",a->witness_available,a->witness_changed_bytes,a->witness_crc[0],a->witness_crc[1]);
 for(unsigned i=0;i<384;i++)fprintf(f,"%02x",a->witness[0][i]);fprintf(f,"\",\"after_hex\":\"");for(unsigned i=0;i<384;i++)fprintf(f,"%02x",a->witness[1][i]);fprintf(f,"\"}}\n");
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static int fast_ldc_audit_save(const char*root,const struct bitb_request*q){
 char path[1200];snprintf(path,sizeof path,"%s/ldc-config-audit.json",root);FILE*f=fopen(path,"wx");if(!f)return 0;
 const struct ldc_config_audit*a=&ldc_audit;
 fprintf(f,"{\"schema\":5,\"trial_scope\":\"NATIVE_M2M_SINGLE_FULL_FRAME\",\"full_frame_completed\":%s,\"start_function\":\"d5_ep_top_start_standalone\",\"completion_source\":\"CORE5_WDMA0_EOF\",\"processing_block\":[480,4320],\"overlap\":[0,0],\"output_window\":[0,0,6479,4319],\"ldc_context\":0,\"DMA_context\":0,\"ldc_table_base\":512,\"rdma0_ctrl2_offset\":4880,\"wdma0_ctrl2_offset\":5904,\"version\":\"R9i\",\"atomic\":false,\"request_crc\":%u,\"pa\":%u,\"nonce\":%u,\"source_sha256\":\"%s\",\"configured\":%u,\"committed\":%u,\"mismatches\":%u,\"first_offset\":%u,\"expected_value\":%u,\"actual_value\":%u,\"mask\":%u,\"dma_submitted\":%u,\"worker_error\":%u,\"top_offsets\":[4128,4132,4096,4168],",proof.ldc_stage.fresh_completion&&a->stop_verified&&proof.ldc_stage.restore_verified&&!proof.ldc_stage.error?"true":"false",q->crc,q->pa,full_workspace_original.nonce,full_workspace_original.source_sha256,a->configured,a->committed,a->mismatches,a->first_offset,a->expected_value,a->actual_value,a->mask,proof.ldc_stage.submission_attempted,proof.ldc_stage.error);
 const char*names[]={"top_before","top_after","expected","before_commit","after_commit"};
 fprintf(f,"\"start_trigger_readback_required\":false,\"start_update_mode_mask\":3146499,\"start_update_mode_expected\":2097666,\"start_TOP1008_expected_pulses\":[1048577,65536],\"start_success_authority\":\"NATIVE_RETURN_AND_MANUAL_MODE_THEN_FRESH_CORE5\",\"dma_rearm_before_submit\":true,\"dma_rearm_calls\":%u,\"dma_rearm_channels\":[%u,%u],\"dma_rearm_status\":[%u,%u],",a->dma_rearm_calls,a->dma_rearm_channel[0],a->dma_rearm_channel[1],a->dma_rearm_status[0],a->dma_rearm_status[1]);
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
static int fast_jpeg_write(const char*path,unsigned bytes,uint32_t*crc){
 FILE*f=fopen(path,"wx");if(!f)return 0;uint32_t c=~0u;unsigned done=0;int ok=1;
 while(done<bytes){unsigned n=bytes-done;if(n>65536)n=65536;if(fwrite(full_pad+done,1,n,f)!=n){ok=0;break;}c=bitb_crc_step(c,full_pad+done,n);done+=n;}
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;*crc=~c;return ok;
}
static int fast_jpeg_read(void*v,uint64_t at,void*p,unsigned n){(void)v;memcpy(p,full_pad+(unsigned)at,n);return 1;}
static int fast_quant_verified(unsigned bytes){struct nj_config actual;struct nj_reader r={NULL,bytes,fast_jpeg_read,0};return nj_jpeg(&r,0,bytes,&actual)==1&&!memcmp(actual.quant,full_jpeg_config.quant,128);}
static int fast_sample_save(const char*root,const char*name,const struct bitb_request*q,volatile unsigned char*m,unsigned off,unsigned boundary,unsigned stride,unsigned uv){
 struct ps_sample s;ps_init(&s,boundary,full_workspace_original.nonce,full_workspace_original.source_sha256);s.source_y=q->pa+off;s.source_uv=s.source_y+uv;
 uint32_t stamp[2]={0};syscall(263,1,stamp);s.begin_sec=stamp[0];s.begin_nsec=stamp[1];
 if(!ps_copy(&s,m+off,SW_BYTES-off,stride,uv))return 0;__sync_synchronize();syscall(263,1,stamp);s.end_sec=stamp[0];s.end_nsec=stamp[1];ps_seal(&s);
 char path[1200];snprintf(path,sizeof path,"%s/%s",root,name);return save_bytes(path,&s,sizeof s);
}
static void fast_merge(struct bitb_result*all,const struct bitb_result*r){
 all->engine_jobs+=r->engine_jobs;all->ldc_hardware_jobs+=r->ldc_hardware_jobs;all->lut_hardware_jobs+=r->lut_hardware_jobs;
 all->table_operations+=r->table_operations;all->table_completions+=r->table_completions;all->table_loaded|=r->table_loaded;
 all->local_reset_calls+=r->local_reset_calls;all->translations+=r->translations;all->matches+=r->matches;
 all->source_ok&=r->source_ok;all->target_ok&=r->target_ok;all->guards_ok&=r->guards_ok;all->owner_stable&=r->owner_stable;all->restore_verified&=r->restore_verified;
 all->safe_to_release&=r->safe_to_release;all->cold_restart_required|=r->cold_restart_required;
}
static int p23_run_stages(struct native_context*c,const struct bitb_request*q,struct bitb_result*all,volatile unsigned char*memory,const char*root){
 memset(&proof,0,sizeof proof);proof.magic=J422_PROOF_MAGIC;proof.version=4;proof.bytes=sizeof proof;proof.request_crc=q->crc;proof.pa=q->pa;proof.alias_va=c->alias_base;proof.candidate_format=1;proof.protected_prefix_bytes=SW_CONTROL;
 memcpy(proof.lut_table_sha256,full_table_sha256,sizeof proof.lut_table_sha256);memcpy(proof.prefix_sha256,full_workspace_original.prefix_sha256,sizeof proof.prefix_sha256);
 struct bitb_result*dr=&proof.ldc_stage,*lr=&proof.lut_stage,*jr=&proof.stage;char dir[1200],path[1200];uint32_t ep=all->ep_fd,sma=all->sma_fd,flags=all->sma_open_flags,elapsed[3]={0},begin,save_ms=0,quant_ms=0;int rc=1;
 nxl_live_progress("PIPE_LDC","RUNNING","Applying native LDC with explicit single-memory start");
 j422_stage_init(dr,q,c);proof.started=1;c->stage=1;c->lut_mode=1;c->origin=begin=millis(c);c->last_wait_errno=c->note_overflow=0;
 struct ldc_ops ld={c,rd_ldc,wr_ldc,call_ldc,ctl,state,idle,barrier,c->ldc_bytes,ldc_progress_capture,ldc_progress_wait,ldc_progress_coverage,millis,delay};rc=ldc_run(q,dr,memory,&ldc_saved,&ldc_audit,&full_ldc_config,&ld);elapsed[0]=millis(c)-begin;
 if(c->last_wait_errno)dr->wait_errno=c->last_wait_errno;
 snprintf(dir,sizeof dir,"%s/00_ldc",root);if(!j422_owner(c,dr,dir))rc=1;
 snprintf(path,sizeof path,"%s/native-ldc-state.bin",root);if(!save_bytes(path,&ldc_saved,sizeof ldc_saved))rc=1;
 if(!fast_ldc_audit_save(root,q))rc=1;
 if(!fast_ldc_hw_save(root,q,c->ldc_bytes))rc=1;
 if(!ldc_progress_file(root,q,dr))rc=1;
 /* The completed Bayer input stays read-only through LDC. Keep its bounded
  * sample even if LDC times out; this does not authorize resource return. */
 if(!fast_sample_save(root,"bayer-out-samples.bin",q,memory,SW_INPUT,PS_BAYER_OUT,SW_FRONT_STRIDE,SW_FRONT_UV)&&!rc)rc=1;
 if(!rc&&!fast_sample_save(root,"ldc-out-samples.bin",q,memory,SW_LDC_OUT,PS_LDC_OUT,SW_FRONT_STRIDE,SW_FRONT_UV))rc=1;
 if(rc&&!dr->error)dr->error=BJ_SAVE;dr->phase=9;dr->crc=bitb_crc(dr,offsetof(struct bitb_result,crc));
 if(rc){proof.failed=1;goto done;}proof.completed=1;
 if(!dr->safe_to_release||!dr->restore_verified||!dr->kernel_restored||!dr->owner_stable){rc=1;proof.failed=1;goto done;}
 nxl_live_progress("PIPE_LUT","RUNNING","Applying the native ISP LUT");
 j422_stage_init(lr,q,c);proof.started=2;c->stage=2;c->lut_mode=1;c->origin=begin=millis(c);c->last_wait_errno=c->note_overflow=0;
 memset(&lut_steps,0,sizeof lut_steps);lut_steps.magic=P12_STEP_MAGIC;lut_steps.version=1;lut_steps.record_bytes=sizeof(struct p12_step);
 struct p11_ops lo={c,rd_lut,wr_lut,call_lut,ctl,state,idle,millis,delay,barrier,checkpoint_lut};rc=p11_run(q,lr,memory,&lut_saved,&lo);elapsed[1]=millis(c)-begin;
 if(c->last_wait_errno)lr->wait_errno=c->last_wait_errno;
 lr->route_samples=lut_steps.count;lr->route_verified=lut_steps.count&&!c->note_overflow;lr->route_flags=c->note_overflow;
 snprintf(dir,sizeof dir,"%s/00_lut",root);if(!j422_owner(c,lr,dir))rc=1;
 if(!rc&&!fast_sample_save(root,"lut-out-samples.bin",q,memory,SW_LUT_OUT,PS_LUT_OUT,TEST_W,TEST_Y))rc=1;
 if(rc&&!lr->error)lr->error=BJ_SAVE;lr->phase=9;lr->crc=bitb_crc(lr,offsetof(struct bitb_result,crc));
 if(rc){proof.failed=2;goto done;}proof.completed=2;
 if(!lr->safe_to_release||!lr->restore_verified||!lr->kernel_restored||!lr->owner_stable){rc=1;proof.failed=2;goto done;}
 nxl_live_progress("PIPE_JPEG","RUNNING","Encoding JPEG with the source SRW quantization tables");
 proof.started=3;c->stage=3;c->lut_mode=0;c->origin=begin=millis(c);c->last_wait_errno=c->note_overflow=0;
 j422_stage_init(jr,q,c);struct jpeg_meta*j=&proof.jpeg;j->magic=JPEG_META_MAGIC;j->version=1;j->bytes=sizeof *j;j->width=TEST_W;j->height=TEST_H;j->format=422;
 memset(&steps,0,sizeof steps);steps.magic=0x31534a4e;steps.version=1;
 struct jpeg_ops jo={c,rd,wr,call,ctl,state,idle,barrier,checkpoint,kernel_word};rc=jpeg_run_input(q,jr,memory,&saved,j,&jo,TEST_OUT,1);elapsed[2]=millis(c)-begin;
 jr->route_samples=steps.count;jr->route_verified=steps.count&&!c->note_overflow;jr->route_flags=c->note_overflow;proof.direct_handoff=full_initialized==3;
 if(c->last_wait_errno)jr->wait_errno=c->last_wait_errno;
 struct jpeg_info info=full_jpeg_info;
 uint32_t stamp=millis(c);int quant_ok=!rc&&full_pad&&full_jpeg_info_valid&&full_pad_bytes==j->stream_bytes&&info.width==TEST_W&&info.height==TEST_H&&info.format==422&&fast_quant_verified(j->stream_bytes);quant_ms=millis(c)-stamp;
 if(quant_ok){
  proof.jpeg_quant_verified=1;for(unsigned i=0;i<3;i++)proof.observed_sampling[i]=info.sampling[i];j->structural=1;j->format=info.format;
  stamp=millis(c);proof.stream_saved=fast_jpeg_write(full_output_target,j->stream_bytes,&jr->target_crc);save_ms=millis(c)-stamp;j->saved=proof.stream_saved;if(!proof.stream_saved)rc=1;
 }else rc=1;
 snprintf(dir,sizeof dir,"%s/01_jpeg",root);if(!j422_owner(c,jr,dir))rc=1;
 if(rc&&!jr->error)jr->error=BJ_SAVE;jr->phase=9;jr->crc=bitb_crc(jr,offsetof(struct bitb_result,crc));j->crc=bitb_crc(j,offsetof(struct jpeg_meta,crc));
 if(rc)proof.failed=3;else{proof.completed=3;nxl_live_progress("PIPE_JPEG_WRITTEN","RUNNING","JPEG written; source tables verified; checking release guards");}
done:
 *all=proof.started==3?*jr:proof.started==2?*lr:*dr;all->ep_fd=ep;all->sma_fd=sma;all->sma_open_flags=flags;
 if(proof.started>=2)fast_merge(all,dr);if(proof.started==3)fast_merge(all,lr);
 snprintf(path,sizeof path,"%s/pipeline-isp-settings.json",root);FILE*f=fopen(path,"w");if(!f||fprintf(f,"{\"schema\":1,\"version\":\"R9i\",\"source_sha256\":\"%s\",\"ldc_profile_index\":%u,\"ldc_profile_key\":\"%s\",\"ldc_table_sha256\":\"%s\",\"ldc_generation\":\"%s\",\"ldc_approximate\":%s,\"ldc_anchor_lo\":%u,\"ldc_anchor_hi\":%u,\"ldc_fraction_q8\":%u,\"ldc_jobs\":%u,\"jpeg_quant_verified\":%u,\"stage_completed\":%u,\"failed_stage\":%u,\"ldc_ms\":%u,\"lut_ms\":%u,\"jpeg_ms\":%u,\"jpeg_save_ms\":%u,\"jpeg_quant_ms\":%u,\"jpeg_cached_bytes\":%u,\"jpeg_full_structure_scans\":%u,\"sample_points\":25,\"sample_tile_side\":16,\"full_frame_files\":0}\n",full_workspace_original.source_sha256,full_ldc_config.profile_index,full_ldc_config.profile_key,full_ldc_config.table_sha256,lp_method(&full_ldc_config),lp_approximate(&full_ldc_config)?"true":"false",full_ldc_config.reserved[3],full_ldc_config.reserved[4],full_ldc_config.reserved[5],all->ldc_hardware_jobs,proof.jpeg_quant_verified,proof.completed,proof.failed,elapsed[0],elapsed[1],elapsed[2],save_ms,quant_ms,full_jpeg_cache_bytes,full_jpeg_info_valid?1u:0u)<0)rc=1;
 if(f&&fclose(f))rc=1;all->failed_stage=proof.failed;if(rc&&!all->error)all->error=BJ_SAVE;return rc;
}
