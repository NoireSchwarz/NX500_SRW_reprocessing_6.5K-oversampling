#include "front_handoff.h"
struct front_handoff_context {const char *dir,*boundary;};
static const char *front_handoff_error="POST_FRAME_CONTEXT_UNVERIFIED";
static struct fh_result front_handoff_last;
static uint32_t front_handoff_context_read(void *opaque){
 (void)opaque;uint32_t flags=0;
 if(still_owned())flags|=FH_OWNED;if(slot_original())flags|=FH_SLOT_ORIGINAL;
 if(!attached&&detached_ok)flags|=FH_DETACHED;
 if(!api_inflight&&!native_signal)flags|=FH_API_QUIET;
 if(code_restored&&idle_registers_verified&&!call_boundary_active)flags|=FH_REGISTERS_RESTORED;
 if(!camera_paused&&camera_resumed_verified)flags|=FH_CAMERA_RESUMED;
 if(p49_native_done&&task_quiescent_verified)flags|=FH_NATIVE_DONE;return flags;
}
static int front_handoff_report(void *opaque,const struct fh_result *r,unsigned final){
 const struct front_handoff_context*c=opaque;const struct fr_wait_result*w=&r->readiness;
 const struct fr_snapshot*s=final?&w->last:&w->first;uint32_t context=final?r->last_context:r->first_context;
 char path[1200];if(snprintf(path,sizeof path,"%s/front_handoff_%s_%s.json",c->dir,c->boundary,final?"final":"first")>=(int)sizeof path)return 0;
 FILE*f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"format\":\"NXFRONTHANDOFF1\",\"boundary\":\"%s\",\"read_only\":true,\"native_retries\":0,\"final\":%s,\"context_flags\":%u,\"context_missing_mask\":%u,\"read_mask\":%u,\"failure_mask\":%u,\"samples\":%u,\"consecutive\":%u,\"elapsed_ms\":%u,\"limit_ms\":%u,\"ready\":%s,\"cancelled\":%u,\"timed_out\":%u,\"clock_failed\":%u,\"fields\":[",c->boundary,final?"true":"false",context,FH_REQUIRED&~context,s->read_mask,s->failure_mask,w->samples,w->consecutive,w->elapsed_ms,FR_WAIT_MS,w->ready?"true":"false",w->cancelled,w->timed_out,w->clock_failed);
 for(unsigned i=0;i<FR_FIELDS;i++){const struct fr_field*v=s->field+i;fprintf(f,"%s{\"name\":\"%s\",\"address\":%u,\"observed\":%u,\"readable\":%u,\"passed\":%u}",i?",":"",fr_descriptors[i].name,v->address,v->value,v->readable,v->passed);}
 fputs("]}\n",f);int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static int front_handoff_released(const char *dir,const char *boundary){
 struct front_handoff_context c={dir,boundary};
 int ok=fh_wait(&c,front_readiness_read,front_readiness_clock,front_readiness_pause,front_readiness_cancel,front_handoff_context_read,front_handoff_report,&front_handoff_last);
 const struct fr_wait_result*w=&front_handoff_last.readiness;uint32_t missing=FH_REQUIRED&~front_handoff_last.last_context;
 front_handoff_error=ok?"POST_FRAME_STABLE":!w->report_ok?"POST_FRAME_EVIDENCE_SAVE_FAILED":missing?"POST_FRAME_HARD_CONTEXT_CHANGED":w->clock_failed?"POST_FRAME_CLOCK_FAILED":w->cancelled?"POST_FRAME_CANCELLED":"POST_FRAME_STILL_NOT_STABLE";
 fprintf(log_file,"FRONT_HANDOFF boundary=%s result=%d context_missing_mask=%u read_mask=%u failure_mask=%u samples=%u consecutive=%u elapsed_ms=%u reason=%s\n",boundary,ok,missing,w->last.read_mask,w->last.failure_mask,w->samples,w->consecutive,w->elapsed_ms,front_handoff_error);fflush(log_file);return ok;
}
