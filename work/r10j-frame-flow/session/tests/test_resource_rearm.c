#define main session_unused_main
#include "../src/session.c"
#undef main
#include <assert.h>
static int call(const char*cmd,const char*a){char*args[]={"session","/mnt/mmc/NXDEMO51",(char*)cmd,(char*)a,NULL};return session_unused_main(a?4:3,args);}
static void put(const char*path,const char*text){int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0&&write_all(fd,text,strlen(text))&&!close(fd));}
int main(void){
 const char*token="0123456789abcdef0123456789abcdef";assert(!setenv("NX51_SESSION_TOKEN",token,1));assert(call("status",NULL)==0);
 struct session s,source;assert(current(&s)==1&&s.requires_reset&&!strcmp(s.state,"RESTART_REQUIRED"));assert(strcmp(s.boot,boot));
 assert(rearm_previous(&s,&source)&&!strcmp(source.job,s.job));char original[1024];strcpy(original,s.job);
 char dir[1400],path[1500],text[8192],record[8192],source_hash[65],target_hash[65],hash[65];
 assert(encode(&s,record));digest(record,strlen(record),target_hash);assert(encode(&source,record));digest(record,strlen(record),source_hash);
 snprintf(dir,sizeof dir,"%s/rearm_%s",s.job,token);assert(!mkdir(dir,0700));
 int n=snprintf(text,sizeof text,"NX51REARMPROOF3\njob=%s\nprevious=%s\ntoken=%s\nsource_record_sha256=%s\ntarget_record_sha256=%s\nresource_operations=1\nresource_ranges=1\nreadonly=1\nnative_calls=0\nresource_snapshots=3\nfront_allocation_retired=1\nbackend_allocation_retired=1\nallocator_layout_verified=1\nallocator_final_revalidated=1\nfront_range_state=VACANT\nbackend_range_state=VACANT\nbackend_resources_retired=1\nnative_outcome_resolved=1\nknown_roots_clear=1\ncode_original=1\nshell_parked=1\nqueue_empty=1\nworkers_idle=1\nreason=EMPTY_SESSION_RESOURCE_PROOF_VERIFIED\n",s.job,source.job,token,source_hash,target_hash);
 digest(text,(size_t)n,hash);snprintf(text+n,sizeof text-(size_t)n,"sha256=%s\nend=NX51REARMEND3\n",hash);snprintf(path,sizeof path,"%s/REARM.proof",dir);put(path,text);
 assert(call("verify-resource-proof",dir)==0);assert(call("renew-front-grant",dir)!=0);
 /* Keep a valid checksum while changing the expected current/source record.
  * These are identity binding checks, not just damaged-file checks. */
 struct session changed=s;changed.front_attempt++;
 assert(!rearm_receipt(&changed,&source,dir));changed=source;changed.front_attempt++;
 assert(!rearm_receipt(&s,&changed,dir));
 assert(!setenv("NX51_SESSION_TOKEN","fedcba9876543210fedcba9876543210",1));
 assert(!rearm_receipt(&s,&source,dir));assert(!setenv("NX51_SESSION_TOKEN",token,1));
 size_t text_len=strlen(text);text[text_len-1]=0;put(path,text);
 assert(call("verify-resource-proof",dir)!=0);text[text_len-1]='\n';put(path,text);
 assert(call("verify-resource-proof",dir)==0);
 char locks[1600];snprintf(locks,sizeof locks,"NX51LOCKRESET1\njob=%s\ntoken=%s\nengine=absent\ntrace=absent\nmemory_test=absent\nrecovery=absent\nnext_round=fresh_session_token\nend=NX51LOCKRESETEND1\n",s.job,token);snprintf(path,sizeof path,"%s/LOCKS_RESET",dir);put(path,locks);
 assert(call("renew-front-grant",dir)==0&&exact_text("/opt/usr/nx-lut-lite/state/p49-experiment.state","NXP33INSTALL1\nREADY=0.3.6-demo50\n"));
 assert(call("rearm-fresh",dir)==0&&current(&s)==1&&strcmp(s.job,original)&&!s.requires_reset&&!s.verified&&!s.front_attempt&&!s.backend_attempt&&!strcmp(s.state,"NEED_STILL_INIT"));
 struct session old;assert(state_read(original,&old)==1&&old.requires_reset&&old.front_attempt==1&&!strcmp(old.state,"RESTART_REQUIRED"));
 puts("PASS failed task/unchanged-or-changed boot may request proof; grant requires lock receipt; proofV3 creates fresh job and preserves old failure");return 0;
}
