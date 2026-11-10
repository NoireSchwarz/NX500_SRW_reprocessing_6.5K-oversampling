/* SPDX-License-Identifier: AGPL-3.0-only.
 * Owned ISP NV16 -> one native JPEG candidate. CPU work is byte-preserving
 * padding removal, hashes, proof validation and durable file publication.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <inttypes.h>
#include "../../hardware/src/bitb_job.h"
#include "../../hardware/src/pipeline_proof.h"
#include "sha256.h"
#include "../../core/src/param_dma_contract.h"
#include "../../core/src/cc_contract.h"
#include "front_json.h"
#include "post_json.h"
#include "../../common/shared_workspace_io.h"
#define VERSION "0.4.0-demo51-422"
#define FRAME422_BYTES 55987200u
static uint64_t timing_start_ms;
static const char *timing_phase;
static int monotonic_ms(uint64_t *out){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000L)return 0;*out=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;return 1;}
static void timing_begin(const char *phase){timing_phase=phase;if(monotonic_ms(&timing_start_ms))fprintf(stderr,"NXPOSTTIMING1 event=BEGIN phase=%s monotonic_ms=%" PRIu64 " elapsed_ms=0 result=RUNNING\n",phase,timing_start_ms);else{timing_start_ms=UINT64_MAX;fprintf(stderr,"NXPOSTTIMING1 event=BEGIN phase=%s clock_error=1 result=RUNNING\n",phase);}}
static void timing_end(const char *result){if(!timing_phase)return;uint64_t now;if(timing_start_ms!=UINT64_MAX&&monotonic_ms(&now)&&now>=timing_start_ms)fprintf(stderr,"NXPOSTTIMING1 event=END phase=%s monotonic_ms=%" PRIu64 " elapsed_ms=%" PRIu64 " result=%s\n",timing_phase,now,now-timing_start_ms,result);else fprintf(stderr,"NXPOSTTIMING1 event=END phase=%s clock_error=1 result=%s\n",timing_phase,result);timing_phase=NULL;}
static volatile sig_atomic_t canceled;
static void onsignal(int value){(void)value;canceled=1;}
struct cfg {const char *y,*uv,*ys,*uvs,*source,*producer,*ps,*out,*profile,*final_check;unsigned pw,ph,stride,x,y0,w,h;};
static int hex64(const char *s){if(!s||strlen(s)!=64)return 0;for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
static int number(const char *s,unsigned *out){if(!s||!*s||(s[0]=='0'&&s[1]))return 0;unsigned long v=0;for(;*s;s++){if(*s<'0'||*s>'9'||v>100000000u)return 0;v=v*10+(unsigned)(*s-'0');}if(v>1000000000u)return 0;*out=(unsigned)v;return 1;}
static int parse(int argc,char **argv,struct cfg *c){
 memset(c,0,sizeof *c);unsigned seen=0;
 for(int i=1;i<argc;i+=2){if(i+1>=argc)return 0;const char *k=argv[i],*v=argv[i+1];
#define STR(K,F) if(!strcmp(k,K)){if(c->F||!*v)return 0;c->F=v;continue;}
 STR("--y",y) STR("--uv",uv) STR("--y-sha256",ys) STR("--uv-sha256",uvs) STR("--source-srw-sha256",source) STR("--producer-report",producer) STR("--producer-report-sha256",ps) STR("--output-dir",out) STR("--yuv",profile) STR("--final-check",final_check)
#undef STR
#define NUM(K,F,B) if(!strcmp(k,K)){if((seen&(1u<<B))||!number(v,&c->F))return 0;seen|=1u<<B;continue;}
 NUM("--plane-width",pw,0) NUM("--plane-height",ph,1) NUM("--stride",stride,2) NUM("--crop-x",x,3) NUM("--crop-y",y0,4) NUM("--width",w,5) NUM("--height",h,6)
#undef NUM
 return 0;
 }
 return (!c->final_check||!strcmp(c->final_check,"internal")||!strcmp(c->final_check,"runner-independent"))&&seen==127u&&c->y&&c->uv&&c->producer&&c->out&&hex64(c->ys)&&hex64(c->uvs)&&hex64(c->source)&&hex64(c->ps)&&c->profile&&!strcmp(c->profile,"native-nv16")&&c->pw==6480&&c->ph==4320&&c->stride==6496&&c->w==6480&&c->h==4320&&!c->x&&!c->y0;
}
static int final_check_delegated(const struct cfg *c){return c->final_check&&!strcmp(c->final_check,"runner-independent");}
static const char *final_check_name(const struct cfg *c){return final_check_delegated(c)?"runner-independent":"internal";}
static const char *success_contract(const struct cfg *c){return final_check_delegated(c)?"PUBLISHED_PENDING_RUNNER_VERIFICATION":"PUBLISHED_AND_INTERNAL_VERIFICATION";}
static int open_regular(const char *p,uint64_t exact){int fd=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat s;if(fd<0)return -1;if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size<0||(exact&&(uint64_t)s.st_size!=exact)){close(fd);return -1;}return fd;}
static int read_exact(int fd,void *v,size_t n,off_t offset){unsigned char *p=v;while(n){if(canceled)return 0;ssize_t q=pread(fd,p,n,offset);if(q<0&&errno==EINTR)continue;if(q<=0)return 0;p+=q;n-=(size_t)q;offset+=q;}return 1;}
static int fd_digest(int fd,char out[65],uint64_t *bytes,uint32_t *crc){struct sha256 s;sha256_init(&s);uint32_t c=~0u;unsigned char buffer[65536],digest[32];off_t pos=0;for(;;){if(canceled)return 0;ssize_t q=pread(fd,buffer,sizeof buffer,pos);if(q<0&&errno==EINTR)continue;if(q<0)return 0;if(!q)break;sha256_update(&s,buffer,(size_t)q);if(crc)c=bitb_crc_step(c,buffer,(unsigned)q);pos+=q;}sha256_final(&s,digest);digest_hex(digest,out);if(bytes)*bytes=(uint64_t)pos;if(crc)*crc=~c;return 1;}
static int fd_hash(int fd,char out[65],uint64_t *bytes){return fd_digest(fd,out,bytes,NULL);}
static int path_digest(const char *p,char out[65],uint64_t *bytes,uint32_t *crc){int fd=open_regular(p,0);if(fd<0)return 0;int ok=fd_digest(fd,out,bytes,crc);close(fd);return ok;}
static int path_hash(const char *p,char out[65],uint64_t *bytes){return path_digest(p,out,bytes,NULL);}
struct attestation {uint32_t nonce,cc_reloaded;char front[65],param[65];};
static int u32value(const char *s,uint32_t *out){uint64_t n=0;if(!*s||(s[0]=='0'&&s[1]))return 0;for(;*s;s++){if(*s<'0'||*s>'9')return 0;n=n*10+(unsigned)(*s-'0');if(n>UINT32_MAX)return 0;}*out=(uint32_t)n;return 1;}
static int lines_fd(int fd,char *text,size_t cap,char **line,unsigned n){struct stat st;if(fstat(fd,&st)||st.st_size<=0||(uint64_t)st.st_size>=cap||!read_exact(fd,text,(size_t)st.st_size,0))return 0;size_t bytes=(size_t)st.st_size;text[bytes]=0;if(memchr(text,0,bytes)||memchr(text,'\r',bytes))return 0;char *p=text;for(unsigned i=0;i<n;i++){line[i]=p;char *e=strchr(p,'\n');if(!e)return 0;*e=0;p=e+1;}return *p==0;}
static const char *value(const char *line,const char *key){size_t n=strlen(key);return !strncmp(line,key,n)&&line[n]=='='?line+n+1:NULL;}
static int equal_line(const char *line,const char *key,const char *want){const char *v=value(line,key);return v&&!strcmp(v,want);}
static int attestation(int fd,const struct cfg *c,struct attestation *a){
 char text[2048],*line[16];if(!lines_fd(fd,text,sizeof text,line,16)||strcmp(line[0],"NXP49POSTINPUT1")||!equal_line(line[1],"version","0.3.6-demo50")||!equal_line(line[2],"source_srw_sha256",c->source)||!equal_line(line[3],"y_sha256",c->ys)||!equal_line(line[4],"uv_sha256",c->uvs))return 0;
 unsigned width,height,stride;const char *v=value(line[5],"width");if(!v||!number(v,&width))return 0;v=value(line[6],"height");if(!v||!number(v,&height))return 0;v=value(line[7],"stride");if(!v||!number(v,&stride))return 0;v=value(line[8],"nonce");if(!v||!u32value(v,&a->nonce)||!a->nonce)return 0;
 /* v1 production handoff identifies a complete validated active plane, not
  * a padded preview which this helper is allowed to crop by guesswork. */
 if(width!=c->pw||width!=c->w||height!=c->ph||height!=c->h||stride!=c->stride||c->x||c->y0||!equal_line(line[9],"source_wb_complete","1")||!equal_line(line[11],"native_frame_complete","1")||!equal_line(line[12],"source_origin_x","0")||!equal_line(line[13],"source_origin_y","0"))return 0;
 v=value(line[10],"cc_source_reloaded");if(!v||(!strcmp(v,"0")?((a->cc_reloaded=0),0):!strcmp(v,"1")?((a->cc_reloaded=1),0):1))return 0;
 v=value(line[14],"front_result_sha256");if(!v||!hex64(v))return 0;memcpy(a->front,v,65);v=value(line[15],"param_dma_audit_sha256");if(!v||!hex64(v))return 0;memcpy(a->param,v,65);
 const char *slash=strrchr(c->producer,'/');int parent=slash?(int)(slash-c->producer):1;const char *base=slash?c->producer:".";char fp[1200],pp[1200],hash[65];
 if(snprintf(fp,sizeof fp,"%.*s/result.json",parent,base)>=(int)sizeof fp||snprintf(pp,sizeof pp,"%.*s/p49_param_dma_audit.bin",parent,base)>=(int)sizeof pp||!path_hash(fp,hash,NULL)||strcmp(hash,a->front))return 0;
 int front=open_regular(fp,0);if(front<0)return 0;struct stat st;if(fstat(front,&st)||st.st_size<=0||st.st_size>1048576){close(front);return 0;}char *json=malloc((size_t)st.st_size);if(!json){close(front);return 0;}int fj=read_exact(front,json,(size_t)st.st_size,0)&&p49_front_json(json,(size_t)st.st_size,a->nonce,a->cc_reloaded,&(struct p49_front_expected){c->source,c->ys,c->uvs,c->w,c->h,c->stride});free(json);close(front);if(!fj)return 0;
 char cp[1200];if(snprintf(cp,sizeof cp,"%.*s/p48_cc_audit.bin",parent,base)>=(int)sizeof cp)return 0;int cf=open_regular(cp,P48_CC_BYTES);if(cf<0)return 0;uint32_t *cc=malloc(P48_CC_BYTES);if(!cc){close(cf);return 0;}int cv=read_exact(cf,cc,P48_CC_BYTES,0)&&p48_cc_audit_valid(cc,a->nonce)&&cc[3]==a->cc_reloaded;if(cv&&!a->cc_reloaded)cv=p48_cc_same(cc+80,cc+90,10);free(cc);close(cf);if(!cv)return 0;

 int pd=open_regular(pp,P49_PD_BYTES);if(pd<0)return 0;uint32_t rec[64];int ok=fd_hash(pd,hash,NULL)&&!strcmp(hash,a->param)&&read_exact(pd,rec,sizeof rec,0)&&(a->cc_reloaded?p49_pd_audit_valid(rec,a->nonce):p49_pd_factory_audit_valid(rec,a->nonce));close(pd);return ok;
}

static int dir_sync(const char *dir){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;int ok=fsync(fd)==0;if(close(fd))ok=0;return ok;}
static int write_all(int fd,const void *data,size_t n){const unsigned char *p=data;while(n){ssize_t k=write(fd,p,n);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;p+=k;n-=(size_t)k;}return 1;}
static int write_new(const char *p,const void *data,size_t n){int fd=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;int ok=write_all(fd,data,n)&&fsync(fd)==0;if(close(fd))ok=0;return ok;}
static int atomic_text(const char *dir,const char *name,const char *text){
 char temporary[1200],final[1200];if(snprintf(temporary,sizeof temporary,"%s/.%s.%ld.tmp",dir,name,(long)getpid())>=(int)sizeof temporary||snprintf(final,sizeof final,"%s/%s",dir,name)>=(int)sizeof final)return 0;
 if(!write_new(temporary,text,strlen(text)))return 0;int ok=!rename(temporary,final)&&dir_sync(dir);if(!ok)unlink(temporary);return ok;
}
static int post_status(const char *dir,const char *phase,const char *reason,unsigned controller_pid){
 char text[768];int n=snprintf(text,sizeof text,"NX52POSTSTATUS1\nphase=%s\nreason=%s\ncontroller_pid=%u\nLUT_hardware_jobs=1\nCPU_JPEG=0\n",phase,reason,controller_pid);
 return n>0&&(unsigned)n<sizeof text&&atomic_text(dir,"post-status.txt",text);
}
static int post_outcome(const char *dir,unsigned code,unsigned controller_pid){
 char text[512];int n=snprintf(text,sizeof text,"NX52POSTOUTCOME1\nexit=%u\ncontroller_started=%u\ncontroller_pid=%u\nclassification=%s\n",code,controller_pid!=0,controller_pid,code==0?"COMPLETE":controller_pid?"CONTROLLER_RESULT_REQUIRED":"SAFE_NO_SUBMIT");
 return n>0&&(unsigned)n<sizeof text&&atomic_text(dir,"post-outcome.txt",text);
}
/* Byte geometry only. Every Y and interleaved CbCr row is copied intact;
 * only each source row's 16 padding bytes are omitted. No chroma resampling. */
static int import422(int fy,int fu,const struct cfg *c,const char *path){
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;unsigned char row[6480];int ok=c->w<=sizeof row&&c->w&&c->h&&c->stride>=c->w;
 for(unsigned plane=0;ok&&plane<2;plane++)for(unsigned y=0;y<c->h;y++)if(!read_exact(plane?fu:fy,row,c->w,(off_t)y*c->stride)||!write_all(fd,row,c->w)){ok=0;break;}
 if(fsync(fd))ok=0;if(close(fd))ok=0;return ok;
}
struct jpeg_reader {int fd;uint64_t bytes,at,base;unsigned used;unsigned char data[4096];};
static int jpeg_byte(struct jpeg_reader *r,unsigned *v){
 if(r->at>=r->bytes)return 0;if(r->at<r->base||r->at>=r->base+r->used){r->base=r->at;uint64_t n=r->bytes-r->at;if(n>sizeof r->data)n=sizeof r->data;if(!read_exact(r->fd,r->data,(size_t)n,(off_t)r->at))return 0;r->used=(unsigned)n;}*v=r->data[r->at-r->base];r->at++;return 1;
}
static int jpeg_header_stream(const char *path,uint64_t *size){
 int fd=open_regular(path,0);if(fd<0)return 0;struct stat st;int ok=!fstat(fd,&st)&&st.st_size>=32&&(uint64_t)st.st_size<=JPEG_CAP-JPEG_PAD;
 struct jpeg_reader r={.fd=fd,.bytes=ok?(uint64_t)st.st_size:0,.base=UINT64_MAX};unsigned a,b,sof=0,sos=0,dqt=0,dht=0,eoi=0,ids[3]={0};
 if(!ok||!jpeg_byte(&r,&a)||!jpeg_byte(&r,&b)||a!=255||b!=216){close(fd);return 0;}
 while(ok&&r.at<r.bytes){
  unsigned marker,high,low;if(!jpeg_byte(&r,&a)||a!=255){ok=0;break;}do{if(!jpeg_byte(&r,&marker)){ok=0;break;}}while(marker==255);if(!ok)break;
  if(marker==0xd9){eoi=1;ok=r.at==r.bytes&&sof==1&&sos==1&&dqt&&dht;break;}
  if(marker==0||marker==0xd8||(marker>=0xd0&&marker<=0xd7)||!jpeg_byte(&r,&high)||!jpeg_byte(&r,&low)){ok=0;break;}
  unsigned len=(high<<8)|low;if(len<2||len-2>r.bytes-r.at){ok=0;break;}uint64_t end=r.at+len-2;
  if(marker==0xc0){
   unsigned body[15];if(++sof!=1||len!=17){ok=0;break;}for(unsigned i=0;i<15;i++)if(!jpeg_byte(&r,body+i))ok=0;
   if(!ok||body[0]!=8||((body[1]<<8)|body[2])!=4320||((body[3]<<8)|body[4])!=6480||body[5]!=3||body[7]!=0x21||body[10]!=0x11||body[13]!=0x11){ok=0;break;}
   ids[0]=body[6];ids[1]=body[9];ids[2]=body[12];if(ids[0]==ids[1]||ids[0]==ids[2]||ids[1]==ids[2]){ok=0;break;}
  }else if(marker==0xdb)dqt++;
  else if(marker==0xc4)dht++;
  else if(marker==0xda){
   unsigned body[10];if(sof!=1||++sos!=1||len!=12){ok=0;break;}for(unsigned i=0;i<10;i++)if(!jpeg_byte(&r,body+i))ok=0;
   if(!ok||body[0]!=3||body[1]!=ids[0]||body[3]!=ids[1]||body[5]!=ids[2]||body[7]||body[8]!=63||body[9]){ok=0;break;}
   while(r.at<r.bytes){
    if(!jpeg_byte(&r,&a)){ok=0;break;}if(a!=255)continue;
    uint64_t start=r.at-1;do{if(!jpeg_byte(&r,&b)){ok=0;break;}}while(b==255);if(!ok)break;
    if(b==0||(b>=0xd0&&b<=0xd7))continue;r.at=start;break;
   }
   continue;
  }else if(marker>=0xc0&&marker<=0xcf){ok=0;break;}
  r.at=end;
 }
 ok=ok&&eoi&&r.at==r.bytes&&sof==1&&sos==1&&dqt&&dht;
 if(size&&ok)*size=r.bytes;close(fd);return ok;
}
static int exact_file(const char *path,void *out,unsigned n){int fd=open_regular(path,n);if(fd<0)return 0;int ok=read_exact(fd,out,n,0);close(fd);return ok;}
static int load_native_proof(const char *root,const char *input_hash,uint32_t input_crc,struct j422_proof *proof){
 char path[1200];struct bitb_request q;
 if(snprintf(path,sizeof path,"%s/copy_request.bin",root)>=(int)sizeof path||!exact_file(path,&q,sizeof q)||!bitb_request_valid(&q))return 0;
 if(snprintf(path,sizeof path,"%s/j422-proof.bin",root)>=(int)sizeof path||!exact_file(path,proof,sizeof *proof)||!j422_proof_valid(proof,&q)||strcmp(proof->input_sha256,input_hash)||proof->lut_stage.source_crc!=input_crc)return 0;
 struct sw_lease lease;if(snprintf(path,sizeof path,"%s/shared-workspace.bin",root)>=(int)sizeof path||!sw_file_read(path,&lease,sizeof lease)||!sw_valid(&lease)||lease.phase!=SW_FRONT_READY||lease.pa!=q.pa||lease.va!=q.owner_va||strcmp(lease.lut_sha256,proof->lut_table_sha256)||strcmp(lease.prefix_sha256,proof->prefix_sha256))return 0;
 if(snprintf(path,sizeof path,"%s/result.json",root)>=(int)sizeof path)return 0;int fd=open_regular(path,0);if(fd<0)return 0;struct stat st;int ok=!fstat(fd,&st)&&st.st_size>0&&st.st_size<65536;char text[65536];if(ok)ok=read_exact(fd,text,(size_t)st.st_size,0);close(fd);if(!ok)return 0;
 const char *one[]={"ownership_verified","shared_workspace_borrowed","shared_workspace_completed","shared_workspace_released","release_returned","allocator_return_verified","backend_CAP_releases","lut_hardware_jobs","original_code_restored","detached","idle_registers_verified","ep_contract_verified","sync_preflight_verified","copy_worker_ready","copy_go_sent","copy_worker_exited","copy_result_valid","copy_safe_to_release","lut_jpeg_pipeline_verified","copy_rehearsal_verified"};
 for(unsigned i=0;i<sizeof one/sizeof *one;i++)if(!p49_field_uint(text,(size_t)st.st_size,one[i],1))return 0;
 const char *zero[]={"exit","copy_worker_exit","api_inflight_at_exit","copy_memory_quarantined","camera_stopped_at_exit","request_returned","allocation_retained","backend_CAP_requests"};
 for(unsigned i=0;i<sizeof zero/sizeof *zero;i++)if(!p49_field_uint(text,(size_t)st.st_size,zero[i],0))return 0;
 uint64_t artifact_bytes;char artifact_hash[65];
 if(snprintf(path,sizeof path,"%s/srw-lut.bin",root)>=(int)sizeof path||!path_hash(path,artifact_hash,&artifact_bytes)||artifact_bytes!=LUT_SLOT_BYTES||strcmp(artifact_hash,lease.lut_sha256))return 0;
 if(snprintf(path,sizeof path,"%s/workspace-prefix.bin",root)>=(int)sizeof path||!path_hash(path,artifact_hash,&artifact_bytes)||artifact_bytes!=SW_CONTROL||strcmp(artifact_hash,lease.prefix_sha256))return 0;
 struct sw_lease returned;if(snprintf(path,sizeof path,"%s/workspace-released.bin",root)>=(int)sizeof path||!sw_file_read(path,&returned,sizeof returned)||!sw_same_allocation(&returned,&lease)||returned.phase!=SW_RELEASED)return 0;
 return p49_field_text(text,(size_t)st.st_size,"backend","NATIVE_LUT_JPEG_422_SHARED192")&&p49_field_uint(text,(size_t)st.st_size,"engine_jobs",2)&&p49_field_uint(text,(size_t)st.st_size,"camera_pauses",2)&&p49_field_uint(text,(size_t)st.st_size,"camera_resumes",2)&&p49_field_uint(text,(size_t)st.st_size,"camera_resumed_verified",2)&&p49_field_uint(text,(size_t)st.st_size,"pa",q.pa)&&p49_field_uint(text,(size_t)st.st_size,"front_retirement_samples",16);
}
static int native_stream_verified(const char *path,const struct j422_proof *p,char hash[65],uint64_t *bytes){
 uint64_t header_size,n;uint32_t crc;
 return jpeg_header_stream(path,&header_size)&&path_digest(path,hash,&n,&crc)&&n==header_size&&n==p->jpeg.stream_bytes&&crc==p->stage.target_crc&&(*bytes=n,1);
}
static int single_root_jpeg(const char *dir){
 DIR *d=opendir(dir);if(!d)return 0;struct dirent *e;unsigned count=0;int ok=1;
 while((e=readdir(d))){const char *dot=strrchr(e->d_name,'.');if(!dot||(strcasecmp(dot,".jpg")&&strcasecmp(dot,".jpeg")))continue;if(strcmp(e->d_name,"native.jpg")){ok=0;break;}count++;}
 if(closedir(d))ok=0;return ok&&count==1;
}
static int output_contract(const char *json,size_t n){
 struct p49_slice v;int mode=p49_field(json,n,"final_check_mode",&v),delegated=p49_field(json,n,"final_verification_delegated",&v),contract=p49_field(json,n,"successful_return_contract",&v);
 if(!mode&&!delegated&&!contract)return 1;if(!mode||!delegated||!contract)return 0;
 if(p49_field_text(json,n,"final_check_mode","internal"))return p49_field_boolean(json,n,"final_verification_delegated",0)&&p49_field_text(json,n,"successful_return_contract","PUBLISHED_AND_INTERNAL_VERIFICATION");
 return p49_field_text(json,n,"final_check_mode","runner-independent")&&p49_field_boolean(json,n,"final_verification_delegated",1)&&p49_field_text(json,n,"successful_return_contract","PUBLISHED_PENDING_RUNNER_VERIFICATION");
}
static int output_json(const char *json,size_t n,const char *source,const char *input,const char *native_hash,uint64_t native_bytes){
 struct p49_slice geometry,jpeg;
 return output_contract(json,n)&&p49_field_text(json,n,"format","NX52RAW422POST1")&&p49_field_text(json,n,"version",VERSION)&&p49_field_uint(json,n,"exit",0)&&p49_field_text(json,n,"backend","NATIVE_LUT_JPEG_422_SHARED192")&&p49_field_text(json,n,"source_srw_sha256",source)&&p49_field_text(json,n,"input422_sha256",input)&&p49_field_uint(json,n,"sampling",422)&&p49_field_uint(json,n,"native_jobs",2)&&p49_field_uint(json,n,"lut_hardware_jobs",1)&&p49_field_boolean(json,n,"LUT_applied",1)&&p49_field_boolean(json,n,"CPU_JPEG",0)&&p49_field_boolean(json,n,"post_input_semantics_verified",1)&&p49_field_boolean(json,n,"native_proof_verified",1)&&p49_field_boolean(json,n,"source_cc_reloaded",0)&&p49_field_boolean(json,n,"factory_cc",1)&&p49_field_boolean(json,n,"format1_camera_verified_before_this_run",0)&&p49_field_text(json,n,"geometry_conversion","NV16_stride6496_to_NV16_stride6480_padding_only")&&p49_field(json,n,"input_geometry",&geometry)&&p49_field_uint(geometry.p,geometry.n,"output_width",6480)&&p49_field_uint(geometry.p,geometry.n,"output_height",4320)&&p49_field(json,n,"native",&jpeg)&&p49_field_text(jpeg.p,jpeg.n,"file","native.jpg")&&p49_field_text(jpeg.p,jpeg.n,"sha256",native_hash)&&p49_field_uint(jpeg.p,jpeg.n,"bytes",(unsigned)native_bytes);
}
static int verify_output(const char *dir){
 char marker[1200],path[1200],hw[1200],text[2048],*line[8],hash[65],input_hash[65];
 if(snprintf(marker,sizeof marker,"%s/P52_POST_COMPLETE",dir)>=(int)sizeof marker)return 0;int fd=open_regular(marker,0);if(fd<0)return 0;int ok=lines_fd(fd,text,sizeof text,line,8);close(fd);
 if(!ok||strcmp(line[0],"NX52POSTCOMPLETE1")||!equal_line(line[4],"width","6480")||!equal_line(line[5],"height","4320")||!equal_line(line[6],"sampling","422")||!equal_line(line[7],"status","PASS"))return 0;
 const char *rh=value(line[1],"result_sha256"),*nh=value(line[2],"native_sha256"),*source=value(line[3],"source_srw_sha256");if(!hex64(rh)||!hex64(nh)||!hex64(source)||!single_root_jpeg(dir))return 0;
 if(snprintf(hw,sizeof hw,"%s/hardware",dir)>=(int)sizeof hw||snprintf(path,sizeof path,"%s/input422.bin",hw)>=(int)sizeof path)return 0;uint64_t input_bytes,native_bytes;uint32_t input_crc;
 if(!path_digest(path,input_hash,&input_bytes,&input_crc)||input_bytes!=FRAME422_BYTES)return 0;struct j422_proof proof;if(!load_native_proof(hw,input_hash,input_crc,&proof))return 0;
 if(snprintf(path,sizeof path,"%s/native.jpg",dir)>=(int)sizeof path||!native_stream_verified(path,&proof,hash,&native_bytes)||strcmp(hash,nh))return 0;
 if(snprintf(path,sizeof path,"%s/result.json",dir)>=(int)sizeof path||!path_hash(path,hash,NULL)||strcmp(hash,rh))return 0;fd=open_regular(path,0);if(fd<0)return 0;struct stat st;if(fstat(fd,&st)||st.st_size<=0||st.st_size>65536){close(fd);return 0;}char json[65536];ok=read_exact(fd,json,(size_t)st.st_size,0)&&output_json(json,(size_t)st.st_size,source,input_hash,nh,native_bytes);close(fd);
 if(!ok||snprintf(path,sizeof path,"%s/post-status.txt",dir)>=(int)sizeof path)return 0;fd=open_regular(path,0);if(fd<0)return 0;char status[1024],*sl[6];ok=lines_fd(fd,status,sizeof status,sl,6)&&!strcmp(sl[0],"NX52POSTSTATUS1")&&equal_line(sl[1],"phase","COMPLETE")&&equal_line(sl[2],"reason","NONE")&&equal_line(sl[4],"LUT_hardware_jobs","1")&&equal_line(sl[5],"CPU_JPEG","0");close(fd);return ok;
}
static int final_output_check(const char *dir,const struct cfg *c){
 if(final_check_delegated(c)){fprintf(stderr,"NXPOSTDELEGATION1 final_check=runner-independent successful_return=PUBLISHED_PENDING_RUNNER_VERIFICATION external_verify_output_required=1\n");return 1;}
 return verify_output(dir);
}
static int post_report(const char *path,const struct cfg *c,const struct attestation *a,const char *input_hash,const char *native_hash,uint64_t bytes,const struct j422_proof*proof){
 char text[4096];int n=snprintf(text,sizeof text,"{\"format\":\"NX52RAW422POST1\",\"version\":\"%s\",\"exit\":0,\"final_check_mode\":\"%s\",\"final_verification_delegated\":%s,\"successful_return_contract\":\"%s\",\"build_target\":\"ARM_EABI_CAMERA\",\"backend\":\"NATIVE_LUT_JPEG_422_SHARED192\",\"source_srw_sha256\":\"%s\",\"input422_sha256\":\"%s\",\"producer_nonce\":%u,\"sampling\":422,\"native_jobs\":2,\"lut_hardware_jobs\":1,\"LUT_applied\":true,\"CPU_JPEG\":false,\"post_input_semantics_verified\":true,\"native_proof_verified\":true,\"source_cc_reloaded\":false,\"factory_cc\":true,\"format1_camera_verified_before_this_run\":false,\"geometry_conversion\":\"NV16_stride6496_to_NV16_stride6480_padding_only\",\"color_correctness_verified\":false,\"workspace_bytes\":167772160,\"workspace_allocations\":1,\"backend_allocations\":0,\"workspace_release_verified\":true,\"stage_handoff_copy_bytes\":0,\"control_prefix_preserved\":65536,\"lut_table_sha256\":\"%s\",\"input_geometry\":{\"output_width\":6480,\"output_height\":4320},\"native\":{\"file\":\"native.jpg\",\"sha256\":\"%s\",\"bytes\":%llu}}\n",VERSION,final_check_name(c),final_check_delegated(c)?"true":"false",success_contract(c),c->source,input_hash,a->nonce,proof->lut_table_sha256,native_hash,(unsigned long long)bytes);
 return n>0&&(unsigned)n<sizeof text&&write_new(path,text,(size_t)n);
}
static int marker_write(const char *dir,const struct cfg *c,const char *native_hash){
 char path[1200],rh[65],text[1024];if(snprintf(path,sizeof path,"%s/result.json",dir)>=(int)sizeof path||!path_hash(path,rh,NULL))return 0;
 int n=snprintf(text,sizeof text,"NX52POSTCOMPLETE1\nresult_sha256=%s\nnative_sha256=%s\nsource_srw_sha256=%s\nwidth=6480\nheight=4320\nsampling=422\nstatus=PASS\n",rh,native_hash,c->source);
 if(n<=0||(unsigned)n>=sizeof text||snprintf(path,sizeof path,"%s/P52_POST_COMPLETE",dir)>=(int)sizeof path)return 0;
 return write_new(path,text,(size_t)n)&&dir_sync(dir);
}
#include "shared_post_inputs.inc"
#include "workspace_return_verify.inc"
static void usage(void){puts("NXL_RAW422_POST " VERSION "; --version | --clock | --verify-input <source args> | --verify-output DIR | <source args>; --yuv native-nv16; --final-check internal|runner-independent (default internal)");}
int main(int argc,char **argv){
 if(argc==3&&!strcmp(argv[1],"--verify-pipeline-release"))return verify_workspace_return(argv[2],0)?0:2;
 if(argc==3&&!strcmp(argv[1],"--verify-workspace-release"))return verify_workspace_return(argv[2],1)?0:2;
 if(argc==3&&!strcmp(argv[1],"--workspace-owner-nonce")){
  char path[1200];struct sw_lease lease;
  if(snprintf(path,sizeof path,"%s/shared-workspace.bin",argv[2])>=(int)sizeof path||!sw_file_read(path,&lease,sizeof lease)||!sw_valid(&lease)||lease.phase!=SW_FRONT_READY)return 2;
  printf("%u\n",lease.nonce);return 0;
 }
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_RAW422_POST=0.4.0-demo51-422; NATIVE_LUT_JPEG_SHARED192; NV16_NO_CHROMA_AVERAGING; FINAL_CHECK_DELEGATION_R6");return 0;}
 if(argc==2&&!strcmp(argv[1],"--clock")){uint64_t ms;if(!monotonic_ms(&ms))return 1;printf("NXPOSTCLOCK1 monotonic_ms=%" PRIu64 "\n",ms);return 0;}
 if(argc==3&&!strcmp(argv[1],"--verify-output")){timing_begin("VERIFY_OUTPUT");int ok=verify_output(argv[2]);timing_end(ok?"PASS":"FAIL");return ok?0:2;}
 int verify_only=argc>1&&!strcmp(argv[1],"--verify-input");if(verify_only){argc--;argv++;}
 timing_begin("ARGS");struct cfg c;if(!parse(argc,argv,&c)){usage();fprintf(stderr,"RAW422_POST_ERROR phase=ARGS reason=INVALID_ARGUMENTS no_submit=1\n");timing_end("FAIL");return 42;}timing_end("PASS");
 signal(SIGINT,onsignal);signal(SIGTERM,onsignal);signal(SIGHUP,onsignal);
 int fy=-1,fu=-1,fp=-1,success=0,created=0;unsigned controller_pid=0;const char *phase="VERIFY_INPUT",*error="INPUT";char hash[65],input_hash[65],native_hash[65],path[1200],hw[1100];uint32_t input_crc=0;uint64_t native_bytes=0;struct attestation a;
#define FAIL(REASON) do{error=REASON;goto done;}while(0)
#define PHASE(NAME) do{timing_end("PASS");phase=(NAME);timing_begin(phase);}while(0)
 timing_begin(phase);
 if(!verify_only){if(canceled||mkdir(c.out,0700))FAIL("OUTPUT_NOT_NEW");created=1;if(!post_status(c.out,phase,"NONE",0))FAIL("STATUS_COMMIT");}
 fy=open_regular(c.y,(uint64_t)c.stride*c.h);fu=open_regular(c.uv,(uint64_t)c.stride*c.h);fp=open_regular(c.producer,0);if(fy<0||fu<0||fp<0)FAIL("INPUT_SIZE_OR_KIND");
 if(!fd_hash(fy,hash,NULL)||strcmp(hash,c.ys)||!fd_hash(fu,hash,NULL)||strcmp(hash,c.uvs)||!fd_hash(fp,hash,NULL)||strcmp(hash,c.ps)||!attestation(fp,&c,&a)||a.cc_reloaded)FAIL("SOURCE_OR_NATIVE_PROOF");
 if(verify_only){success=1;goto done;}
 PHASE("IMPORT_NV16");if(!post_status(c.out,phase,"NONE",0))FAIL("STATUS_COMMIT");struct statvfs disk;if(statvfs(c.out,&disk)||(uint64_t)disk.f_bavail*disk.f_frsize<768u*1024u*1024u)FAIL("SD_NEEDS_768M_FREE");
 if(snprintf(hw,sizeof hw,"%s/hardware",c.out)>=(int)sizeof hw||mkdir(hw,0700))FAIL("HARDWARE_DIR");if(snprintf(path,sizeof path,"%s/input422.bin",hw)>=(int)sizeof path||!import422(fy,fu,&c,path))FAIL("NV16_IMPORT");
 PHASE("VERIFY_IMPORTED_NV16");uint64_t input_bytes;if(!path_digest(path,input_hash,&input_bytes,&input_crc)||input_bytes!=FRAME422_BYTES)FAIL("NV16_IMPORT_IDENTITY");
 PHASE("SOURCE_RECHECK");if(!fd_hash(fy,hash,NULL)||strcmp(hash,c.ys)||!fd_hash(fu,hash,NULL)||strcmp(hash,c.uvs))FAIL("INPUT_CHANGED");
 PHASE("VERIFY_LIVE_SHARED_WORKSPACE");if(!post_shared_inputs(hw,&c,&a))FAIL("SHARED_WORKSPACE_OWNER_OR_PROOF_INVALID");
 PHASE("INPUT_IDENTITY_AND_START_MARKER");if(snprintf(path,sizeof path,"%s/input.identity",hw)>=(int)sizeof path)FAIL("INPUT_IDENTITY_PATH");char identity[256];int n=snprintf(identity,sizeof identity,"NX422INPUT1 55987200 %s\n",input_hash);if(n<=0||(unsigned)n>=sizeof identity||!write_new(path,identity,(size_t)n)||!dir_sync(hw))FAIL("INPUT_IDENTITY");
 if(canceled)FAIL("CANCELED_BEFORE_NATIVE_START");if(snprintf(path,sizeof path,"%s/P50_HARDWARE_STARTED",c.out)>=(int)sizeof path||!write_new(path,"ONE_NATIVE_ATTEMPT\n",19)||!dir_sync(c.out))FAIL("START_MARKER");
 PHASE("START_NATIVE_CONTROLLER");if(!post_status(c.out,phase,"NONE",0))FAIL("STATUS_COMMIT");if(canceled)FAIL("CANCELED_BEFORE_NATIVE_START");
 pid_t child=fork();if(child<0)FAIL("FORK");if(!child){execl("/opt/usr/nx-lut-lite/bin/nxl-hw-controller.arm","nxl-hw-controller.arm",hw,(char*)0);_exit(127);}
 controller_pid=(unsigned)child;PHASE("WAIT_NATIVE_CONTROLLER");int wait_status_saved=post_status(c.out,phase,"NONE",controller_pid);
 int status=0;pid_t got;do{got=waitpid(child,&status,0);}while(got<0&&errno==EINTR);if(got!=child||!WIFEXITED(status)||WEXITSTATUS(status))FAIL("NATIVE_CONTROLLER_FAILED");
 if(!wait_status_saved)FAIL("WAIT_STATUS_COMMIT");if(canceled)FAIL("CANCELED_AFTER_NATIVE_CONTROLLER");PHASE("VERIFY_NATIVE_PROOF");struct j422_proof proof;if(!load_native_proof(hw,input_hash,input_crc,&proof))FAIL("NATIVE_PROOF_NOT_VERIFIED");
 PHASE("PUBLISH_NATIVE_JPEG");if(!post_status(c.out,phase,"NONE",controller_pid))FAIL("STATUS_COMMIT");char from[1200],to[1200];if(snprintf(from,sizeof from,"%s/native-422.jpg",hw)>=(int)sizeof from||snprintf(to,sizeof to,"%s/native.jpg",c.out)>=(int)sizeof to||!native_stream_verified(from,&proof,native_hash,&native_bytes)||rename(from,to)||!dir_sync(c.out))FAIL("NATIVE_JPEG_PUBLISH");
 PHASE("COMMIT_REPORTS");if(snprintf(path,sizeof path,"%s/result.json",c.out)>=(int)sizeof path||!post_report(path,&c,&a,input_hash,native_hash,native_bytes,&proof)||!marker_write(c.out,&c,native_hash))FAIL("COMMIT");
 PHASE("FINAL_OUTPUT_CHECK");if(!post_status(c.out,"COMPLETE","NONE",controller_pid)||!final_output_check(c.out,&c))FAIL("FINAL_COMMIT_VERIFICATION");timing_end(final_check_delegated(&c)?"DELEGATED":"PASS");success=1;
 done:timing_end(success?"PASS":"FAIL");if(fy>=0)close(fy);if(fu>=0)close(fu);if(fp>=0)close(fp);unsigned code=success?0:controller_pid?1:42;
 if(created){timing_begin("COMMIT_OUTCOME");if(!success)post_status(c.out,"FAILED",error,controller_pid);if(!post_outcome(c.out,code,controller_pid)&&success){success=0;code=controller_pid?1:42;post_status(c.out,"FAILED","OUTCOME_COMMIT",controller_pid);}timing_end(success?"PASS":"FAIL");}
 if(!success)fprintf(stderr,"RAW422_POST_ERROR phase=%s reason=%s no_submit=%u errno=%d\n",phase,error,controller_pid==0,errno);else puts(verify_only?"RAW422_POST_INPUT_VERIFIED":final_check_delegated(&c)?"RAW422_POST_PUBLISHED_PENDING_RUNNER_VERIFY":"RAW422_POST_COMPLETE");
 return (int)code;
#undef PHASE
#undef FAIL
}
