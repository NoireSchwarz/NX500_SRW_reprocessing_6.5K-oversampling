/* SPDX-License-Identifier: AGPL-3.0-only. One owner, no intermediate images.
 * 42/43/45: no owned native work; 44: failed image, verified pool return.
 * Unknown ownership/DMA state is never converted to a releasable state. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include "sha256.h"
#include "../../common/progress_emit.h"
#include "../../post/src/post_json.h"
#include "../../common/shared_workspace_io.h"
#include "../../hardware/src/lut_cpu.h"
#include "../../common/isp_compare.h"
int nxl_front_entry(int,char**);
#ifdef NXL_PIPELINE_TEST
static int nxl_front_resource_target(const char*d){(void)d;return 1;}
static int nxl_front_resource_released(void){return 1;}
static int nxl_front_resource_prepared(void){return 1;}
#else
int nxl_front_resource_target(const char*);
int nxl_front_resource_released(void);
int nxl_front_resource_prepared(void);
#endif
int nxl_post_entry(int,char**);
static int pipeline_path(const char*s){
 const char*p="/mnt/mmc/NXDEMO51/JOB_";
 if(strncmp(s,p,strlen(p))||strlen(s)>1000||strstr(s,"//"))return 0;
 for(;*s;s++)if(!((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'||*s=='/'))return 0;
 return 1;
}
static int pipeline_sync(const char*dir){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 0;int ok=!fsync(fd);if(close(fd))ok=0;return ok;}
static int pipeline_read_small(const char*path,char*data,size_t cap,size_t*bytes){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;if(fd<0)return 0;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&(uint64_t)st.st_size<cap;size_t at=0;
 while(ok&&at<(size_t)st.st_size){ssize_t n=read(fd,data+at,(size_t)st.st_size-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(close(fd))ok=0;if(!ok)return 0;data[at]=0;*bytes=at;return 1;
}
static int pipeline_copy_small(const char*from,const char*to){
 char b[65537];size_t n;return pipeline_read_small(from,b,sizeof b,&n)&&sw_file_new(to,b,n);
}
/* Six fixed configuration records exceed the generic 64KiB text limit.
 * Keep their exact ABI length; never broaden the text/JSON copy bound. */
static int pipeline_copy_isp(const char *from,const char *to){
 const unsigned expected=(unsigned)sizeof(struct ic_sample)*IC_STAGES;int a=open(from,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(a<0)return 0;
 struct stat st;if(fstat(a,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)expected){close(a);return 0;}
 int b=open(to,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(b<0){close(a);return 0;}unsigned at=0;unsigned char buf[4096];int ok=1;
 while(ok&&at<expected){unsigned n=expected-at;if(n>sizeof buf)n=sizeof buf;ssize_t got=read(a,buf,n);if(got<0&&errno==EINTR)continue;if(got<=0){ok=0;break;}unsigned sent=0;
  while(sent<(unsigned)got){ssize_t written=write(b,buf+sent,(unsigned)got-sent);if(written<0&&errno==EINTR)continue;if(written<=0){ok=0;break;}sent+=(unsigned)written;}at+=(unsigned)got;
 }
 if(at!=expected||fsync(b))ok=0;if(close(a))ok=0;if(close(b))ok=0;return ok;
}
static int pipeline_copy_tail(const char*from,const char*to){
 int fd=open(from,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;if(fd<0)return 0;
 char b[32768];int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0;
 size_t n=ok&&st.st_size<(off_t)sizeof b?(size_t)st.st_size:sizeof b;
 if(ok)ok=pread(fd,b,n,st.st_size-(off_t)n)==(ssize_t)n;if(close(fd))ok=0;
 return ok&&sw_file_new(to,b,n);
}
/* Failure-only allocator records have one exact firmware-defined size.
 * Stream them in bounded chunks; never raise the generic small-file limit. */
static int pipeline_copy_allocator(const char*from,const char*to){
 const unsigned expected=45056u*16u;int source=open(from,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(source<0)return 0;
 struct stat st;if(fstat(source,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)expected){close(source);return 0;}
 int dest=open(to,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(dest<0){close(source);return 0;}
 unsigned copied=0;unsigned char bytes[8192];int ok=1;
 while(ok&&copied<expected){unsigned want=expected-copied;if(want>sizeof bytes)want=sizeof bytes;ssize_t n=read(source,bytes,want);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}
  unsigned at=0;while(at<(unsigned)n){ssize_t written=write(dest,bytes+at,(unsigned)n-at);if(written<0&&errno==EINTR)continue;if(written<=0){ok=0;break;}at+=(unsigned)written;}copied+=(unsigned)n;
 }
 if(!ok||copied!=expected||fsync(dest))ok=0;if(close(dest))ok=0;if(close(source))ok=0;return ok;
}
#include "../../common/jpeg_ticket.h"
#include "../../common/album_output.h"
static int pipeline_hash(const char*path,char out[65],unsigned*bytes){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st={0};if(fd<0)return 0;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=32&&(uint64_t)st.st_size<SW_BYTES;
 struct sha256 h;sha256_init(&h);unsigned char b[65536],d[32];ssize_t n=0;unsigned used=0;
 while(ok&&(n=read(fd,b,sizeof b))>0){if(!used&&(n<2||b[0]!=0xff||b[1]!=0xd8))ok=0;sha256_update(&h,b,(size_t)n);used+=(unsigned)n;}
 if(n<0||used!=(uint64_t)st.st_size)ok=0;if(close(fd))ok=0;if(!ok)return 0;
 sha256_final(&h,d);digest_hex(d,out);*bytes=used;return 1;
}
static int pipeline_hex(const char*s){for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return !s[64];}
static int pipeline_json_text(const char*t,size_t n,const char*k,char*out,size_t cap){
 struct p49_slice v;if(!p49_field(t,n,k,&v)||v.n<2||v.n>cap+1||v.p[0]!='"'||v.p[v.n-1]!='"')return 0;
 memcpy(out,v.p+1,v.n-2);out[v.n-2]=0;return 1;
}
#ifdef NXL_PIPELINE_TEST
int pipeline_test_controller(const char*,const char*,unsigned);
static int pipeline_controller(const char*dir,const char*out,unsigned abort_only){return pipeline_test_controller(dir,out,abort_only);}
#else
static int pipeline_controller(const char*dir,const char*out,unsigned abort_only){
 pid_t p=fork();if(p<0)return -1;if(!p){
  if(abort_only)execl("/opt/usr/nx-lut-lite/bin/nxl-hw-controller.arm","nxl-hw-controller.arm","--release-shared",dir,(char*)0);
  else execl("/opt/usr/nx-lut-lite/bin/nxl-hw-controller.arm","nxl-hw-controller.arm","--fast-pipeline",dir,out,(char*)0);
  _exit(127);
 }
 int status;pid_t got;do{got=waitpid(p,&status,0);}while(got<0&&errno==EINTR);
 return got==p&&WIFEXITED(status)?WEXITSTATUS(status):-1;
}
#endif
static int pipeline_release_verified(const char*dir,unsigned abort_only){
 char*args[]={"embedded-post",abort_only?"--verify-workspace-release":"--verify-pipeline-release",(char*)dir,NULL};return !nxl_post_entry(3,args);
}
static int pipeline_abort_return(const char*front){
 char path[1200],dir[1200],out[1200];struct sw_lease lease;
 nxl_live_progress("PIPE_ABORT_CHECK","RUNNING","Checking whether the failed front has a verified releasable lease");
 snprintf(path,sizeof path,"%s/shared-workspace.bin",front);
 if(!sw_file_read(path,&lease,sizeof lease)||!sw_valid(&lease)||lease.phase!=SW_FRONT_READY||lease.owner_pid!=(uint32_t)getpid())return 0;
 snprintf(dir,sizeof dir,"%s/abort-return",front);if(mkdir(dir,0700))return 0;
 snprintf(out,sizeof out,"%s/shared-workspace.bin",dir);if(!sw_file_new(out,&lease,sizeof lease))return 0;
 if(pipeline_controller(dir,NULL,1)!=0||!pipeline_release_verified(dir,1))return 0;
 if(!nxl_front_resource_released())fprintf(stderr,"NXPIPELINE1 resource_return_ledger_save_failed=1 physically_verified=1\n");
 if(!sw_owner_proof_retire(&lease))fprintf(stderr,"NXPIPELINE1 owner_proof_retirement_incomplete=1 workspace_return_verified=1\n");
 return 1;
}
static int pipeline_native_image_ok(const char*dir){
 char path[1200],t[16384];size_t n;snprintf(path,sizeof path,"%s/result.json",dir);if(!pipeline_read_small(path,t,sizeof t,&n))return 0;
 const char*one[]={"copy_go_sent","copy_worker_exited","copy_result_valid","copy_safe_to_release","lut_jpeg_pipeline_verified","shared_workspace_completed","lut_hardware_jobs","ldc_hardware_jobs"};
 for(unsigned i=0;i<sizeof one/sizeof *one;i++)if(!p49_field_uint(t,n,one[i],1))return 0;
 return p49_field_uint(t,n,"copy_worker_exit",0)&&p49_field_uint(t,n,"copy_memory_quarantined",0)&&p49_field_uint(t,n,"engine_jobs",3)&&
  p49_field_uint(t,n,"requested_bytes",SW_BYTES)&&p49_field_text(t,n,"backend","NATIVE_LDC_LUT_JPEG_422_SHARED192");
}
static int pipeline_native_ok(const char*dir){
 char path[1200],t[16384];size_t n;snprintf(path,sizeof path,"%s/result.json",dir);
 return pipeline_native_image_ok(dir)&&pipeline_read_small(path,t,sizeof t,&n)&&p49_field_uint(t,n,"exit",0);
}
static const char*pipeline_backend_failure(const char*dir){
 char path[1200],t[16384];size_t n;snprintf(path,sizeof path,"%s/result.json",dir);
 if(pipeline_read_small(path,t,sizeof t,&n)&&p49_field_uint(t,n,"copy_result_valid",1)){
  const char*codes[]={"LDC_CONFIGURATION_READBACK_FAILED","LDC_WORKER_REPORTED_FAILURE","LUT_WORKER_REPORTED_FAILURE","JPEG_WORKER_REPORTED_FAILURE"};
  const char*reasons[]={"LDC_CONFIGURATION_READBACK_FAILED_WORKSPACE_RETURNED","LDC_FAILED_WORKSPACE_RETURNED","LUT_FAILED_WORKSPACE_RETURNED","JPEG_FAILED_WORKSPACE_RETURNED"};
  for(unsigned i=0;i<4;i++)if(p49_field_text(t,n,"copy_error",codes[i]))return reasons[i];
 }
 return "LUT_JPEG_FAILED_WORKSPACE_RETURNED";
}
static int pipeline_ldc_timeout(const char*dir){
 /* Wire12 BJ_WAIT=13. Classification only; the existing return verifier and
  * EP lifetime branch remain the sole resource-release authority. */
 char path[1200],t[16384];size_t n;snprintf(path,sizeof path,"%s/result.json",dir);
 return pipeline_read_small(path,t,sizeof t,&n)&&p49_field_uint(t,n,"copy_go_sent",1)&&p49_field_uint(t,n,"copy_result_valid",1)&&
  p49_field_uint(t,n,"pipeline_failed_stage",1)&&p49_field_uint(t,n,"pipeline_worker_error",13)&&p49_field_text(t,n,"copy_error","LDC_COMPLETION_TIMEOUT");
}
static int pipeline_ldc_drain(const char*dir){
 char p[1200],text[16384];size_t n;snprintf(p,sizeof p,"%s/result.json",dir);
 return pipeline_read_small(p,text,sizeof text,&n)&&p49_field_uint(text,n,"copy_go_sent",1)&&p49_field_uint(text,n,"copy_result_valid",1)&&p49_field_uint(text,n,"copy_safe_to_release",0)&&p49_field_uint(text,n,"pipeline_failed_stage",1)&&p49_field_uint(text,n,"pipeline_worker_error",23)&&p49_field_uint(text,n,"pipeline_fresh_completions",1)&&p49_field_text(text,n,"copy_error","LDC_DMA_STOP_ACK_UNVERIFIED");
}
/* Image publication is independent of allocator retirement. A complete native
 * proof and matching read-back digest are required; release failure still
 * returns a failure exit and keeps every hardware/resource hold. */
static int pipeline_publish_image(const char*hw,const char*output,const struct sw_lease*owner,char hash[65],unsigned*bytes){
 char path[1200],dst[1200],want[65],have[65];struct stat st;unsigned size=0;
 if(!pipeline_native_image_ok(hw))return 0;
 snprintf(path,sizeof path,"%s/final-jpeg.sha256",hw);if(!sw_file_read(path,want,64))return 0;want[64]=0;if(!pipeline_hex(want))return 0;
 const char*album_pending=getenv("NX51_FINAL_PENDING");if(album_pending){if(!ao_target(album_pending,1))return 0;snprintf(path,sizeof path,"%s",album_pending);}else snprintf(path,sizeof path,"%s/native.jpg.pending",output);
 if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<32||(uint64_t)st.st_size>SW_TABLE-SW_JPEG_OUT-16384u)return 0;
 struct stat original=st;struct jt_ticket ticket;char ticket_path[1200];snprintf(ticket_path,sizeof ticket_path,"%s/final-jpeg-ticket.bin",hw);
 const char*strict=getenv("NX51_FULL_IMAGE_AUDIT");int reused=(!strict||strcmp(strict,"1"))&&jt_read(ticket_path,&ticket)&&jt_matches(&ticket,&original,owner->nonce,owner->pa,owner->owner_pid,want,owner->source_sha256);
 if(reused){strcpy(have,want);size=(unsigned)original.st_size;}
 else if(!pipeline_hash(path,have,&size)||strcmp(have,want))return 0;
 const char*album_final=getenv("NX51_FINAL_JPEG");if(album_pending){if(!album_final||!ao_target(album_final,0))return 0;snprintf(dst,sizeof dst,"%s",album_final);}else snprintf(dst,sizeof dst,"%s/native.jpg",output);errno=0;if(!lstat(dst,&st)||errno!=ENOENT)return 0;
 char sync_dir[1200];strcpy(sync_dir,output);if(album_pending&&!ao_directory(dst,sync_dir,sizeof sync_dir))return 0;
 if((album_pending?!ao_commit(path,dst):rename(path,dst))||!pipeline_sync(sync_dir)||lstat(dst,&st)||!S_ISREG(st.st_mode)||st.st_dev!=original.st_dev||st.st_ino!=original.st_ino||st.st_size!=original.st_size||st.st_mtim.tv_sec!=original.st_mtim.tv_sec||st.st_mtim.tv_nsec!=original.st_mtim.tv_nsec)return 0;
 snprintf(ticket_path,sizeof ticket_path,"%s/jpeg-published-ticket.bin",output);
 if(!jt_write(ticket_path,&st,owner->nonce,owner->pa,owner->owner_pid,have,owner->source_sha256))fprintf(stderr,"JPEG_PUBLICATION_TICKET unavailable=1 full_hash_fallback_available=1\n");
 fprintf(stderr,"JPEG_PUBLICATION readback_reused=%u bytes=%u\n",reused?1u:0u,size);snprintf(ticket_path,sizeof ticket_path,"%s/output-path.txt",output);char saved_path[1300];int length=snprintf(saved_path,sizeof saved_path,"%s\n",dst);if(length<1||length>=(int)sizeof saved_path||!sw_file_new(ticket_path,saved_path,(size_t)length))return 0;strcpy(hash,have);*bytes=size;return 1;
}
/* Only this process's private RAM directory is removed; SD reports stay. */
static int pipeline_tmp_clear(const char*dir,unsigned depth){
 const char*p="/tmp/nx-lut-lite/pipeline-";if(strncmp(dir,p,strlen(p))||depth>4)return 0;
 DIR*d=opendir(dir);if(!d)return errno==ENOENT;int ok=1;struct dirent*e;
 while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char path[1400];struct stat st;
  if(snprintf(path,sizeof path,"%s/%s",dir,e->d_name)>=(int)sizeof path||lstat(path,&st)){ok=0;continue;}
  if(S_ISDIR(st.st_mode)){if(!pipeline_tmp_clear(path,depth+1))ok=0;}else if(unlink(path))ok=0;
 }
 if(closedir(d))ok=0;if(rmdir(dir))ok=0;return ok;
}
#ifdef NXL_COLD_INIT_TEST
#define PIPELINE_MANUAL_INIT "false"
#define PIPELINE_RELEASE "R9n-fullwidth-power-auto"
#else
#define PIPELINE_MANUAL_INIT "true"
#define PIPELINE_RELEASE "R9n-fullwidth-power"
#endif
static int pipeline_report(const char*output,int rc,const char*reason,int fr,int backend,int released,const struct sw_lease*s,const char*hash,unsigned bytes){
 char path[1200],text[4000],saved[1200];const char*album=getenv("NX51_FINAL_JPEG");if(album&&ao_target(album,0))snprintf(saved,sizeof saved,"%s",album);else snprintf(saved,sizeof saved,"%s/native.jpg",output);snprintf(path,sizeof path,"%s/result.json",output);
 int n=snprintf(text,sizeof text,"{\"format\":\"NX_SINGLE_PIPELINE_FAST1\",\"version\":\"" PIPELINE_RELEASE "\",\"exit\":%d,\"reason\":\"%s\",\"owner_pid\":%u,\"front_exit\":%d,\"backend_exit\":%d,\"workspace_bytes\":%u,\"pa\":%u,\"va\":%u,\"nonce\":%u,\"workspace_return_verified\":%s,\"native_jobs\":%u,\"width\":6480,\"height\":4320,\"sampling\":422,\"jpeg_saved\":%s,\"jpeg_bytes\":%u,\"jpeg_sha256\":\"%s\",\"jpeg_path\":\"%s\",\"lut_sha256\":\"%s\",\"source_srw_sha256\":\"%s\",\"middle_full_frame_files\":0,\"bounded_pixel_sample_points\":25,\"bounded_pixel_tile_side\":16,\"stage_handoff_copy_bytes\":0,\"full_frame_audit\":false,\"pixel_correctness_verified\":false,\"manual_boot_Still_JPEG_initialization\":" PIPELINE_MANUAL_INIT "}\n",rc,reason,(unsigned)getpid(),fr,backend,SW_BYTES,s->pa,s->va,s->nonce,released?"true":"false",rc==0?4u:0u,bytes?"true":"false",bytes,hash,saved,s->lut_sha256,s->source_sha256);
 return n>0&&(unsigned)n<sizeof text&&sw_file_new(path,text,(size_t)n)&&pipeline_sync(output);
}
static int pipeline_verify(const char*output,unsigned fast){
 const char*audit=getenv("NX51_FULL_IMAGE_AUDIT");if(audit&&!strcmp(audit,"1"))fast=0;
 char path[1200],text[8192],hash[65],want[65],hw[1200];size_t n;unsigned bytes=0;
 if(!pipeline_path(output))return 0;snprintf(path,sizeof path,"%s/result.json",output);if(!pipeline_read_small(path,text,sizeof text,&n))return 0;
 if(!p49_field_text(text,n,"format","NX_SINGLE_PIPELINE_FAST1")||!p49_field_uint(text,n,"exit",0)||
  !p49_field_uint(text,n,"workspace_bytes",SW_BYTES)||!p49_field_boolean(text,n,"workspace_return_verified",1)||
  !p49_field_uint(text,n,"native_jobs",4)||!p49_field_uint(text,n,"width",6480)||!p49_field_uint(text,n,"height",4320)||
  !p49_field_uint(text,n,"sampling",422)||!p49_field_uint(text,n,"middle_full_frame_files",0)||
  !p49_field_uint(text,n,"stage_handoff_copy_bytes",0)||!p49_field_boolean(text,n,"full_frame_audit",0)||
  !pipeline_json_text(text,n,"jpeg_sha256",want,sizeof want)||!pipeline_hex(want))return 0;
 snprintf(hw,sizeof hw,"%s/hardware",output);if(!pipeline_release_verified(hw,0)||!pipeline_native_ok(hw))return 0;
 struct sw_lease s;snprintf(path,sizeof path,"%s/shared-workspace.bin",hw);if(!sw_file_read(path,&s,sizeof s)||
  !p49_field_uint(text,n,"pa",s.pa)||!p49_field_uint(text,n,"va",s.va)||!p49_field_uint(text,n,"nonce",s.nonce)||
  !p49_field_text(text,n,"lut_sha256",s.lut_sha256)||!p49_field_text(text,n,"source_srw_sha256",s.source_sha256))return 0;
 char saved[1200];if(pipeline_json_text(text,n,"jpeg_path",saved,sizeof saved)){if(!ao_target(saved,0)){char legacy[1200];snprintf(legacy,sizeof legacy,"%s/native.jpg",output);if(strcmp(saved,legacy))return 0;}snprintf(path,sizeof path,"%s",saved);}else snprintf(path,sizeof path,"%s/native.jpg",output);
 if(fast){struct stat st;struct jt_ticket ticket;char tp[1200];snprintf(tp,sizeof tp,"%s/jpeg-published-ticket.bin",output);if(!lstat(path,&st)&&jt_read(tp,&ticket)&&jt_matches(&ticket,&st,s.nonce,s.pa,s.owner_pid,want,s.source_sha256)&&p49_field_uint(text,n,"jpeg_bytes",(unsigned)st.st_size)){fprintf(stderr,"JPEG_FINAL_VERIFY metadata_ticket=1 full_readback_already_verified=1\n");return 1;}}
 return pipeline_hash(path,hash,&bytes)&&!strcmp(hash,want)&&p49_field_uint(text,n,"jpeg_bytes",bytes);
}
#ifdef NXL_COLD_INIT_TEST
#include "pipeline_init_trial.inc"
#endif
int main(int argc,char**argv){
 if((argc==3&&!strcmp(argv[1],"--source-sha"))||(argc==4&&!strcmp(argv[1],"--source-ticket")))return nxl_front_entry(argc,argv);
 if(argc==2&&!strcmp(argv[1],"--version")){
#ifdef NXL_COLD_INIT_TEST
  puts("NXL_DEMO_FRONT=0.3.6-demo50; R8E_COLD_CONFIGURATION_TRIAL; SHARED192; ORIGINAL_CONFIG_BEFORE_START; OWNED_EP_REFERENCE; RELEASE_R8I_BLACK_PROFILE_A028; RELEASE_R9N_FULLWIDTH_POWER");
#else
  puts("NXL_DEMO_FRONT=0.3.6-demo50; R8A_STAGE_CHANNEL_ONE_PROCESS_SRW_LUT_JPEG; SHARED192_BANK2; FAST_RAM_HANDOFF; MANUAL_BOOT_INIT; RELEASE_R8I_BLACK_PROFILE_A028; RELEASE_R9N_FULLWIDTH_POWER");
#endif
  return 0;
 }
 if(argc==3&&!strcmp(argv[1],"--validate-lut")){const char*p="/tmp/nx-lut-lite/pipeline-lut-";unsigned char table[LUT_SLOT_BYTES];return strncmp(argv[2],p,strlen(p))||strstr(argv[2],"..")||!sw_file_read(argv[2],table,sizeof table)||!lut_table_valid(table,sizeof table)?2:0;}
 if(argc==3&&(!strcmp(argv[1],"--verify-output")||!strcmp(argv[1],"--verify-output-fast")))return pipeline_verify(argv[2],!strcmp(argv[1],"--verify-output-fast"))?0:2;
 if((argc!=4&&argc!=5)||strcmp(argv[1],"--pipeline")||!pipeline_path(argv[2])||!pipeline_path(argv[3]))return 2;
 const char*frontsd=argv[2],*output=argv[3];char root[120],front[150],hw[150],path[1400],dst[1400],hash[65]="";
 int rc=45,fr=-1,backend=-1,released=0,ldc_timeout=0,ldc_drain=0;unsigned jpeg_bytes=0;const char*reason="PIPELINE_SETUP_FAILED";struct sw_lease lease={0};struct timespec ts;
#ifdef NXL_COLD_INIT_TEST
 unsigned trial_acquired=0;
#endif
 /* Cover even setup/space failures: a later successful retry must not leave
  * an unexplained attempted round. No CAP call precedes this durable intent. */
 nxl_live_progress("PIPE_PREPARE","RUNNING","Preparing the single shared workspace");
 if(!nxl_front_resource_target(frontsd)||!nxl_front_resource_prepared())return 45;
 if(clock_gettime(CLOCK_MONOTONIC,&ts))return 45;snprintf(root,sizeof root,"/tmp/nx-lut-lite/pipeline-%u-%lu",(unsigned)getpid(),(unsigned long)ts.tv_nsec);
 snprintf(front,sizeof front,"%s/front",root);snprintf(hw,sizeof hw,"%s/hardware",root);
 if(mkdir(output,0700)||mkdir(root,0700)||mkdir(front,0700)||mkdir(hw,0700))goto done;
 const char*album_source=getenv("NX51_SOURCE_SRW"),*album_mode=getenv("NX51_ALBUM_OUTPUT");if(album_source&&(!album_mode||strcmp(album_mode,"0"))&&!ao_prepare(album_source)){reason="SOURCE_ALBUM_OUTPUT_UNAVAILABLE";goto done;}
 struct statvfs space;if(statvfs(output,&space))goto done;uint64_t block=space.f_frsize?space.f_frsize:space.f_bsize;
 if(!block||(uint64_t)space.f_bavail<(134217728ULL+block-1)/block){reason="FINAL_JPEG_SPACE_BELOW_128MIB";goto done;}
 if(argc==5&&*argv[4]){const char*p="/tmp/nx-lut-lite/pipeline-lut-";if(strncmp(argv[4],p,strlen(p))||strstr(argv[4],".."))goto done;snprintf(path,sizeof path,"%s/srw-lut.bin",front);if(!pipeline_copy_small(argv[4],path))goto done;}
 char*fa[]={"embedded-front",front,"--pipeline-return",NULL};
#ifdef NXL_COLD_INIT_TEST
 if(trial_helper("--acquire")){rc=trial_hold_absent()?45:1;reason="AUTO_EP_ACQUIRE_FAILED";goto done;}
 trial_acquired=1;
#endif
 fprintf(stderr,"NXPIPELINE1 phase=SRW event=BEGIN owner_pid=%u middle_pixel_files=0\n",(unsigned)getpid());rc=1;nxl_live_progress("PIPE_SRW","RUNNING","Reading SRW and preparing native ISP");fr=nxl_front_entry(3,fa);
 snprintf(path,sizeof path,"%s/result.json",front);snprintf(dst,sizeof dst,"%s/result.json",frontsd);(void)pipeline_copy_small(path,dst);
 if(fr){reason="FRONT_FAILED";if(fr==46){rc=46;reason="FRONT_CODE_GUARD_REJECTED_NO_SUBMIT";}else if(fr==45){rc=45;reason="SOURCE_NATIVE_SETTINGS_UNAVAILABLE_NO_SUBMIT";}else if(fr==42||fr==43){rc=fr;reason=fr==42?"BOOT_INITIALIZATION_REQUIRED":"BANK2_CAPACITY_UNAVAILABLE";}else if(pipeline_abort_return(front)){released=1;rc=44;}goto done;}
 snprintf(path,sizeof path,"%s/shared-workspace.bin",front);
 if(!sw_file_read(path,&lease,sizeof lease)||!sw_valid(&lease)||lease.phase!=SW_FRONT_READY||lease.owner_pid!=(uint32_t)getpid()){reason="FRONT_LEASE_INVALID";goto done;}
 snprintf(dst,sizeof dst,"%s/shared-workspace.bin",frontsd);if(!sw_file_new(dst,&lease,sizeof lease))goto abort_before_backend;
 snprintf(dst,sizeof dst,"%s/shared-workspace.bin",hw);if(!sw_file_new(dst,&lease,sizeof lease))goto abort_before_backend;
 snprintf(path,sizeof path,"%s/srw-lut.bin",front);snprintf(dst,sizeof dst,"%s/srw-lut.bin",hw);if(!pipeline_copy_small(path,dst))goto abort_before_backend;
 const char*settings[]={"native-jpeg-config.bin","native-ldc-config.bin"};
 for(unsigned i=0;i<2;i++){snprintf(path,sizeof path,"%s/%s",front,settings[i]);snprintf(dst,sizeof dst,"%s/%s",hw,settings[i]);if(!pipeline_copy_small(path,dst))goto abort_before_backend;}
 nxl_live_progress("PIPE_FRONT_DONE","RUNNING","Still output and shared workspace lease verified");
 fprintf(stderr,"NXPIPELINE1 phase=LUT_JPEG event=BEGIN owner_pid=%u handoff_copy_bytes=0\n",(unsigned)getpid());backend=pipeline_controller(hw,output,0);
 released=pipeline_release_verified(hw,0);
 ldc_timeout=pipeline_ldc_timeout(hw);ldc_drain=pipeline_ldc_drain(hw);
 int image_saved=pipeline_publish_image(hw,output,&lease,hash,&jpeg_bytes);
 if(released&&!nxl_front_resource_released()){rc=44;reason="RESOURCE_RETURN_SAVE_FAILED";goto done;}
 if(!released){reason=ldc_drain?"LDC_DMA_STOP_ACK_UNVERIFIED_WORKSPACE_HELD":ldc_timeout?"LDC_COMPLETION_TIMEOUT_WORKSPACE_HELD":"NATIVE_RETURN_UNVERIFIED";if(image_saved)nxl_live_progress("PIPE_IMAGE_HELD","HELD","JPEG saved as native.jpg; workspace return unverified; resource check required");goto done;}rc=44;
 snprintf(dst,sizeof dst,"%s/hardware",output);if(mkdir(dst,0700)){reason="RETURN_RECEIPT_DIRECTORY_FAILED";goto done;}
 const char*files[]={"result.json","controller-outcome.txt","shared-workspace.bin","workspace-released.bin"};
 for(unsigned i=0;i<sizeof files/sizeof *files;i++){snprintf(path,sizeof path,"%s/%s",hw,files[i]);snprintf(dst,sizeof dst,"%s/hardware/%s",output,files[i]);if(!pipeline_copy_small(path,dst)){reason="RETURN_RECEIPT_SAVE_FAILED";goto done;}}
 if(backend||!pipeline_native_ok(hw)){reason=pipeline_backend_failure(hw);goto done;}
 if(!image_saved){reason="JPEG_READBACK_OR_COMMIT_FAILED";goto done;}
 nxl_live_progress("PIPE_SAVED","RUNNING","Final JPEG saved; checking camera restoration");
 rc=0;reason="SRW_LUT_JPEG_SAVED";goto done;
abort_before_backend:
 reason="BACKEND_SETUP_FAILED";if(pipeline_abort_return(front)){released=1;rc=44;}
done:
#ifdef NXL_COLD_INIT_TEST
 if(trial_acquired){
  if(((rc==0||rc==44)&&released) || rc==42||rc==43||rc==45||rc==46){
   nxl_live_progress("AUTO_EP_RELEASE","RUNNING","Returning the test EP reference after verified workspace retirement");
   if(trial_helper("--release")||!trial_hold_absent()){rc=1;reason="AUTO_EP_RETURN_UNCONFIRMED";}
  }else{rc=1;reason=ldc_drain?"LDC_DMA_STOP_ACK_UNVERIFIED_WORKSPACE_AND_EP_HELD":ldc_timeout?"LDC_COMPLETION_TIMEOUT_WORKSPACE_AND_EP_HELD":"AUTO_NATIVE_STATE_UNKNOWN_EP_REFERENCE_HELD";}
 }
#endif
 if(released&&sw_valid(&lease)&&!sw_owner_proof_retire(&lease))fprintf(stderr,"NXPIPELINE1 owner_proof_retirement_incomplete=1 workspace_return_verified=1\n");
 if(rc){const char*from[]={"front/probe.log","hardware/probe.log","hardware/result.json","hardware/front-retirement-failed.bin","hardware/controller-outcome.txt"};const char*to[]={"failure-front.log","failure-backend.log","failure-backend.json","failure-retirement.bin","failure-backend-outcome.txt"};
  for(unsigned i=0;i<sizeof from/sizeof *from;i++){snprintf(path,sizeof path,"%s/%s",root,from[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,to[i]);(void)pipeline_copy_small(path,dst);}
  snprintf(path,sizeof path,"%s/trace_events.txt",hw);snprintf(dst,sizeof dst,"%s/failure-backend-trace.log",frontsd);(void)pipeline_copy_tail(path,dst);}
 /* Small control-plane evidence survives a power cycle; no image planes or
  * full workspace snapshots are written to the card. */
 const char *compare_from[]={"selected-source.json","selected-c0.bin","case_00/isp-config-stages.bin","case_00/isp-config-schema.json","native-recorder-link.json","source-tone-curves.json"};
 const char *compare_to[]={"selected-source.json","selected-c0.bin","isp-config-stages.bin","isp-config-schema.json","native-recorder-link.json","source-tone-curves.json"};
 unsigned compare_saved=0;
 for(unsigned i=0;i<sizeof compare_from/sizeof *compare_from;i++){snprintf(path,sizeof path,"%s/%s",front,compare_from[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,compare_to[i]);int copied=i==2?pipeline_copy_isp(path,dst):pipeline_copy_small(path,dst);if(copied)compare_saved|=1u<<i;}
 if(!rc&&getenv("NX51_SOURCE_SRW")&&compare_saved!=63u){rc=44;reason="COMPARE_EVIDENCE_SAVE_FAILED_WORKSPACE_RETURNED";}
 const char*settings_evidence[]={"native-jpeg-config.bin","native-ldc-config.bin","source-settings.json","front-timing.json","source-settings-rejected.json"};
 unsigned settings_saved=0;
 for(unsigned i=0;i<5;i++){snprintf(path,sizeof path,"%s/%s",front,settings_evidence[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,settings_evidence[i]);if(pipeline_copy_small(path,dst))settings_saved|=1u<<i;}
 const char*pixel_evidence[]={"pipeline-isp-settings.json","bayer-out-samples.bin","ldc-out-samples.bin","lut-out-samples.bin","native-ldc-state.bin","j422-proof.bin","ldc-config-audit.json","ldc-hardware-audit.json","ldc-progress-audit.json","p11_kernel_before.bin","p11_kernel_after.bin","p11_rtos_irq_before.bin","p11_rtos_irq_after.bin"};
 unsigned pixel_saved=0;
 for(unsigned i=0;i<sizeof pixel_evidence/sizeof *pixel_evidence;i++){snprintf(path,sizeof path,"%s/%s",hw,pixel_evidence[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,pixel_evidence[i]);if(pipeline_copy_small(path,dst))pixel_saved|=1u<<i;}
 if(!rc&&((settings_saved&15u)!=15u||pixel_saved!=((1u<<(sizeof pixel_evidence/sizeof *pixel_evidence))-1u))){rc=44;reason="NATIVE_SETTINGS_OR_PIXEL_EVIDENCE_SAVE_FAILED_WORKSPACE_RETURNED";}
 const char*front_evidence[]={"case_00/result.json","case_00/front_readiness_first.json","case_00/front_readiness_final.json","case_00/pipeline-jpeg-preflight.json","case_00/front_handoff_postframe_first.json","case_00/front_handoff_postframe_final.json","front_handoff_export_first.json","front_handoff_export_final.json","case_00/source-cc-audit.bin","case_00/source-wb-audit.bin","case_00/gamma-test-audit.bin"};
 const char*front_names[]={"front-frame-result.json","front-readiness-first.json","front-readiness-final.json","front-jpeg-preflight.json","front-handoff-postframe-first.json","front-handoff-postframe-final.json","front-handoff-export-first.json","front-handoff-export-final.json","source-cc-audit.bin","source-wb-audit.bin","gamma-test-audit.bin"};
 unsigned iq_saved=0;for(unsigned i=0;i<sizeof front_evidence/sizeof *front_evidence;i++){snprintf(path,sizeof path,"%s/%s",front,front_evidence[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,front_names[i]);int saved=pipeline_copy_small(path,dst);if(i>=8&&saved)iq_saved|=1u<<(i-8);}
 snprintf(path,sizeof path,"%s/case_00/p39_rtos_code_guards.txt",front);snprintf(dst,sizeof dst,"%s/front-rtos-code-guards.txt",frontsd);(void)pipeline_copy_small(path,dst);
 snprintf(path,sizeof path,"%s/case_00/frozen-config-audit.bin",front);snprintf(dst,sizeof dst,"%s/frozen-config-audit.bin",frontsd);(void)pipeline_copy_small(path,dst);
 if(!rc&&getenv("NX51_SOURCE_SRW")&&iq_saved!=7u){rc=44;reason="SOURCE_IQ_EVIDENCE_SAVE_FAILED_WORKSPACE_RETURNED";}
 const char*cq_front[]={"BEFORE_SRW","AFTER_STILL"},*cq_backend[]={"BEFORE_EP","AFTER_EP","AFTER_RELEASE_CALL","AFTER_RELEASE_CHECK","AFTER_RELEASE"};
 for(unsigned i=0;i<sizeof cq_front/sizeof *cq_front;i++){snprintf(path,sizeof path,"%s/case_00/diag-cmdq-%s.bin",front,cq_front[i]);snprintf(dst,sizeof dst,"%s/diag-cmdq-%s.bin",frontsd,cq_front[i]);(void)pipeline_copy_small(path,dst);}
 for(unsigned i=0;i<sizeof cq_backend/sizeof *cq_backend;i++){snprintf(path,sizeof path,"%s/diag-cmdq-%s.bin",hw,cq_backend[i]);snprintf(dst,sizeof dst,"%s/diag-cmdq-%s.bin",frontsd,cq_backend[i]);(void)pipeline_copy_small(path,dst);}
 const char*return_json[]={"allocator-return-first.json","allocator-return-final.json","allocator-cache-fence.json"};
 for(unsigned i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",hw,return_json[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,return_json[i]);(void)pipeline_copy_small(path,dst);}
 snprintf(path,sizeof path,"%s/allocator-return.csv",hw);snprintf(dst,sizeof dst,"%s/allocator-return.csv",frontsd);(void)pipeline_copy_tail(path,dst);
 if(rc){const char*tables[]={"allocator-return-first-a.bin","allocator-return-first-b.bin","allocator-return-last-a.bin","allocator-return-last-b.bin"};
  for(unsigned i=0;i<4;i++){snprintf(path,sizeof path,"%s/%s",hw,tables[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,tables[i]);(void)pipeline_copy_allocator(path,dst);}(void)pipeline_sync(frontsd);}
 if(rc){const char*control[]={"copy_peer_audit.txt","copy_worker.bin"};
  for(unsigned i=0;i<2;i++){snprintf(path,sizeof path,"%s/%s",hw,control[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,control[i]);(void)pipeline_copy_small(path,dst);}
  const char*logs[]={"p21a_preflight.txt","p11_native.txt","r6-worker-timing.log"};
  for(unsigned i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",hw,logs[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,logs[i]);(void)pipeline_copy_tail(path,dst);}}
 if(rc){snprintf(path,sizeof path,"%s/trace_events.txt",front);snprintf(dst,sizeof dst,"%s/failure-front-trace.log",frontsd);(void)pipeline_copy_tail(path,dst);
  const char*control[]={"failed-native-result.bin","failed-native-stop.bin","failed-native-initial-stop.bin","failed-native-batch.bin","failed-native-idle.bin"};
  for(unsigned i=0;i<sizeof control/sizeof *control;i++){snprintf(path,sizeof path,"%s/case_00/%s",front,control[i]);snprintf(dst,sizeof dst,"%s/%s",frontsd,control[i]);(void)pipeline_copy_small(path,dst);}}
 snprintf(dst,sizeof dst,"%s/stage-progress.bin",output);(void)pipeline_copy_tail("/tmp/nx-lut-lite/PROGRESS.live",dst);
 snprintf(path,sizeof path,"%s/owner-proof.bin",front);snprintf(dst,sizeof dst,"%s/owner-proof.bin",frontsd);(void)pipeline_copy_small(path,dst);
 if(!pipeline_report(output,rc,reason,fr,backend,released,&lease,hash,jpeg_bytes)&&rc==0)rc=released?44:1;
 if(rc){char status[128];int bytes=snprintf(status,sizeof status,"%s\n",reason);snprintf(path,sizeof path,"%s/STATUS.reason",output);if(bytes>0&&(unsigned)bytes<sizeof status)(void)sw_file_new(path,status,(unsigned)bytes);
#ifdef NXL_COLD_INIT_TEST
  trial_failure_status(reason);
#endif
 }
 if(rc==0||rc==42||rc==43||rc==44||rc==45||rc==46)(void)pipeline_tmp_clear(root,0);
 fprintf(stderr,"NXPIPELINE1 phase=TERMINAL exit=%d reason=%s workspace_return_verified=%d\n",rc,reason,released);return rc;
}
