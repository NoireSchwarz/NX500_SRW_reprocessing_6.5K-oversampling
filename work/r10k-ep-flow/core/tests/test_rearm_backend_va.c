#define main rearm_production_main
#include "../src/nxl-session-rearm.c"
#undef main
int main(int argc,char**argv){if(argc!=2)return 3;previous="/mnt/mmc/NXDEMO51/JOB_MODEL";backend_attempt=1;int ok=backend_info();printf("backend_info=%d va=%u\n",ok,backend_va);return ok==atoi(argv[1])?0:4;}
