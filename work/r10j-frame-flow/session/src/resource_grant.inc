/* Permission renewal follows current retirement and owned round-lock cleanup. */
static int renew_resource_grant(const struct session*s,const struct session*source,const char*proof){
 if(!rearm_receipt(s,source,proof)||!lock_reset_receipt(s,proof))return 0;
 const char*path="/opt/usr/nx-lut-lite/state/p49-experiment.state";int fd=open(path,O_RDWR|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 static const char ready[]="NXP33INSTALL1\nREADY=0.3.6-demo50\n",started[]="NXP33INSTALL1\nSTARTED=0.3.6-demo50\n";
 struct stat st;char b[4097];int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&st.st_size>0&&st.st_size<4096&&!flock(fd,LOCK_EX|LOCK_NB);
 ssize_t n=ok?pread(fd,b,(size_t)st.st_size,0):-1;if(n<0||n!=st.st_size)ok=0;
 if(ok){b[n]=0;ok=(n==sizeof ready-1&&!memcmp(b,ready,sizeof ready-1))||(n>=(ssize_t)sizeof started-1&&!memcmp(b,started,sizeof started-1));}
 if(ok){char before[2048];ok=bounded(before,sizeof before,proof,"FRONT_GRANT.before");int out=ok?open(before,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600):-1;if(out<0)ok=0;else{ok=write_all(out,b,(size_t)n)&&!fsync(out);if(close(out))ok=0;}}
 if(ok)ok=!ftruncate(fd,0)&&lseek(fd,0,SEEK_SET)==0&&write_all(fd,ready,sizeof ready-1)&&!fsync(fd);
 char actual[sizeof ready];if(ok)ok=pread(fd,actual,sizeof ready,0)==sizeof ready-1&&!memcmp(actual,ready,sizeof ready-1);if(close(fd))ok=0;
 if(ok){char text[1400];int n=snprintf(text,sizeof text,"NX51FRONTGRANT1\njob=%s\ntoken=%s\nready=1\n",s->job,getenv("NX51_SESSION_TOKEN"));ok=n>0&&n<(int)sizeof text&&atomic(proof,"FRONT_GRANT.ready",text);}
 return ok;
}
static int resource_grant_receipt(const struct session*s,const char*proof){char path[2048],b[1400],want[1400];size_t n;snprintf(want,sizeof want,"NX51FRONTGRANT1\njob=%s\ntoken=%s\nready=1\n",s->job,getenv("NX51_SESSION_TOKEN"));return bounded(path,sizeof path,proof,"FRONT_GRANT.ready")&&bytes(path,b,sizeof b,&n)&&n==strlen(want)&&!memcmp(b,want,n);}
