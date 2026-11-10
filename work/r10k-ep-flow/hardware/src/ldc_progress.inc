/* Sparse destination markers are observations, NEVER completion authority.
 * Seed only after ownership/addresses are checked and before any start.
 * No source byte, interrupt, queue, mask or reset is changed by capture. */
#include "ldc_timed_wait.h"
#define LDP_POINTS 256u
#define LDP_MAX 25u
static const unsigned ldp_rows[8]={0,1,24,1408,1471,1472,2160,4319};
static const unsigned ldp_cols[16]={0,64,128,256,448,464,472,480,896,960,1440,2320,3240,4640,6000,6472};
static const uint32_t ldp_top_offsets[]={0x1000,0x1008,0x100c,0x1100,0x1200,0x132c,0x172c,0x1114,0x1048,0x120,0x124};
enum {LDP_TOP_WORDS=sizeof ldp_top_offsets/sizeof *ldp_top_offsets};
struct ldp_record {uint32_t phase,ms,flags,clock[2],top[LDP_TOP_WORDS],mask[5],raw[5],rtos[5],linux_mask[5],core5[3],debug[20],changed[LDP_POINTS],changed_points,changed_bytes;};
static struct {uint32_t seeded,started_ms,count,overflow,kernel_read_errors;unsigned char marker[LDP_POINTS][8];struct ldp_record records[LDP_MAX];} ldp;
static unsigned ldp_offset(unsigned i){
 unsigned plane=i/128,k=i%128,row=ldp_rows[k/16],col=ldp_cols[k%16];
 if(row>=TEST_H)row=TEST_H-1;if(col>TEST_W-8)col=TEST_W-8;
 return SW_LDC_OUT+(plane?SW_FRONT_UV:0)+row*SW_FRONT_STRIDE+col;
}
static uint32_t ldp_mask(struct native_context*c,unsigned i){
 uint32_t bits=0;volatile const unsigned char*p=c->memory+ldp_offset(i);
 for(unsigned j=0;j<8;j++)if(p[j]!=ldp.marker[i][j])bits|=1u<<j;
 return bits;
}
static void ldc_progress_capture(void*v,unsigned phase){
 struct native_context*c=v;
 if(phase==0){
  memset(&ldp,0,sizeof ldp);
  for(unsigned i=0;i<LDP_POINTS;i++)for(unsigned j=0;j<8;j++)
   c->memory[ldp_offset(i)+j]=ldp.marker[i][j]=(unsigned char)(full_workspace_original.nonce+i*97u+j*31u+0x96u);
  barrier(c);ldp.seeded=1;
 }
 if(phase==1){ldp.started_ms=millis(c);return;}
 if(ldp.count>=LDP_MAX){ldp.overflow=1;return;}
 struct ldp_record*r=ldp.records+ldp.count++;r->phase=phase;r->ms=millis(c)-c->origin;
 r->clock[0]=c->top[0x1020/4];r->clock[1]=c->top[0x1024/4];
 for(unsigned i=0;i<LDP_TOP_WORDS;i++)r->top[i]=c->top[ldp_top_offsets[i]/4];
 for(unsigned i=0;i<5;i++){r->mask[i]=c->top[(0x1070+i*4)/4];r->raw[i]=c->top[(0x10c8+i*4)/4];r->rtos[i]=c->rtos[(0xaac+i*4)/4];}
 if(kernel_read(c,EP_KERNEL_STATE_VA+0x64,r->linux_mask,20)&&kernel_read(c,0xc0755604,r->core5,12))r->flags|=1u;else ldp.kernel_read_errors++;
 if(!((r->clock[0]|r->clock[1])&8u)&&c->ldc_bytes>=0x458){
  for(unsigned i=0;i<20;i++)r->debug[i]=c->ldc[lh_debug_offsets[i]/4];r->flags|=2u;
 }
 if(r->clock[0]==c->top[0x1020/4]&&r->clock[1]==c->top[0x1024/4])r->flags|=4u;else r->flags&=~2u;
 for(unsigned i=0;i<LDP_POINTS;i++){
  uint32_t bits=r->changed[i]=ldp_mask(c,i);r->changed_points+=!!bits;
  for(unsigned j=0;j<8;j++)r->changed_bytes+=(bits>>j)&1u;
 }
}
static int ldc_progress_coverage(void*v){
 struct native_context*c=v;if(!ldp.seeded)return 0;
 for(unsigned i=0;i<LDP_POINTS;i++)if(!ldp_mask(c,i))return 0;
 return 1; /* called ONLY after fresh kernel completion, never instead of it */
}
static int ldp_wait_call(void*v,uint32_t*p){return ctl(v,0x40086806,p);}
static int ldp_wait_errno(void*v){return ((struct native_context*)v)->last_errno;}
static void ldp_wait_observe(void*v){ldc_progress_capture(v,4);}
static int ldc_progress_wait(void*v,uint32_t*p){
 struct native_context*c=v;struct ldc_wait_ops o={c,millis,ldp_wait_call,ldp_wait_errno,ldp_wait_observe};
 int rc=ldc_timed_wait(&o,ldp.started_ms,p[0]),e=rc<0?errno:0;
 c->last_ioctl=0x40086806;c->last_return=rc;c->last_errno=c->last_wait_errno=e;
 fprintf(c->log,"LDC_BOUNDED_WAIT return=%d errno=%d budget_ms=%u elapsed_ms=%u observations=%u overflow=%u\n",rc,e,p[0],millis(c)-ldp.started_ms,ldp.count,ldp.overflow);
 return rc;
}
static void ldp_array(FILE*f,const uint32_t*p,unsigned n){fputc('[',f);for(unsigned i=0;i<n;i++)fprintf(f,"%s%u",i?",":"",p[i]);fputc(']',f);}
static int ldc_progress_file(const char*root,const struct bitb_request*q,const struct bitb_result*r){
 char path[1200];snprintf(path,sizeof path,"%s/ldc-progress-audit.json",root);FILE*f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"schema\":2,\"version\":\"R9a\",\"request_crc\":%u,\"pa\":%u,\"nonce\":%u,\"source_sha256\":\"%s\",\"seeded\":%u,\"marker_bytes\":2048,\"points\":256,\"bytes_per_point\":8,\"seed_before_start\":true,\"is_completion_authority\":false,\"source_modified\":false,\"native_M2M_source_captured\":false,\"source_block\":[480,4320],\"processing_block\":[480,4320],\"overlap\":[0,0],\"wait_budget_ms\":5000,\"wait_slice_ms\":250,\"trial_scope\":\"NATIVE_M2M_SINGLE_FULL_FRAME\",\"full_frame_completed\":%s,\"output_window\":[0,0,6479,4319],\"fresh_completion\":%u,\"wait_return\":%d,\"wait_errno\":%u,\"overflow\":%u,\"kernel_read_errors\":%u,\"rows\":[",q->crc,q->pa,full_workspace_original.nonce,full_workspace_original.source_sha256,ldp.seeded,r->fresh_completion&&ldc_audit.stop_verified&&r->restore_verified&&!r->error?"true":"false",r->fresh_completion,r->wait_return,r->wait_errno,ldp.overflow,ldp.kernel_read_errors);
 for(unsigned i=0;i<8;i++)fprintf(f,"%s%u",i?",":"",ldp_rows[i]);fputs("],\"columns\":[",f);for(unsigned i=0;i<16;i++)fprintf(f,"%s%u",i?",":"",ldp_cols[i]);
 fputs("],\"point_order\":\"plane_Y_then_UV,row_then_column\",\"changed_mask_bits\":\"bit_j: byte_j differs from its pre-start marker\",\"top_offsets\":",f);ldp_array(f,ldp_top_offsets,LDP_TOP_WORDS);
 fputs(",\"debug_offsets\":",f);ldp_array(f,lh_debug_offsets,20);fputs(",\"records\":[",f);
 for(unsigned i=0;i<ldp.count;i++){
  const struct ldp_record*s=ldp.records+i;fprintf(f,"%s{\"phase\":%u,\"ms\":%u,\"flags\":%u,\"changed_points\":%u,\"changed_bytes\":%u",i?",":"",s->phase,s->ms,s->flags,s->changed_points,s->changed_bytes);
  const char*names[]={"clock","top","mask","raw","rtos","linux_mask","core5","debug","changed"};const uint32_t*arrays[]={s->clock,s->top,s->mask,s->raw,s->rtos,s->linux_mask,s->core5,s->debug,s->changed};const unsigned sizes[]={2,LDP_TOP_WORDS,5,5,5,5,3,20,LDP_POINTS};
  for(unsigned k=0;k<9;k++){fprintf(f,",\"%s\":",names[k]);ldp_array(f,arrays[k],sizes[k]);}fputc('}',f);
 }
 fputs("]}\n",f);int ok=!ferror(f)&&ftell(f)>0&&ftell(f)<=65536;if(fclose(f))ok=0;return ok;
}
