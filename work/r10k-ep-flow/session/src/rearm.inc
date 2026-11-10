/* Narrow read-only-proof rearm for an untouched inherited cold-hold job.
 * This never releases native memory/locks, resumes a worker, or imports YUV.
 */
#include "preflight_hold.inc"
static int empty_cold_session(const struct session *s){
 return s->seq==1&&!strcmp(s->state,"RESTART_REQUIRED")&&s->requires_reset&&!s->verified&&!strcmp(s->front,"-")&&!s->front_attempt&&!s->backend_attempt&&!strcmp(s->stage,"idle")&&!strcmp(s->resume,"front")&&!strcmp(s->reason,"REAL_KERNEL_RESTART_NOT_VERIFIED")&&!strcmp(s->outcome,"SAFE_RELEASED_COLD")&&!strcmp(s->look,"-")&&!s->strength;
}
static int historical_rearm_previous(const struct session *s,struct session *previous){
 int legacy=preflight_legacy(s);if(!empty_cold_session(s)&&!legacy)return 0;
 struct session canonical;if(state_read(s->job,&canonical)!=1||
  (legacy?!preflight_legacy(&canonical):!empty_cold_session(&canonical))||strcmp(canonical.job,s->job)||strcmp(canonical.boot,s->boot))return 0;
 char path[2048],text[512];size_t n;static const char event[]="seq=1 event=NEW_SESSION state=RESTART_REQUIRED stage=idle outcome=SAFE_RELEASED_COLD reason=REAL_KERNEL_RESTART_NOT_VERIFIED\n";
 if(!legacy&&(!bounded(path,sizeof path,s->job,"events.log")||!bytes(path,text,sizeof text,&n)||n!=sizeof event-1||memcmp(text,event,n)))return 0;
 struct pointer_scan p;if(pointer_read(&p)!=1||strcmp(p.job,s->job)||!p.previous_job[0]||!strcmp(p.previous_job,s->job)||!realdir(p.previous_job)||!no_symlink_parents(p.previous_job)||state_read(p.previous_job,previous)!=1)return 0;
 if(strcmp(previous->job,p.previous_job)||strcmp(previous->state,"COMPLETE")||strcmp(previous->outcome,"VERIFIED_COMPLETE")||(strcmp(previous->reason,"JPEG_SAVED")&&strcmp(previous->reason,"SRW_LUT_JPEG_SAVED"))||!previous->verified||!previous->requires_reset||!previous->front_attempt||!previous->backend_attempt||strcmp(previous->stage,"backend")||strcmp(previous->resume,"none"))return 0;
 size_t j=strlen(previous->job);return !strncmp(previous->front,previous->job,j)&&!strncmp(previous->front+j,"/front_",7)&&realdir(previous->front)&&no_symlink_parents(previous->front);
}
static int historical_rearm_receipt(const struct session *s,const struct session *previous,const char *dir){
 const char *token=getenv("NX51_SESSION_TOKEN");if(!token||strlen(token)!=32||!realdir(dir)||!no_symlink_parents(dir))return 0;
 char expected[2048],path[2048],text[MAX_RECORD],copy[MAX_RECORD],value[1200],hash[65],want[65];size_t n;
 if(snprintf(expected,sizeof expected,"%s/rearm_%s",s->job,token)>=(int)sizeof expected||strcmp(dir,expected)||!bounded(path,sizeof path,dir,"REARM.proof")||!bytes(path,text,sizeof text,&n)||n>=sizeof copy)return 0;
 memcpy(copy,text,n+1);char *p=text;
#define REARM_FIELD(k,v) do{if(!field(&p,k,value,sizeof value)||strcmp(value,v))return 0;}while(0)
 REARM_FIELD("","NX51REARMPROOF2");REARM_FIELD("job=",s->job);REARM_FIELD("previous=",previous->job);REARM_FIELD("token=",token);
 REARM_FIELD("readonly=","1");REARM_FIELD("native_calls=","0");REARM_FIELD("resource_snapshots=","3");REARM_FIELD("front_allocation_retired=","1");REARM_FIELD("backend_allocation_retired=","1");REARM_FIELD("allocator_layout_verified=","1");REARM_FIELD("allocator_final_revalidated=","1");
 if(!field(&p,"front_range_state=",value,sizeof value)||(strcmp(value,"VACANT")&&strcmp(value,"REUSED")))return 0;
 if(!field(&p,"backend_range_state=",value,sizeof value)||(strcmp(value,"VACANT")&&strcmp(value,"REUSED")))return 0;
 REARM_FIELD("backend_resources_retired=","1");REARM_FIELD("native_outcome_resolved=","1");REARM_FIELD("known_roots_clear=","1");REARM_FIELD("code_original=","1");REARM_FIELD("shell_parked=","1");REARM_FIELD("queue_empty=","1");REARM_FIELD("workers_idle=","1");REARM_FIELD("reason=","EMPTY_SESSION_RESOURCE_PROOF_VERIFIED");
 size_t prefix=(size_t)(p-text);if(!field(&p,"sha256=",hash,sizeof hash)||strlen(hash)!=64)return 0;REARM_FIELD("end=","NX51REARMEND2");if(*p)return 0;digest(copy,prefix,want);return !strcmp(hash,want);
#undef REARM_FIELD
}

#include "resource_rearm.inc"
