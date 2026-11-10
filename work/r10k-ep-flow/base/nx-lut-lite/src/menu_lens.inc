/* Read-only logger control stays in the existing menu. No hardware actions,
 * progress overlay, session transition or viewfinder navigation. */
static pid_t lens_child;
static int lens_fd=-1,lens_recording,lens_command;
static char lens_output[2048];static size_t lens_used;
static void lens_menu_start(int command){
 if(lens_child>0)return;
 const char*mode=command==1?"lens-start":command==2?"lens-mark":command==3?"lens-stop":"lens-status";
 int fd;pid_t p=backend(mode,"",&fd);
 if(p<0){strcpy(notice,"LENS CONTROL COULD NOT START");return;}
 lens_child=p;lens_fd=fd;lens_command=command;lens_used=0;lens_output[0]=0;
 int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);lens_fd=-1;}
 if(command)strcpy(notice,command==1?"STARTING LENS RECORDER":command==3?"SAVING LENS EVIDENCE":"ADDING LENS TEST MARK");
}
static void lens_menu_read(void){
 for(unsigned i=0;lens_fd>=0&&i<16;i++){char b[256];ssize_t n=read(lens_fd,b,sizeof b);if(n<0&&errno==EINTR)continue;if(n<=0)break;
  size_t take=(size_t)n;if(take>sizeof lens_output-1-lens_used)take=sizeof lens_output-1-lens_used;
  memcpy(lens_output+lens_used,b,take);lens_used+=take;lens_output[lens_used]=0;
 }
}
static int lens_menu_poll(void){
 if(lens_child<=0)return 0;
 lens_menu_read();
 int status=0;pid_t got=waitpid(lens_child,&status,WNOHANG);if(!got||(got<0&&errno==EINTR))return 0;
 lens_menu_read();if(lens_fd>=0)close(lens_fd);lens_fd=-1;lens_child=0;
 int ok=got>0&&WIFEXITED(status)&&!WEXITSTATUS(status);
 int active=strstr(lens_output,"RECORDER_STARTED ")||strstr(lens_output,"RECORDER_ALREADY_ACTIVE ")||strstr(lens_output,"RECORDER_ACTIVE ");
 int inactive=strstr(lens_output,"RECORDER_STOPPED ")||strstr(lens_output,"NO_LIVE_LENS_RECORDER");
 if(lens_command==1&&!active)ok=0;
 if(lens_command==2&&!strstr(lens_output,"MARKED "))ok=0;
 if(lens_command==3&&!inactive)ok=0;
 if(ok){
  if(active)lens_recording=1;
  if(inactive)lens_recording=0;
  if(lens_command)strcpy(notice,lens_command==1?"LENS RECORDER ON - NXDEMO51 / INIT_LENS":lens_command==3?"LENS EVIDENCE SAVED - RECORDER OFF":"LENS TEST MARK SAVED");
 }else if(lens_command&&strstr(lens_output,"RECORDER_USB_CONNECTED"))strcpy(notice,"UNPLUG USB TO START TEST RECORDER");
 else if(lens_command&&strstr(lens_output,"RECORDER_USB_STATE_UNAVAILABLE"))strcpy(notice,"USB STATE UNKNOWN - TEST RECORDER REFUSED");
 else if(lens_command)strcpy(notice,"LENS CONTROL FAILED - SEE INIT_LENS REPORT");
 return 1;
}
