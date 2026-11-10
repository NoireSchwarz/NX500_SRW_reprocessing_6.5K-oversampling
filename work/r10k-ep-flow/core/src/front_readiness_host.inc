/* SPDX-License-Identifier: AGPL-3.0-only. Read-only Linux adapter. */
#include "front_readiness.h"
#include "front_preflight.h"
static struct fr_wait_result front_readiness_last;
_Static_assert(P39_TASK==FR_TASK,"readiness task address");
static int front_readiness_read(void *context,uint32_t address,uint32_t *out,unsigned words){
 (void)context;return ro_words(address,out,words);
}
static int front_idle(void){
 struct fr_snapshot snapshot;fr_take(&snapshot,front_readiness_read,NULL);return fr_config_admitted(&snapshot);
}
static uint64_t front_readiness_clock(void *context){(void)context;return ns()/1000000ULL;}
static void front_readiness_pause(void *context,unsigned milliseconds){(void)context;pause_ms(milliseconds);}
static int front_readiness_cancel(void *context){(void)context;return stop_requested!=0;}
static int front_readiness_report(void *context,const struct fr_wait_result *r,unsigned final){
 const char *dir=context;const struct fr_snapshot *s=final?&r->last:&r->first;
 char path[1200],temporary[1200];const char *name=final?"front_readiness_final.json":"front_readiness_first.json";
 if(snprintf(path,sizeof path,"%s/%s",dir,name)>=(int)sizeof path||snprintf(temporary,sizeof temporary,"%s/%s.tmp",dir,name)>=(int)sizeof temporary)return 0;
 FILE *f=fopen(temporary,"wx");if(!f)return 0;
 fprintf(f,"{\n\"format\":\"NXFRONTREADINESS1\",\"phase\":\"%s\",\"read_only\":true,\"native_tasks_submitted\":0,\"wait_limit_ms\":%u,\"sample_spacing_ms\":%u,\"required_consecutive_passes\":%u,\"samples\":%u,\"consecutive_passes\":%u,\"elapsed_ms\":%u,\"ready\":%s,\"cancelled\":%s,\"timed_out\":%s,\"clock_failed\":%s,\"read_mask\":%u,\"failure_mask\":%u,\"repeated_pair_stable\":%s,\"stability_failure_bit\":%u,\n\"fields\":[\n",final?"FINAL":"FIRST",FR_WAIT_MS,FR_SAMPLE_MS,FR_REQUIRED_STABLE,r->samples,r->consecutive,r->elapsed_ms,r->ready?"true":"false",r->cancelled?"true":"false",r->timed_out?"true":"false",r->clock_failed?"true":"false",s->read_mask,s->failure_mask,s->stable_pair?"true":"false",FR_STABILITY_BIT);
 for(unsigned i=0;i<FR_FIELDS;i++){
  const struct fr_descriptor *d=fr_descriptors+i;const struct fr_field *v=s->field+i;
  fprintf(f,"%s{\"name\":\"%s\",\"failure_bit\":%u,\"address\":%u,\"readable\":%s,\"observed\":%u,\"mask\":%u,\"rule\":\"%s\",\"expected\":%u,\"pointer_min\":%u,\"pointer_max\":%u,\"pointer_alignment\":%u,\"passed\":%s}",i?",\n":"",d->name,1u<<i,v->address,v->readable?"true":"false",v->value,d->mask,d->rule==FR_POINTER?"INCLUSIVE_ALIGNED_POINTER":(d->rule==FR_NOT_EQUAL?"NOT_EQUAL":"EQUAL"),d->expected,d->rule==FR_POINTER?0x81000000u:0u,d->rule==FR_POINTER?0x93fff000u:0u,d->rule==FR_POINTER?4u:0u,v->passed?"true":"false");
 }
 #ifdef NXL_COLD_INIT_TEST
 fprintf(f,"\n],\"config_admission_only\":true,\"start_ready\":%s}\n",s->failure_mask?"false":"true");
#else
 fputs("\n]}\n",f);
#endif
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;
 if(ok&&rename(temporary,path))ok=0;
 if(ok){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)ok=0;else{if(fsync(fd))ok=0;if(close(fd))ok=0;}}
 return ok;
}
static int front_initial_ready(const char *dir){
 memset(&front_readiness_last,0,sizeof front_readiness_last);
#ifdef NXL_COLD_INIT_TEST
 return fr_wait_mode((void*)dir,front_readiness_read,front_readiness_clock,front_readiness_pause,front_readiness_cancel,front_readiness_report,&front_readiness_last,1);
#else
 return fr_wait((void*)dir,front_readiness_read,front_readiness_clock,front_readiness_pause,front_readiness_cancel,front_readiness_report,&front_readiness_last);
#endif
}
