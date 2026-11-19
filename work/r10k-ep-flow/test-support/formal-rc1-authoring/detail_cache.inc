static int detail_params_valid(const int*);
static int detail_cache_path(char path[256]){
 const char*id=detail_name(detail.cursor);if(!name_ok(id))return 0;
 const char*root="/tmp/nx-lut-lite/detail-edits-rc1";struct stat st;
 if(mkdir(root,0700)&&errno!=EEXIST)return 0;
 if(srw_lstat(root,&st)||!S_ISDIR(st.st_mode))return 0;
 return snprintf(path,256,"%s/%s_%s",root,detail_log_scope?"log":"std",id)<256;
}
static int detail_cache_save(void){
 char path[256],text[400];if(!detail_cache_path(path))return 0;
 const char*b=detail_committed_b[0]?detail_committed_b:detail_name(detail.cursor);
 int pb[4];memcpy(pb,detail.prepared?detail_committed_params:detail.b,sizeof pb);if(detail_log_scope)memset(pb,0,sizeof pb);
 int n=snprintf(text,sizeof text,"NXL_DETAIL_EDIT_RC1 A=%s %d %d %d %d B=%s %d %d %d %d prepared=%d\n",detail_name(detail.cursor),detail.a[0],detail.a[1],detail.a[2],detail.a[3],b,pb[0],pb[1],pb[2],pb[3],detail_log_scope?0:detail.prepared);
 if(n<=0||n>=(int)sizeof text)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;struct stat st;
 int ok=!srw_fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&!ftruncate(fd,0)&&write(fd,text,(size_t)n)==n&&!fsync(fd);if(close(fd))ok=0;return ok;
}
static int detail_load_parameters(void){
 char path[256],line[400],a[81],b[81],extra;int pa[4],pb[4],prepared;
 if(!detail_cache_path(path))return 0;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 if(srw_fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<=0||st.st_size>=400){close(fd);return 0;}
 ssize_t n=read(fd,line,sizeof line-1);close(fd);if(n<=0)return 0;line[n]=0;
 if(sscanf(line,"NXL_DETAIL_EDIT_RC1 A=%80s %d %d %d %d B=%80s %d %d %d %d prepared=%d\n%c",a,pa,pa+1,pa+2,pa+3,b,pb,pb+1,pb+2,pb+3,&prepared,&extra)!=11||strcmp(a,detail_name(detail.cursor))||!name_ok(b)||!detail_params_valid(pa)||!detail_params_valid(pb)||(prepared!=0&&prepared!=1))return 0;
 if(detail_log_scope&&(prepared||strcmp(a,b)||pb[0]||pb[1]||pb[2]||pb[3]))return 0;
 int found=0;for(int i=0;i<total_count;i++)if(!strcmp(names[i],b)&&((!strcmp(info_for(b)->group,"loglut"))==detail_log_scope))found=1;
 if(!found)return 0;
 memcpy(detail.a,pa,sizeof pa);memcpy(detail.b,pb,sizeof pb);memcpy(detail_committed_params,pb,sizeof pb);strcpy(detail_committed_b,b);detail.prepared=prepared;
 strcpy(detail_message,"已載入暫存參數，按OK套用");return 1;
}
static void detail_switch_a(int next){
 if(next==detail.cursor)return;
 (void)detail_cache_save();detail.cursor=ld_wrap(next,detail.count);
 memset(detail.a,0,sizeof detail.a);memset(detail.b,0,sizeof detail.b);detail.a[0]=detail_log_scope?100:effective_strength();detail.b[0]=100;
 detail.prepared=0;detail_committed_b[0]=0;
 if(!detail_load_parameters()){detail_message[0]=0;detail_queue(0);}
 else detail_dirty=0;
}
