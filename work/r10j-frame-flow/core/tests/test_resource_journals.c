#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include "../src/sha256.h"
#include "../../common/resource_ledger.h"
#include "../../common/resource_inventory.h"
#include "../../common/progress_journal.h"
#include "../../common/workflow_hold.h"
int main(void){
 struct nr_resource r={.phase=NR_PREPARED,.capacity=167772160,.bank=2,.kind=35,.owner_pid=123,.ticks_lo=12345};
 assert(nr_append("/tmp/resource.bin",&r));r.request_started=1;r.phase=NR_ALLOCATING;assert(nr_append("/tmp/resource.bin",&r));
 r.phase=NR_REQUEST_RETURNED;r.response_valid=1;r.response[2]=r.capacity;r.response[3]=2;assert(nr_append("/tmp/resource.bin",&r));
 struct nr_resource last;assert(nr_read("/tmp/resource.bin",&last)&&last.sequence==3&&last.response_valid&&!last.response[0]&&!last.response[1]);
 for(unsigned i=0;i<sizeof last;i++){struct nr_resource bad=last;((unsigned char*)&bad)[i]^=1;assert(!nr_valid(&bad));}
 r=last;r.owner_pid++;assert(!nr_append("/tmp/resource.bin",&r));
 assert(np_publish("/mnt/mmc/PROGRESS.events","INSTALL","DONE","old task status remains history"));
 assert(np_publish("/mnt/mmc/PROGRESS.events","RESOURCE_CHECK","READY","last complete record"));
 struct np_record p;assert(np_last("/mnt/mmc/PROGRESS.events",&p)&&p.sequence==2&&!strcmp(p.state,"READY"));
 int fd=open("/mnt/mmc/PROGRESS.events",O_WRONLY|O_APPEND);assert(fd>=0&&write(fd,"torn",4)==4&&!close(fd));
 assert(np_last("/mnt/mmc/PROGRESS.events",&p)&&p.sequence==2);assert(np_publish("/mnt/mmc/PROGRESS.events","PIPELINE","RUNNING","torn display tail repaired"));
 assert(np_last("/mnt/mmc/PROGRESS.events",&p)&&p.sequence==3&&!strcmp(p.state,"RUNNING"));
 struct wh_hold h;assert(wh_check("/tmp/nx-lut-lite/engine.lock","/mnt/mmc/NXDEMO51/JOB_MODEL",&h)&&h.round&&h.round_pid==794);
 assert(!strcmp(h.reason,"UNSEEN_FUTURE_FAILURE"));assert(!wh_check("/tmp/nx-lut-lite/engine.lock","/mnt/mmc/NXDEMO51/JOB_OTHER",&h));
 fd=open("/tmp/nx-lut-lite/engine.lock/pid",O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0&&write(fd,"999\n",4)==4&&!close(fd));assert(!wh_check("/tmp/nx-lut-lite/engine.lock",NULL,&h));
 puts("PASS durable resource facts,256 corruption checks, append progress/torn-tail recovery, round identity and foreign/live-lock rejection");return 0;
}
