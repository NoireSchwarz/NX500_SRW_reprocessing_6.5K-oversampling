#define _GNU_SOURCE
#include <assert.h>
#include "../../common/shared_workspace_io.h"
int main(int argc,char**argv){
 struct sw_lease s;assert(argc==2&&sw_file_read("/lease.bin",&s,sizeof s)&&sw_valid(&s));
 if(!strcmp(argv[1],"publish")){int ok=sw_owner_proof_publish(&s);printf("publish=%d\n",ok);return ok?0:2;}
 if(!strcmp(argv[1],"retire")){int ok=sw_owner_proof_retire(&s);printf("retire=%d\n",ok);return ok?0:2;}
 if(!strcmp(argv[1],"legacy")){
  char p[80],name[80];snprintf(p,sizeof p,"/proc/%u/comm",s.owner_pid);FILE*f=fopen(p,"r");assert(f&&fgets(name,sizeof name,f));fclose(f);
  int ok=!strncmp(name,"nxl-demo-front",14);printf("legacy_name_gate=%d\n",ok);return ok?0:2;
 }
 int ok=sw_owner_alive(&s);printf("alive=%d %s\n",ok,sw_owner_error);return ok?0:2;
}
