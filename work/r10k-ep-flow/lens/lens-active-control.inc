/* SPDX-License-Identifier: AGPL-3.0-only. Actual flock + process lifetime,
 * never an SD marker or boot-id whitelist. Stop targets a report, not a PID. */
static int lens_active(char*report,size_t cap){
 int lock=open("/tmp/nx-lut-lite/lens-evidence.fd",O_RDWR|O_NOFOLLOW|O_CLOEXEC);if(lock<0)return 0;
 int attempt=flock(lock,LOCK_EX|LOCK_NB),busy=attempt<0&&(errno==EAGAIN||errno==EWOULDBLOCK);if(!attempt)flock(lock,LOCK_UN);close(lock);if(!busy)return 0;
 char text[1400];int fd=open("/tmp/nx-lut-lite/lens-evidence.active",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st={0};
 int regular=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<(off_t)sizeof text;ssize_t n=regular?read(fd,text,(size_t)st.st_size):-1;close(fd);if(n!=st.st_size||n<=0)return 0;text[n]=0;
 int pid=0;unsigned long long ticks=0;char extra;
 if(sscanf(text,"NXLENSWATCH2\n%d\n%llu\n%1023[^\n]\n%c",&pid,&ticks,report,&extra)!=3||pid<=1||!ticks||cap<1024||!observer_path(report)||strncmp(report,"/mnt/mmc/NXDEMO51/INIT_LENS_",26)||process_identity(pid)!=ticks)return 0;
 char path[80],target[1200],self[1200];snprintf(path,sizeof path,"/proc/%d/exe",pid);ssize_t a=readlink(path,target,sizeof target-1),b=readlink("/proc/self/exe",self,sizeof self-1);
 if(a<=0||b<=0||a>=(ssize_t)sizeof target-1||b>=(ssize_t)sizeof self-1||a!=b||memcmp(target,self,(size_t)a)||process_identity(pid)!=ticks)return 0;
 return 1;
}
static int lens_control(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXLENSWATCH8 R9u MANUAL_ONLY PHYSICAL_USB_EXIT; full-width PP6/19 / native MV_UD power8 / first-frame counters / sensor window / movie buffer / LDC / Gamma19; bounded; own lock; read-only");return 0;}
 if(argc==2&&!strcmp(argv[1],"--usb-ready")){int s=nxl_observer_usb_state();puts(s==0?"RECORDER_USB_DISCONNECTED":s>0?"RECORDER_USB_CONNECTED":"RECORDER_USB_STATE_UNAVAILABLE");return s==0?0:7;}
 char report[1024]={0};int active=lens_active(report,sizeof report);
 if(argc==2&&!strcmp(argv[1],"--active")){if(active){puts(report);return 0;}puts("NO_LIVE_LENS_RECORDER");return 3;}
 if(!active)return 3;
 directory=open(report,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)return 2;
 if(argc==3&&!strcmp(argv[1],"--yield-hardware")){
  if(!lhy_token(argv[2])||!lhy_publish(directory,"hardware-yield.request",argv[2])){close(directory);return 1;}
  uint64_t deadline=now()+3000000000u;
  for(unsigned i=0;i<300&&now()<deadline;i++){
   char acknowledged[33],current[1024];
   if(lhy_read(directory,"hardware-yield.ack",acknowledged)&&!strcmp(acknowledged,argv[2])&&lens_active(current,sizeof current)&&!strcmp(current,report)){
    printf("RECORDER_EP_MMIO_YIELDED report=%s token=%s\n",report,argv[2]);close(directory);return 0;
   }
   struct timespec delay={0,10000000};nanosleep(&delay,NULL);
  }
  close(directory);puts("RECORDER_EP_MMIO_YIELD_UNCONFIRMED");return 1;
 }
 if(argc==2&&!strcmp(argv[1],"--stop")){
  int f=openat(directory,"STOP",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);int saved=errno;if(f>=0){close(f);fsync(directory);}close(directory);if(f<0&&saved!=EEXIST)return 1;puts(report);return 0;
 }
 if(argc==3&&!strcmp(argv[1],"--mark")){
  size_t n=strlen(argv[2]);if(!n||n>80){close(directory);return 2;}for(size_t i=0;i<n;i++)if(!((argv[2][i]>='a'&&argv[2][i]<='z')||(argv[2][i]>='A'&&argv[2][i]<='Z')||(argv[2][i]>='0'&&argv[2][i]<='9')||argv[2][i]=='_'||argv[2][i]=='-')){close(directory);return 2;}
  int f=openat(directory,"marks.tsv",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0){close(directory);return 1;}
  int ok=dprintf(f,"%llu\t%s\n",(unsigned long long)now(),argv[2])>0&&!fsync(f);if(close(f))ok=0;close(directory);return ok?0:1;
 }
 close(directory);return 2;
}
