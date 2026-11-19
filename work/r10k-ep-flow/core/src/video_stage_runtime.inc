/* R10z Linux endpoints: proc files, readlink and read-only RAM mappings.
 * Never open an original codec/EP driver, trace a thread, or write pixels. */
struct vz_map_scan {int fd;struct vz_maps map;unsigned at,used,line_bytes,pthread_seen,epoch;char chunk[4096],line[1200];};
struct vz_runtime {
 struct runtime*r;int syscall_fd;uint32_t tid,pthread_base,pthread_layout_verified;uint64_t tid_ticks;
 struct mapped_page pixel_pages[64];unsigned pixel_count,pixel_eviction;
 struct vz_map_scan scan;
};
static int vz_pthread_identity(void){
 int f=open("/lib/libpthread-2.13.so",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;
 struct stat st;int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size==84028;
 struct sha256 h;sha256_init(&h);unsigned char buf[4096],digest[32];ssize_t n;unsigned bytes=0;
 while(ok&&(n=read(f,buf,sizeof buf))>0){bytes+=(unsigned)n;sha256_update(&h,buf,(size_t)n);}
 if(ok&&n<0)ok=0;if(close(f))ok=0;sha256_final(&h,digest);char hex[65];digest_hex(digest,hex);
 return ok&&bytes==84028u&&!strcmp(hex,"83aaaa614344f4d28047358eb7ce49665439bd7149bd17ad9fc1846a309abe9e");
}
static uint32_t vz_runtime_pthread_base(void*opaque){struct vz_runtime*x=opaque;return x->pthread_layout_verified?x->pthread_base:0;}
static uint64_t vz_tid_ticks(struct vz_runtime*x,uint32_t tid){
 char path[100],buf[2048];snprintf(path,sizeof path,"/proc/%d/task/%u/stat",x->r->pid,tid);
 if(!read_text(path,buf,sizeof buf))return 0;char*q=strrchr(buf,')');if(!q||q[1]!=' ')return 0;q+=2;
 for(unsigned i=3;i<22;i++){q=strchr(q,' ');if(!q)return 0;while(*q==' ')q++;}return strtoull(q,NULL,10);
}
static int vz_runtime_maps(void*opaque,struct vz_maps*m){
 struct vz_runtime*x=opaque;char path[100],line[1200];memset(m,0,sizeof*m);m->full_hash=2166136261u;
 if(ticks(x->r->pid)!=x->r->start)return 0;
 snprintf(path,sizeof path,"/proc/%d/maps",x->r->pid);FILE*f=fopen(path,"r");if(!f)return 0;
 unsigned lines=0,pthread_seen=0;uint32_t pthread_base=0;int ok=1;
 while(fgets(line,sizeof line,f)){
  if(++lines>512u||!strchr(line,'\n')){ok=0;break;}
  m->full_hash=bv_hash_more(m->full_hash,line,(unsigned)strlen(line));
  unsigned lo,hi;unsigned long long off;char mode[8],name[512];
  if(sscanf(line,"%x-%x %7s %llx %*s %*u %511[^\n]",&lo,&hi,mode,&off,name)!=5)continue;
  if(!strcmp(name,"/lib/libpthread-2.13.so")&&!off&&mode[0]=='r'&&strchr(mode,'x')){pthread_seen++;pthread_base=lo;}
  if(strcmp(name,"/dev/mem")||off<0x94000000ULL||off>=0xc0000000ULL)continue;
  if(m->count>=VZ_MAPS||mode[0]!='r'||mode[3]!='s'||hi<=lo||off+hi-lo>0xc0000000ULL){ok=0;break;}
  m->map[m->count++]=(struct vz_mapping){lo,hi,(uint32_t)off,1};
 }
 if(ferror(f))ok=0;if(fclose(f))ok=0;if(ticks(x->r->pid)!=x->r->start)ok=0;
 m->status=ok?1u:2u;x->pthread_base=ok&&pthread_seen==1u?pthread_base:0;
 m->pthread_layout_verified=x->pthread_layout_verified;m->pthread_base=vz_runtime_pthread_base(x);return ok;
}
static int vz_map_done(struct vz_runtime*x,unsigned failure,unsigned error,struct vz_maps*out){
 struct vz_map_scan*s=&x->scan;
 if(s->fd>=0&&close(s->fd)&&!failure){failure=7;error=(unsigned)errno;}s->fd=-1;
 s->map.status=failure?2u:1u;s->map.failure_stage=failure;s->map.read_errno=error;s->map.scan_end_ns=clock_ns(CLOCK_MONOTONIC);
 s->map.pthread_layout_verified=x->pthread_layout_verified;s->map.pthread_seen=s->pthread_seen;
 if(failure||s->pthread_seen!=1u)s->map.pthread_base=0;
 x->pthread_base=s->map.pthread_base;*out=s->map;return failure?-1:1;
}
static int vz_runtime_maps_step(void*opaque,int command,struct vz_maps*out){
 struct vz_runtime*x=opaque;struct vz_map_scan*s=&x->scan;
 if(command<0){if(s->fd>=0)close(s->fd);s->fd=-1;s->map.status=2;s->map.failure_stage=10;s->map.scan_end_ns=clock_ns(CLOCK_MONOTONIC);*out=s->map;return -1;}
 if(command>0){
  if(s->fd>=0)close(s->fd);unsigned epoch=s->epoch+1u;memset(s,0,sizeof*s);s->fd=-1;s->epoch=epoch;
  s->map.epoch=epoch;s->map.full_hash=2166136261u;s->map.scan_begin_ns=clock_ns(CLOCK_MONOTONIC);
  if(ticks(x->r->pid)!=x->r->start)return vz_map_done(x,1,0,out);
  char path[100];snprintf(path,sizeof path,"/proc/%d/maps",x->r->pid);s->fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(s->fd<0)return vz_map_done(x,2,(unsigned)errno,out);
 }
 if(s->fd<0){*out=s->map;return s->map.status==1u?1:-1;}
 uint64_t begun=clock_ns(CLOCK_MONOTONIC),cpu=clock_ns(CLOCK_PROCESS_CPUTIME_ID);unsigned reads=0,parsed=0,worked=0;
 if(begun-s->map.scan_begin_ns>15000000000ULL)return vz_map_done(x,8,0,out);
 /* Clock syscalls per byte would dominate on this kernel. Check at most
  * every256 bytes; one read,32 lines and4096 processed bytes are hard caps. */
 while(parsed<32u&&worked<4096u){
  if(!(worked&255u)&&(clock_ns(CLOCK_PROCESS_CPUTIME_ID)-cpu>=800000u||clock_ns(CLOCK_MONOTONIC)-begun>=3000000u))break;
  if(s->at==s->used){
   if(reads++)break;ssize_t n=read(s->fd,s->chunk,sizeof s->chunk);
   if(n<0)return vz_map_done(x,3,(unsigned)errno,out);
   if(!n){
    if(s->line_bytes)return vz_map_done(x,4,0,out);
    if(ticks(x->r->pid)!=x->r->start)return vz_map_done(x,1,0,out);
    return vz_map_done(x,0,0,out);
   }
   s->at=0;s->used=(unsigned)n;s->map.bytes+=(unsigned)n;
   if(s->map.bytes>262144u)return vz_map_done(x,5,0,out);
  }
  char ch=s->chunk[s->at++];worked++;s->map.full_hash=(s->map.full_hash^(unsigned char)ch)*16777619u;
  if(ch!='\n'){if(s->line_bytes+1u>=sizeof s->line)return vz_map_done(x,4,0,out);s->line[s->line_bytes++]=ch;continue;}
  s->line[s->line_bytes]=0;s->line_bytes=0;parsed++;s->map.lines++;
  if(s->map.lines>4096u)return vz_map_done(x,5,0,out);
  unsigned lo,hi;unsigned long long off;char mode[8],name[512];
  if(sscanf(s->line,"%x-%x %7s %llx %*s %*u %511[^\n]",&lo,&hi,mode,&off,name)!=5)continue;
  if(!strcmp(name,"/lib/libpthread-2.13.so")&&!off&&mode[0]=='r'&&strchr(mode,'x')){s->pthread_seen++;s->map.pthread_base=lo;}
  if(strcmp(name,"/dev/mem")||off<0x94000000ULL||off>=0xc0000000ULL)continue;
  if(s->map.count>=VZ_MAPS||mode[0]!='r'||mode[3]!='s'||hi<=lo||off+hi-lo>0xc0000000ULL)return vz_map_done(x,6,0,out);
  s->map.map[s->map.count++]=(struct vz_mapping){lo,hi,(uint32_t)off,1};
 }
 s->map.pthread_seen=s->pthread_seen;*out=s->map;return 0;
}
static int vz_runtime_list(void*opaque,uint32_t*out,unsigned cap,unsigned*count){
 struct vz_runtime*x=opaque;char path[100];snprintf(path,sizeof path,"/proc/%d/task",x->r->pid);
 *count=0;DIR*d=opendir(path);if(!d)return 0;struct dirent*e;unsigned visited=0;
 while((e=readdir(d))){
  if(++visited>1024u){*count=cap+1u;break;}
  char*end;long tid=strtol(e->d_name,&end,10);if(*end||tid<=1||tid>2147483647)continue;
  if(*count<cap)out[*count]=(uint32_t)tid;(*count)++;
 }
 int ok=closedir(d)==0;
 unsigned n=*count<cap?*count:cap;
 for(unsigned i=1;i<n;i++){uint32_t v=out[i];unsigned j=i;while(j&&out[j-1]>v){out[j]=out[j-1];j--;}out[j]=v;}
 return ok;
}
static int vz_runtime_select(void*opaque,uint32_t tid,uint64_t start){
 struct vz_runtime*x=opaque;
 if(!tid){if(x->syscall_fd>=0)close(x->syscall_fd);x->syscall_fd=-1;x->tid=0;x->tid_ticks=0;return 1;}
 if(tid<=1u||!start||vz_tid_ticks(x,tid)!=start)return 0;
 if(x->tid==tid&&x->tid_ticks==start&&x->syscall_fd>=0)return 1;
 char path[100];snprintf(path,sizeof path,"/proc/%d/task/%u/syscall",x->r->pid,tid);
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;
 if(x->syscall_fd>=0)close(x->syscall_fd);x->syscall_fd=f;x->tid=tid;x->tid_ticks=start;return 1;
}
static int vz_parse_syscall(const char*buf,struct vz_task*t){
 if(!strcmp(buf,"running\n")||!strcmp(buf,"running")){t->status=VZ_RUNNING;return 1;}
 long nr=0;unsigned arg[8]={0};char tail=0;
 int n=sscanf(buf,"%ld %x %x %x %x %x %x %x %x %c",&nr,arg,arg+1,arg+2,arg+3,arg+4,arg+5,arg+6,arg+7,&tail);
 if(n==3&&nr==-1){t->nr=-1;t->sp=arg[0];t->pc=arg[1];t->status=VZ_USER;return 1;}
 if(n!=9||nr<0||nr>=1024)return 0;
 t->nr=(int32_t)nr;memcpy(t->arg,arg,24);t->sp=arg[6];t->pc=arg[7];t->status=VZ_SYS;return 1;
}
static int vz_runtime_task(void*opaque,uint32_t tid,struct vz_task*t){
 struct vz_runtime*x=opaque;memset(t,0,sizeof*t);if(tid<=1u){t->failure_stage=1;return 0;}t->tid=tid;
 int cached=x->tid==tid&&x->syscall_fd>=0;int f=cached?x->syscall_fd:-1;
 t->start_ticks=cached?x->tid_ticks:vz_tid_ticks(x,tid);if(!t->start_ticks){t->failure_stage=1;t->read_errno=(uint32_t)errno;return 0;}
 if(!cached){char path[100];snprintf(path,sizeof path,"/proc/%d/task/%u/syscall",x->r->pid,tid);f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0){t->failure_stage=2;t->read_errno=(uint32_t)errno;return 0;}}
 char buf[256];ssize_t n=pread(f,buf,sizeof buf-1,0);int ok=n>0&&n<(ssize_t)sizeof buf-1;
 if(!ok){t->failure_stage=3;t->read_errno=n<0?(uint32_t)errno:0;}
 if(!cached&&close(f)){ok=0;t->failure_stage=3;t->read_errno=(uint32_t)errno;}
 if(!ok)return 0;buf[n]=0;if(!vz_parse_syscall(buf,t)){t->failure_stage=4;return 0;}return 1;
}
static int vz_runtime_fd(void*opaque,unsigned fd){
 struct vz_runtime*x=opaque;if(fd>=4096u)return 0;char path[100],name[160];snprintf(path,sizeof path,"/proc/%d/fd/%u",x->r->pid,fd);
 ssize_t n=readlink(path,name,sizeof name-1);if(n<=0||n>=(ssize_t)sizeof name-1)return 0;name[n]=0;
 return !strcmp(name,"/dev/d5_hevc")?1:!strcmp(name,"/dev/d5_mptop")?2:0;
}
static int vz_runtime_pixel(void*opaque,uint32_t y,uint32_t uv,uint32_t a,void*out,unsigned n){
 struct vz_runtime*x=opaque;if(!vz_pixel_allowed(y,uv,a,n))return 0;
 uint32_t page=a&~4095u;unsigned i;
 for(i=0;i<x->pixel_count;i++)if(x->pixel_pages[i].memory&&x->pixel_pages[i].address==page)break;
 if(i==x->pixel_count){
  int fresh=i<64u;if(!fresh){i=x->pixel_eviction++%64u;if(x->pixel_pages[i].memory&&munmap((void*)x->pixel_pages[i].memory,4096))return 0;x->pixel_pages[i].memory=NULL;}
  void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,x->r->dev,(off_t)(uint64_t)page);if(p==MAP_FAILED)return 0;
  x->pixel_pages[i]=(struct mapped_page){page,p};if(fresh)x->pixel_count++;
 }
 if((a&4095u)+n>4096u)return 0;
 return lens_ro_load(x->pixel_pages[i].memory+(a&4095u),out,n);
}
static void vz_runtime_close(struct vz_runtime*x){
 if(x->scan.fd>=0)close(x->scan.fd);x->scan.fd=-1;
 if(x->syscall_fd>=0)close(x->syscall_fd);x->syscall_fd=-1;
 for(unsigned i=0;i<x->pixel_count;i++)if(x->pixel_pages[i].memory)munmap((void*)x->pixel_pages[i].memory,4096);
 x->pixel_count=0;
}
