/* SPDX-License-Identifier: AGPL-3.0-only. Manual boot preparation stays manual.
 * Read the original JPEG context before requesting the shared CAP. */
static unsigned pipeline_jpeg_missing,pipeline_jpeg_checked;
static int pipeline_jpeg_preflight(const char*dir){
 pipeline_jpeg_missing=0;pipeline_jpeg_checked=0;
 uint32_t count=0,fd=0,selector=0,error=0;char target[256]={0},path[1200];
 if(!fingerprint("/usr/lib/libudd5.so",ALLOC_UDD_BYTES,ALLOC_UDD_FNV)||!read_word(udd_base+0x5708cu,&count)||
    !read_word(udd_base+0x55574u,&fd)||!ro_words(0x867555c4u,&selector,1)||!ro_words(0x867554c4u,&error,1))return 0;
 int sma=0;if((int32_t)fd>=0){snprintf(path,sizeof path,"/proc/%d/fd/%d",pid,(int32_t)fd);ssize_t n=readlink(path,target,sizeof target-1);if(n>0){target[n]=0;sma=!strcmp(target,"/dev/d5_sma");}}
 pipeline_jpeg_checked=1;pipeline_jpeg_missing=!count||(int32_t)fd<0||selector==0;
 int ready=count&&sma&&(selector==3||selector==5)&&!error;
 snprintf(path,sizeof path,"%s/pipeline-jpeg-preflight.json",dir);FILE*f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"format\":\"NXPIPELINE_BOOT_READY1\",\"read_only\":true,\"checked_before_CAP_request\":true,\"open_count\":%u,\"sma_fd\":%d,\"sma_target_verified\":%d,\"selector\":%u,\"error\":%u,\"ready\":%s,\"manual_boot_initialization_required\":%s}\n",count,(int32_t)fd,sma,selector,error,ready?"true":"false",pipeline_jpeg_missing?"true":"false");
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok&&ready;
}
static int front_return_unallocated_grant(void){
 if(!p33_guard_acquired)return 1;if(p33_guard_fd<0)return 0;
 char text[4097],prefix[1400];size_t n=0;int len=snprintf(prefix,sizeof prefix,P33_STARTED_STATE "pid=%ld\nreport=%s\n",(long)getpid(),batch_directory);
 if(len<=0||len>=(int)sizeof prefix||!p33_grant_read(p33_guard_fd,text,&n)||n<(size_t)len||memcmp(prefix,text,(size_t)len))return 0;
 int ok=!ftruncate(p33_guard_fd,0)&&lseek(p33_guard_fd,0,SEEK_SET)==0&&
  write(p33_guard_fd,P33_READY_STATE,sizeof P33_READY_STATE-1)==sizeof P33_READY_STATE-1&&!fsync(p33_guard_fd);
 if(ok){p33_guard_close();p33_guard_acquired=0;}return ok;
}
