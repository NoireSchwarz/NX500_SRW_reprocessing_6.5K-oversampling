/* Session failure is historical. Any owned held task can request a fresh
 * read-only audit; only its current, token-bound proof can permit new work. */
static int resource_check_candidate(const struct session*s){return s->requires_reset&&(!strcmp(s->state,"RESTART_REQUIRED")||!strcmp(s->state,"COMPLETE"));}
static int rearm_previous(const struct session*s,struct session*previous){
 if(!resource_check_candidate(s))return 0;struct session canonical;char a[MAX_RECORD],b[MAX_RECORD];
 if(state_read(s->job,&canonical)!=1||!encode(&canonical,a)||!encode(s,b)||strcmp(a,b))return 0;
 if(s->front_attempt||s->backend_attempt||s->verified){*previous=*s;return 1;}
 struct pointer_scan p;if(pointer_read(&p)!=1||strcmp(p.job,s->job)||!p.previous_job[0]||!realdir(p.previous_job)||!no_symlink_parents(p.previous_job)||state_read(p.previous_job,previous)!=1)return 0;
 if(!strcmp(previous->state,"FRONT_RUNNING")||!strcmp(previous->state,"BACKEND_RUNNING"))return 0;
 return previous->front_attempt||previous->backend_attempt||previous->verified;
}
static int rearm_receipt(const struct session*s,const struct session*source,const char*dir){
 const char*t=getenv("NX51_SESSION_TOKEN");if(!t||strlen(t)!=32||!realdir(dir)||!no_symlink_parents(dir))return 0;
 char expected[2048],path[2048],text[MAX_RECORD],copy[MAX_RECORD],v[1200],h[65],want[65],record[MAX_RECORD];size_t n;
 if(snprintf(expected,sizeof expected,"%s/rearm_%s",s->job,t)>=(int)sizeof expected||strcmp(expected,dir)||!bounded(path,sizeof path,dir,"REARM.proof")||!bytes(path,text,sizeof text,&n))return 0;
 memcpy(copy,text,n+1);char*p=text;
#define RF(k,w) do{if(!field(&p,k,v,sizeof v)||strcmp(v,w))return 0;}while(0)
 RF("","NX51REARMPROOF3");RF("job=",s->job);RF("previous=",source->job);RF("token=",t);
 if(!encode(source,record))return 0;digest(record,strlen(record),want);RF("source_record_sha256=",want);
 if(!encode(s,record))return 0;digest(record,strlen(record),want);RF("target_record_sha256=",want);
 uint64_t count;if(!field(&p,"resource_operations=",v,sizeof v)||!unsigned_number(v,&count)||!count||count>128)return 0;
 if(!field(&p,"resource_ranges=",v,sizeof v)||!unsigned_number(v,&count)||count>64)return 0;
 RF("readonly=","1");RF("native_calls=","0");RF("resource_snapshots=","3");RF("front_allocation_retired=","1");RF("backend_allocation_retired=","1");RF("allocator_layout_verified=","1");RF("allocator_final_revalidated=","1");
 if(!field(&p,"front_range_state=",v,sizeof v)||(strcmp(v,"VACANT")&&strcmp(v,"REUSED")))return 0;
 if(!field(&p,"backend_range_state=",v,sizeof v)||(strcmp(v,"VACANT")&&strcmp(v,"REUSED")))return 0;
 RF("backend_resources_retired=","1");RF("native_outcome_resolved=","1");RF("known_roots_clear=","1");RF("code_original=","1");RF("shell_parked=","1");RF("queue_empty=","1");RF("workers_idle=","1");RF("reason=","EMPTY_SESSION_RESOURCE_PROOF_VERIFIED");
 size_t prefix=(size_t)(p-text);if(!field(&p,"sha256=",h,sizeof h)||strlen(h)!=64)return 0;RF("end=","NX51REARMEND3");if(*p)return 0;digest(copy,prefix,want);return !strcmp(h,want);
#undef RF
}
