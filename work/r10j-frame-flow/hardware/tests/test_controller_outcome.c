#define main controller_main_not_executed
#include "../src/nxl-allocprobe.c"
#undef main
#include <assert.h>
int main(int argc,char **argv){
 assert(argc==2);started=ns();
 assert(!strcmp(controller_classification(),"SAFE_NO_SUBMIT"));
 unsigned *unsafe[]={&attach_attempted,&shell_message_queued,&native_calls,&request_done,&ownership_verified,&api_inflight,&p7_go_sent};
 for(unsigned i=0;i<sizeof unsafe/sizeof *unsafe;i++){*unsafe[i]=1;assert(!strcmp(controller_classification(),"UNKNOWN_REQUIRE_RESTART"));*unsafe[i]=0;}
 struct p7_watch watch={0};p7_shared=&watch;watch.camera_paused=1;assert(!strcmp(controller_classification(),"UNKNOWN_REQUIRE_RESTART"));watch.camera_paused=0;
 ownership_verified=release_done=return_verified=code_restored=detached_ok=idle_registers_verified=1;
 assert(!strcmp(controller_classification(),"SAFE_RELEASED"));
 p7_pauses=2;p7_resumes=1;p7_resumed_verified=1;assert(!strcmp(controller_classification(),"UNKNOWN_REQUIRE_RESTART"));
 p7_resumes=2;p7_resumed_verified=2;assert(!strcmp(controller_classification(),"SAFE_RELEASED"));
 p7_quarantined=1;assert(!strcmp(controller_classification(),"UNKNOWN_REQUIRE_RESTART"));p7_quarantined=0;
 reason="MODEL_SAFE_RELEASED";p7_result.cold_restart_required=1;
 assert(controller_report(argv[1],1,"UNKNOWN_REQUIRE_RESTART"));
 assert(controller_report(argv[1],1,controller_classification()));
 printf("PASS 13 lifecycle classifications plus actualatomicresult/outcomehashcommit\n");return 0;
}
