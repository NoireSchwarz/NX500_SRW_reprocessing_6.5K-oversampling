#define main restore_production_main
#include "../src/nxl-oem-restore.c"
#undef main
int main(int argc,char**argv){if(argc!=2)return 3;strcpy(parent_job,"/mnt/mmc/NXDEMO51/JOB_MODEL");directory="/mnt/mmc/NXDEMO51/JOB_MODEL/restore-camera-pipeline-1";int ok=ni_workspace();printf("native_idle_workspace=%d va=%u\n",ok,ni_lease.va);return ok==atoi(argv[1])?0:4;}
