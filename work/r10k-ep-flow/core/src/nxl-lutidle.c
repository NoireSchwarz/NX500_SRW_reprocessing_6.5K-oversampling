/* SPDX-License-Identifier: AGPL-3.0-only.
 * Reuse the production read-only quiet_source implementation. No capture,
 * allocator transaction, ptrace, MMIO write or SD diagnostic is executed.
 */
#define main gamma_production_main
#include "nxl-gamma-reload.c"
#undef main
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_LUT_IDLE1 R11q quiet_source; read-only; no SD reports");return 0;}
 int state=argc==2&&!strcmp(argv[1],"--gamma-state");
 if(argc!=1&&!state)return 64;
 int ok=0;char tmp[]="/tmp/nx-lut-lite/idle-XXXXXX",file[128];const char*why="CAMERA_IDENTITY_UNAVAILABLE";
 if(!firmware()||!fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV))goto done;
 pid=find_camera();if(pid<2)goto done;
 char path[80];snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,O_RDONLY|O_CLOEXEC);if(procfd<0)goto done;
 if(!mkdtemp(tmp)){why="IDLE_RAM_REPORT_UNAVAILABLE";goto done;}
 ok=load_maps(tmp,"idle");snprintf(file,sizeof file,"%s/camera_maps_idle.txt",tmp);unlink(file);rmdir(tmp);
 if(!ok||!app_guards()||!constructor()){ok=0;why="CAMERA_CONTEXT_UNAVAILABLE";goto done;}
 devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(devfd<0){ok=0;why="CAMERA_RAM_UNAVAILABLE";goto done;}
 if(state){ok=gamma_load_lease();why=ok?"GAMMA_READ_ONLY_STATUS":"GAMMA_LEASE_IDENTITY_OR_OWNERSHIP_FAILED";}
 else {ok=quiet_source();why=idle_reason;}
 done:if(state){reason=why;ok=gamma_result(ok,0,NULL);}
 if(procfd>=0)close(procfd);if(devfd>=0)close(devfd);
 if(state)return ok?0:1;
 printf("NXL_LUT_IDLE1 ready=%d reason=%s\n",ok,why);return ok?0:1;
}
