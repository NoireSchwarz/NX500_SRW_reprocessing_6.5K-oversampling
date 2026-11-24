/* SPDX-License-Identifier: AGPL-3.0-only. Linux file-lifecycle repair only.
 * The reused RTOS code and core library are unchanged. RAM opens are RO. */
static int gamma_unused_production_main(int,char**);
#define main gamma_unused_production_main
#include "nxl-gamma-reload.c"
#undef main
#include "gamma_lifecycle.inc"
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_GAMMA_LIFECYCLE1 RC6.1 DISOWNED_GENERATIONS_NATIVE_FACTORY_PROOF NATIVE_FACTORY_REBUILD");return 0;}
 if(argc!=2||(strcmp(argv[1],"reconcile")&&strcmp(argv[1],"complete")))return 64;
 int ok=0,mode=-1,run=-1,ram=-1;char path[1200];
 uint32_t cookie=(uint32_t)ns()^(uint32_t)getpid();
 if(!gamma_audit_directory(life_dir,sizeof life_dir,"GAMMA_LIFE_RC61",cookie)){life_reason="LIFECYCLE_AUDIT_DIRECTORY_FAILED";goto done;}
 life_reason="LIFECYCLE_FIRMWARE_OR_APP_IDENTITY";
 if(!firmware()||!fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV))goto done;
 pid=find_camera();if(pid<2)goto done;snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,O_RDONLY|O_CLOEXEC);if(procfd<0)goto done;
 if(!load_maps(life_dir,"lifecycle")||!app_guards()||!constructor())goto done;
 devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(devfd<0)goto done;
 mode=lockfile("/tmp/nx-lut-lite/video-mode.fd");run=lockfile("/mnt/mmc/NXDEMO51/run.lock");ram=lockfile("/tmp/nx-lut-lite/run.lock");
 if(mode<0||run<0||ram<0){life_reason="LIFECYCLE_CONTROL_OR_SRW_BUSY";goto done;}
 if(!life_context())goto done;
 ok=!strcmp(argv[1],"complete")?life_complete():life_reconcile();
done:
 if(life_dir[0]){snprintf(path,sizeof path,"%s/result.json",life_dir);FILE*f=fopen(path,"wx");if(f){fprintf(f,"{\"format\":\"NXL_GAMMA_LIFECYCLE1\",\"ok\":%s,\"outcome\":\"%s\",\"reason\":\"%s\",\"failed_stage\":\"%s\",\"system_errno\":%d,\"partition_checks\":%u,\"reused_extent_checks\":%u,\"former_CAP_read_written_or_released\":false,\"RTOS_control_in_this_helper\":false,\"hardware_pixels_verified\":false}\n",ok?"true":"false",life_outcome,life_reason,life_stage,errno,life_free_checks,life_reused_checks);fclose(f);}}
 printf("NXL_GAMMA_LIFECYCLE1\nok=%d\noutcome=%s\nreason=%s\nstage=%s\nend=NXL_GAMMA_LIFECYCLE_END1\n",ok,life_outcome,life_reason,life_stage);
 if(procfd>=0)close(procfd);if(devfd>=0)close(devfd);if(mode>=0)close(mode);if(run>=0)close(run);if(ram>=0)close(ram);return ok?0:1;
}
