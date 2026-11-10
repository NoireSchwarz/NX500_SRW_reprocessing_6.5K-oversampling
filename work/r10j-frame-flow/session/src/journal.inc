/* SPDX-License-Identifier: AGPL-3.0-only. Bounded append-only state authority. */
#define JOURNAL_MAX_BYTES (4u*1024u*1024u)
#define JOURNAL_MAX_RECORDS 4096u
typedef int (*record_parser)(char *,size_t,void *);
/* Return 0 absent, 1 valid nonempty, -1 invalid/torn. No fallback on bad tail. */
static int records(const char *path,const char *end,record_parser parse,void *context){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return errno==ENOENT?0:-1;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>JOURNAL_MAX_BYTES){close(fd);errno=EINVAL;return -1;}
 FILE *f=fdopen(fd,"r");if(!f){close(fd);return -1;}char line[MAX_RECORD],record[MAX_RECORD];size_t used=0;unsigned count=0;int ok=1;
 while(fgets(line,sizeof line,f)){
  size_t n=strlen(line);if(!n||line[n-1]!='\n'||used+n>=sizeof record){ok=0;break;}memcpy(record+used,line,n);used+=n;record[used]=0;
  if(!strcmp(line,end)){if(++count>JOURNAL_MAX_RECORDS||!parse(record,used,context)){ok=0;break;}used=0;}
 }
 if(ferror(f)||used||!count)ok=0;if(fclose(f))ok=0;if(!ok)errno=EINVAL;return ok?1:-1;
}
struct state_scan {struct session last;uint64_t seq;unsigned count;};
static int state_record(char *text,size_t n,void *v){struct state_scan *c=v;struct session s;if(!decode(text,n,&s)||(c->count&&(s.seq!=c->seq+1||strcmp(s.job,c->last.job))))return 0;c->last=s;c->seq=s.seq;c->count++;return 1;}
static int state_read(const char *job,struct session *s){char path[2048];struct state_scan c={0};if(!bounded(path,sizeof path,job,"STATE.journal"))return -1;int r=records(path,"end=NX51END1\n",state_record,&c);if(r==1){if(strcmp(job,c.last.job))return -1;*s=c.last;}return r;}
static int append_record(const char *dir,const char *name,const char *text){
 char p[2048];if(!bounded(p,sizeof p,dir,name))return 0;int fd=open(p,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 struct stat st;size_t n=strlen(text);int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size+n<=JOURNAL_MAX_BYTES&&write_all(fd,text,n);if(close(fd))ok=0;if(ok)ok=syncdir(dir);return ok;
}
static int state_append(const struct session *s,const char *text){
 struct session old,actual;int found=state_read(s->job,&old);if(found<0||(found&&s->seq!=old.seq+1)){errno=EINVAL;diagnostic("STATE_JOURNAL","SEQUENCE_REJECTED",s->job,errno);return 0;}
 if(!append_record(s->job,"STATE.journal",text)||state_read(s->job,&actual)!=1){diagnostic("STATE_JOURNAL","APPEND_OR_READBACK_FAILED",s->job,errno);return 0;}
 char b[MAX_RECORD];if(!encode(&actual,b)||strcmp(b,text)){errno=EIO;diagnostic("STATE_JOURNAL","READBACK_MISMATCH",s->job,errno);return 0;}return 1;
}
struct pointer_scan {uint64_t seq;char job[1024],previous_job[1024];};
static int pointer_record(char *text,size_t n,void *v){
 (void)n;struct pointer_scan *c=v;char *p=text,head[32],num[32],job[1024],hash[65],end[32],prefix[1400],want[65];uint64_t seq;
 if(!field(&p,"",head,sizeof head)||strcmp(head,"NX51PTR4")||!field(&p,"seq=",num,sizeof num)||!unsigned_number(num,&seq)||!seq||(c->seq&&seq!=c->seq+1)||!field(&p,"job=",job,sizeof job)||!field(&p,"sha256=",hash,sizeof hash)||!field(&p,"end=",end,sizeof end)||strcmp(end,"NX51PTR4END")||*p)return 0;
 size_t len=strlen(root);if(!safe(job,sizeof job,1)||strncmp(job,root,len)||strncmp(job+len,"/JOB_",5)||strchr(job+len+1,'/'))return 0;
 int size=snprintf(prefix,sizeof prefix,"NX51PTR4\nseq=%llu\njob=%s\n",(unsigned long long)seq,job);if(size<=0||size>=(int)sizeof prefix)return 0;digest(prefix,(size_t)size,want);if(strcmp(hash,want))return 0;if(c->seq)strcpy(c->previous_job,c->job);c->seq=seq;strcpy(c->job,job);return 1;
}
static int pointer_read(struct pointer_scan *c){char path[2048];memset(c,0,sizeof *c);if(!bounded(path,sizeof path,root,"CURRENT.journal"))return -1;return records(path,"end=NX51PTR4END\n",pointer_record,c);}
static int pointer_append(const char *job){
 struct pointer_scan old,actual;int found=pointer_read(&old);if(found<0)return 0;char b[1600],hash[65];uint64_t seq=found?old.seq+1:1;
 int n=snprintf(b,sizeof b,"NX51PTR4\nseq=%llu\njob=%s\n",(unsigned long long)seq,job);if(n<=0||n>=(int)sizeof b)return 0;digest(b,(size_t)n,hash);snprintf(b+n,sizeof b-(size_t)n,"sha256=%s\nend=NX51PTR4END\n",hash);
 return append_record(root,"CURRENT.journal",b)&&pointer_read(&actual)==1&&actual.seq==seq&&!strcmp(actual.job,job);
}
/* Old event text is only used to detect contradiction, never to authorize a
 * completed image or hardware cleanup. It cannot substitute for state proof. */
struct legacy_events {uint64_t seq;unsigned front,backend;};
static int event_history(const char *job,struct legacy_events *out){
 char path[2048];memset(out,0,sizeof *out);if(!bounded(path,sizeof path,job,"events.log"))return 0;int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return errno==ENOENT?1:0;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>JOURNAL_MAX_BYTES){close(fd);return 0;}FILE *f=fdopen(fd,"r");if(!f){close(fd);return 0;}
 char line[512],event[96],state[32],stage[16],outcome[40],reason[128];unsigned count=0;int ok=1;
 while(fgets(line,sizeof line,f)){unsigned long long seq;int n=0;if(++count>JOURNAL_MAX_RECORDS||sscanf(line,"seq=%llu event=%95s state=%31s stage=%15s outcome=%39s reason=%127s%n",&seq,event,state,stage,outcome,reason,&n)!=6||!seq||seq<=out->seq||strcmp(line+n,"\n")||!safe(event,sizeof event,0)||!safe(state,sizeof state,0)||!safe(stage,sizeof stage,0)||!safe(outcome,sizeof outcome,0)||!safe(reason,sizeof reason,0)){ok=0;break;}out->seq=seq;out->front|=!strcmp(stage,"front");out->backend|=!strcmp(stage,"backend");}
 if(ferror(f))ok=0;if(fclose(f))ok=0;return ok;
}
static int current(struct session *s){
 char path[2048],text[MAX_RECORD],job[1024];size_t n;struct pointer_scan pointer;int ledger=pointer_read(&pointer);if(ledger<0){diagnostic("CURRENT_LEDGER","INVALID_OR_TORN",root,errno);return -1;}
 if(ledger)strcpy(job,pointer.job);else{
  if(!bounded(path,sizeof path,root,"current.job"))return -1;if(!bytes(path,text,sizeof text,&n))return errno==ENOENT?0:-1;
  char *q=text,hash[80],end[32],prefix[2048],want[65];if(!field(&q,"",end,sizeof end)||strcmp(end,"NX51CURRENT1")||!field(&q,"job=",job,sizeof job)||!field(&q,"sha256=",hash,sizeof hash)||!field(&q,"end=",end,sizeof end)||strcmp(end,"NX51END1")||*q)return -1;
  snprintf(prefix,sizeof prefix,"NX51CURRENT1\njob=%s\n",job);digest(prefix,strlen(prefix),want);if(strcmp(hash,want))return -1;
 }
 size_t len=strlen(root);if(!safe(job,sizeof job,1)||strncmp(job,root,len)||strncmp(job+len,"/JOB_",5)||strchr(job+len+1,'/')||!realdir(job)||!no_symlink_parents(job))return -1;
 struct stat st;if(!bounded(path,sizeof path,job,"SESSION.state"))return -1;if(!lstat(path,&st)&&!S_ISREG(st.st_mode)){errno=EINVAL;return -1;}
 int journal=state_read(job,s);if(journal<0){diagnostic("STATE_JOURNAL","INVALID_OR_TORN",job,errno);return -1;}
 struct legacy_events events;if(!event_history(job,&events))return -1;
 if(journal){if(events.seq>s->seq){diagnostic("STATE_JOURNAL","EVENT_SEQUENCE_AHEAD",job,EINVAL);return -1;}return 1;}
 if(!bytes(path,text,sizeof text,&n)||!decode(text,n,s)||strcmp(s->job,job))return -1;
 if(events.seq>s->seq){s->seq=events.seq;s->requires_reset=1;strcpy(s->state,"RESTART_REQUIRED");strcpy(s->outcome,"UNKNOWN_REQUIRE_RESTART");strcpy(s->reason,"LEGACY_STATE_DIVERGENCE");
  if(events.backend){s->backend_attempt=s->backend_attempt?s->backend_attempt:1;strcpy(s->stage,"backend");strcpy(s->resume,"backend");}else if(events.front){s->front_attempt=s->front_attempt?s->front_attempt:1;strcpy(s->stage,"front");strcpy(s->resume,"front");}
  diagnostic("LEGACY_STATE","DIVERGENCE_HELD",job,0);
 }
 /* A legacy pixel reference is never evidence of a new R4 front-ok. Drop
  * that reference while retaining an unknown/reset hold. This also prevents
  * boot reconciliation from laundering old YUV into a fresh journal. */
 if(s->verified){s->verified=0;strcpy(s->front,"-");s->requires_reset=1;
  if(strcmp(s->state,"RESTART_REQUIRED")){strcpy(s->state,"RESTART_REQUIRED");strcpy(s->outcome,"UNKNOWN_REQUIRE_RESTART");strcpy(s->reason,"LEGACY_FRONT_REQUIRES_FRESH");}
  diagnostic("LEGACY_FRONT","FRESH_REQUIRED_HELD",job,0);
 }
 return 1;
}
