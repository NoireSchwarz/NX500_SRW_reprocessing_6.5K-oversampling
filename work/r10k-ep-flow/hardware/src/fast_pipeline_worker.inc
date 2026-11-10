/* Native engines unchanged; lightweight guards replace frame/file audits. */
#include "../../common/progress_emit.h"
static int fast_jpeg_write(const char*path,unsigned bytes,uint32_t*crc){
 FILE*f=fopen(path,"wx");if(!f)return 0;uint32_t c=~0u;unsigned done=0;int ok=1;
 while(done<bytes){unsigned n=bytes-done;if(n>65536)n=65536;if(fwrite(full_pad+done,1,n,f)!=n){ok=0;break;}c=bitb_crc_step(c,full_pad+done,n);done+=n;}
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;*crc=~c;return ok;
}
static int p23_run_stages(struct native_context*c,const struct bitb_request*q,struct bitb_result*all,volatile unsigned char*memory,const char*root){
 memset(&proof,0,sizeof proof);proof.magic=J422_PROOF_MAGIC;proof.version=3;proof.bytes=sizeof proof;proof.request_crc=q->crc;proof.pa=q->pa;proof.alias_va=c->alias_base;proof.candidate_format=1;proof.protected_prefix_bytes=SW_CONTROL;
 memcpy(proof.lut_table_sha256,full_table_sha256,sizeof proof.lut_table_sha256);memcpy(proof.prefix_sha256,full_workspace_original.prefix_sha256,sizeof proof.prefix_sha256);
 struct bitb_result*lr=&proof.lut_stage,*jr=&proof.stage;char dir[1200];uint32_t ep=all->ep_fd,sma=all->sma_fd,flags=all->sma_open_flags;
 nxl_live_progress("PIPE_LUT","RUNNING","Applying the native ISP LUT");
 j422_stage_init(lr,q,c);proof.started=1;c->stage=1;c->lut_mode=1;c->origin=millis(c);c->last_wait_errno=c->note_overflow=0;
 memset(&lut_steps,0,sizeof lut_steps);lut_steps.magic=P12_STEP_MAGIC;lut_steps.version=1;lut_steps.record_bytes=sizeof(struct p12_step);
 struct p11_ops lo={c,rd_lut,wr_lut,call_lut,ctl,state,idle,millis,delay,barrier,checkpoint_lut};
 int rc=p11_run(q,lr,memory,&lut_saved,&lo);if(c->last_wait_errno)lr->wait_errno=c->last_wait_errno;
 lr->route_samples=lut_steps.count;lr->route_verified=lut_steps.count&&!c->note_overflow;lr->route_flags=c->note_overflow;
 snprintf(dir,sizeof dir,"%s/00_lut",root);if(!j422_owner(c,lr,dir))rc=1;lr->phase=9;lr->crc=bitb_crc(lr,offsetof(struct bitb_result,crc));
 if(rc){proof.failed=1;goto done;}proof.completed=1;
 if(!lr->safe_to_release||!lr->restore_verified||!lr->kernel_restored||!lr->owner_stable){rc=1;proof.failed=1;goto done;}
 nxl_live_progress("PIPE_JPEG","RUNNING","Encoding the final native JPEG");
 proof.started=2;c->stage=2;c->lut_mode=0;c->origin=millis(c);c->last_wait_errno=c->note_overflow=0;
 j422_stage_init(jr,q,c);struct jpeg_meta*j=&proof.jpeg;j->magic=JPEG_META_MAGIC;j->version=1;j->bytes=sizeof *j;j->width=TEST_W;j->height=TEST_H;j->format=422;
 memset(&steps,0,sizeof steps);steps.magic=0x31534a4e;steps.version=1;
 struct jpeg_ops jo={c,rd,wr,call,ctl,state,idle,barrier,checkpoint,kernel_word};rc=jpeg_run_input(q,jr,memory,&saved,j,&jo,TEST_OUT,1);
 jr->route_samples=steps.count;jr->route_verified=steps.count&&!c->note_overflow;jr->route_flags=c->note_overflow;proof.direct_handoff=full_initialized==2;
 struct jpeg_info info;memset(&info,0,sizeof info);
 if(!rc&&full_pad&&jpeg_parse_info(full_pad,j->stream_bytes,&info)&&info.width==TEST_W&&info.height==TEST_H&&info.format==422){
  for(unsigned i=0;i<3;i++)proof.observed_sampling[i]=info.sampling[i];j->structural=1;j->format=info.format;
  proof.stream_saved=fast_jpeg_write(full_output_target,j->stream_bytes,&jr->target_crc);j->saved=proof.stream_saved;if(!proof.stream_saved)rc=1;
 }else rc=1;
 snprintf(dir,sizeof dir,"%s/01_jpeg",root);if(!j422_owner(c,jr,dir))rc=1;
 if(rc&&!jr->error)jr->error=BJ_SAVE;jr->phase=9;jr->crc=bitb_crc(jr,offsetof(struct bitb_result,crc));j->crc=bitb_crc(j,offsetof(struct jpeg_meta,crc));
 if(rc)proof.failed=2;else{proof.completed=2;nxl_live_progress("PIPE_JPEG_WRITTEN","RUNNING","JPEG stream written; checking release guards");}
done:
 *all=proof.started==2?*jr:*lr;all->ep_fd=ep;all->sma_fd=sma;all->sma_open_flags=flags;
 if(proof.started==2){all->engine_jobs+=lr->engine_jobs;all->lut_hardware_jobs=lr->lut_hardware_jobs;all->table_operations=lr->table_operations;all->table_completions=lr->table_completions;all->table_loaded=lr->table_loaded;all->local_reset_calls+=lr->local_reset_calls;all->translations+=lr->translations;all->matches+=lr->matches;all->source_ok&=lr->source_ok;all->target_ok&=lr->target_ok;all->guards_ok&=lr->guards_ok;all->owner_stable&=lr->owner_stable;all->restore_verified&=lr->restore_verified;}
 all->failed_stage=proof.failed;if(rc&&!all->error)all->error=BJ_SAVE;return rc;
}
