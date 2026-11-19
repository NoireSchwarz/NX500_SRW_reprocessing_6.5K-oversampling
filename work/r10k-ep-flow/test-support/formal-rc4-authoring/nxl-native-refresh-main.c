/* SPDX-License-Identifier: AGPL-3.0-only. NX500 1.12 original menu commands.
 * No ptrace, process-memory writes or exposure-mode switch. This helper does
 * not inject keys. The original record-resolution command includes Samsung's
 * own S1 GUI refresh; the photo-resolution command does not.
 * Only resolution commands already dispatched by the native application.
 */
#include "native_refresh_readonly.inc"
static volatile sig_atomic_t cancelled;
static void stop_refresh(int s){(void)s;cancelled=1;}
static int refresh_lock(const char*p){
 int f=open(p,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);struct stat st;
 if(f<0)return -1;
 if(fstat(f,&st)||!S_ISREG(st.st_mode)||flock(f,LOCK_EX|LOCK_NB)){close(f);return -1;}return f;
}
static int native_state(struct camera*c,unsigned *live,unsigned *movie,const char**why){
 *why="CAMERA_RESTARTED";
 if(identity(c->pid)!=c->start)return 0;
 *why="STOP_RECORDING_OR_CAPTURE_FIRST";if(busy(c)!=0)return 0;
 *why="NATIVE_UI_STATE_UNAVAILABLE";
 if(pread(c->mem,live,4,0x4ac044)!=4||pread(c->mem,movie,4,0x4ac138)!=4||*live!=1||*movie>1)return 0;
 return vp_library_context(c,why);
}
static int submit(const char*kind,const char*name){
 /* Arguments are selected from exact factory tables, never shell text. */
 pid_t child=fork();if(child<0)return 0;
 if(!child){execl("/usr/bin/st","st","app","nx",kind,"resolution",name,(char*)0);_exit(127);}
 uint64_t end=ms()+4500;int status=0;pid_t got=0;
 while(!cancelled&&ms()<end){got=waitpid(child,&status,WNOHANG);if(got==child)return WIFEXITED(status)&&WEXITSTATUS(status)==0;if(got<0&&errno!=EINTR)return 0;usleep(20000);}
 kill(child,SIGTERM);for(unsigned i=0;i<10;i++){got=waitpid(child,&status,WNOHANG);if(got==child)return 0;usleep(10000);}
 kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}return 0;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_NATIVE_REFRESH_RC4 original st resolution commands; RO verification");return 0;}
 if(argc!=2||(strcmp(argv[1],"video24")&&strcmp(argv[1],"video30")&&strcmp(argv[1],"photo")&&strcmp(argv[1],"snapshot")))return 64;
 const char*why="NX500_FIRMWARE_IDENTITY_FAILED";struct camera c={0,-1,0,0,0};
 int lock=-1,run=-1,ok=0,submitted=0;unsigned live=0,movie=0,before[4]={0};
 signal(SIGTERM,stop_refresh);signal(SIGINT,stop_refresh);signal(SIGHUP,stop_refresh);
 if(!firmware())goto out;
 c.pid=camera_pid();c.start=identity(c.pid);why="CAMERA_PROCESS_UNAVAILABLE";if(c.pid<=1||!c.start)goto out;
 char path[100];snprintf(path,sizeof path,"/proc/%d/mem",c.pid);c.mem=open(path,O_RDONLY|O_CLOEXEC);
 why="CAMERA_READ_UNAVAILABLE";if(c.mem<0)goto out;
 lock=refresh_lock("/tmp/nx-lut-lite/video-mode.fd");run=refresh_lock("/tmp/nx-lut-lite/run.lock");
 why="NATIVE_REFRESH_BUSY";if(lock<0||run<0)goto out;
 struct stat st;why="NATIVE_REFRESH_BUSY";if(lstat("/tmp/nx-lut-lite/engine.lock",&st)==0||errno!=ENOENT)goto out;
 if(!native_state(&c,&live,&movie,&why))goto out;
 memcpy(before,vp_raw_preferences,sizeof before);
 if(!strcmp(argv[1],"snapshot")){why="READ_ONLY_SNAPSHOT";ok=1;goto out;}
 const char*kind="record",*name=NULL;unsigned want=!strcmp(argv[1],"video24")?0:1;
 if(!strcmp(argv[1],"photo")){
  kind="capture";for(unsigned i=0;i<sizeof photos/sizeof*photos;i++)if(photos[i].value==before[3])name=photos[i].name;
  why="PHOTO_SPEC_UNSUPPORTED";if(!name)goto out;
 }else{why="SELECT_NTSC_PRO_FIRST";if(!vp_ntsc||vp_quality!=2)goto out;name=want?"30f":"24f";}
 why="NATIVE_COMMAND_TOOL_IDENTITY_FAILED";
 if(!vp_digest("/usr/bin/st","3cb369d598d35ad498651cb8e409137c424af3ac52afa7e97e2b0dd475f2e370"))goto out;
 why="NATIVE_RESOLUTION_COMMAND_FAILED";if(cancelled||!submit(kind,name))goto out;submitted=1;
 /* Native st submits asynchronously. Bound the preference acknowledgement;
  * a video still needs its final MP4 dimensions checked on the camera. */
 uint64_t until=ms()+2200;
 do{
  if(!native_state(&c,&live,&movie,&why))goto out;
  if(vp_raw_preferences[0]!=before[0]||vp_raw_preferences[2]!=before[2]||vp_raw_preferences[3]!=before[3]){why="UNEXPECTED_NATIVE_SETTING_CHANGE";goto out;}
  if((!strcmp(kind,"capture")&&vp_raw_preferences[1]==before[1])||(!strcmp(kind,"record")&&vp_size==want)){ok=1;why="NATIVE_RESOLUTION_COMMAND_AND_SETTINGS_VERIFIED";break;}
  usleep(30000);
 }while(!cancelled&&ms()<until);
 if(!ok)why="NATIVE_RESOLUTION_ACK_TIMEOUT";
out:
 printf("NXL_NATIVE_REFRESH1 ok=%d submitted=%d action=%s pid=%d liveview=%u movie_standby=%u NTSC=%u movie_size=%u quality=%u photo_size=%u reason=%s\n",ok,submitted,argv[1],c.pid,live,movie,vp_ntsc,vp_size,vp_quality,vp_raw_preferences[3],why);
 if(c.mem>=0)close(c.mem);if(run>=0)close(run);if(lock>=0)close(lock);return ok?0:1;
}
