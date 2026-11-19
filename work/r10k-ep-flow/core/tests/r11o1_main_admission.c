#pragma clang optimize off
#define main production_main
#include "../src/nxl-gamma-reload.c"
#undef main
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--fixture-points")){
  printf("{\"firmware\":%u,\"fingerprint\":%u,\"find_camera\":%u,\"load_maps\":%u,\"app_guards\":%u,\"constructor\":%u,\"code_guards\":%u,\"vfw_capture_route\":%u,\"snapshot_file\":%u,\"physical_read\":%u,\"gamma_code_guards\":%u,\"alarm\":%u,\"signal\":%u,\"time\":%u,\"VM_UHD\":%u}\n",(unsigned)firmware,(unsigned)fingerprint,(unsigned)find_camera,(unsigned)load_maps,(unsigned)app_guards,(unsigned)constructor,(unsigned)code_guards,(unsigned)vfw_capture_route,(unsigned)snapshot_file,(unsigned)physical_read,(unsigned)gamma_code_guards,(unsigned)alarm,(unsigned)signal,(unsigned)time,(unsigned)VM_UHD);return 0;
 }
 return production_main(argc,argv);
}
