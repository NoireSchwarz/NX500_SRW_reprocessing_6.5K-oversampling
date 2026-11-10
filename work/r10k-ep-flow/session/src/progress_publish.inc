#include "../../common/progress_journal.h"
static int publish_progress(int argc,char**argv){
 if(argc!=6)return 64;const char*dir=argv[2];
#ifndef HOST_MODEL
 if(strcmp(dir,DEFAULT_ROOT))return 64;
#endif
 if(!safe(dir,1024,1)||!no_symlink_parents(dir)||!realdir(dir))return 1;
 char logs[1200],path[1300];if(!bounded(logs,sizeof logs,dir,"logs")||!realdir(logs)||!no_symlink_parents(logs)||!bounded(path,sizeof path,logs,"PROGRESS.events"))return 1;
 return np_publish(path,argv[3],argv[4],argv[5])?0:1;
}
