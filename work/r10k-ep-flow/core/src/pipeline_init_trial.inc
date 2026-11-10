/* SPDX-License-Identifier: AGPL-3.0-only. Experimental entry only.
 * Keep the EP reference through all three stages. Release only after the
 * ordinary workspace proofs, or an ordinary pre-allocation exit. */
#ifdef NXL_PIPELINE_TEST
int pipeline_test_init_helper(const char*);
static int trial_helper(const char*mode){return pipeline_test_init_helper(mode);}
#else
static int trial_helper(const char*mode){
 const char*root=getenv("NX51_INIT_REPORT");if(!root||strlen(root)>1000)return 77;
 pid_t p=fork();if(p<0)return 1;if(!p){execl("/opt/usr/nx-lut-lite/bin/nxl-owned-ep.arm","nxl-owned-ep.arm",mode,root,(char*)0);_exit(127);}
 int s;pid_t got;do{got=waitpid(p,&s,0);}while(got<0&&errno==EINTR);
 return got==p&&WIFEXITED(s)?WEXITSTATUS(s):1;
}
#endif
static int trial_hold_absent(void){struct stat s;errno=0;return lstat("/tmp/nx-lut-lite/auto-init-held",&s)<0&&errno==ENOENT;}
static void trial_failure_status(const char*reason){
 const char*root=getenv("NX51_INIT_REPORT"),*token=getenv("NX51_PROGRESS_TOKEN"),*prefix="/mnt/mmc/NXDEMO51/INIT_AUTO_";
 if(!root||!np_operation(token)||strncmp(root,prefix,strlen(prefix))||strcmp(root+strlen(prefix),token))return;
 size_t n=strlen(reason);if(!n||n>100)return;for(unsigned i=0;i<n;i++)if(!((reason[i]>='A'&&reason[i]<='Z')||(reason[i]>='0'&&reason[i]<='9')||reason[i]=='_'))return;
 int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return;
 int fd=openat(dir,"STATUS.reason",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd>=0){char text[102];memcpy(text,reason,n);text[n++]='\n';if(write(fd,text,n)==(ssize_t)n)(void)fsync(fd);close(fd);(void)fsync(dir);}close(dir);
}
