#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "../../common/cmdq_evidence.h"
int main(int argc,char**argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);errno=ESRCH;
 const char*phase=mode==8?"AFTER_RELEASE_CALL":mode==9?"AFTER_RELEASE_CHECK":"BEFORE_SRW";
 int ok=cq_capture(mode==5?"/missing":"/mnt/mmc",mode==4?"INVALID":phase);assert(errno==ESRCH);
 if(mode==4||mode==5){assert(!ok);puts("PASS optional evidence failure leaves errno intact");return 0;}
 assert(ok);char path[128];snprintf(path,sizeof path,"/mnt/mmc/diag-cmdq-%s.bin",phase);
 struct cq_evidence r;int fd=open(path,O_RDONLY);assert(fd>=0&&read(fd,&r,sizeof r)==sizeof r&&!close(fd));
 assert(r.magic==CQ_EVIDENCE_MAGIC&&r.bytes==sizeof r&&r.version==1&&!strcmp(r.phase,phase));
 assert(r.checksum==cq_checksum(&r,offsetof(struct cq_evidence,checksum)));
 assert(r.read_mask==(mode==1?0:mode==3?29:mode==0||mode>=8?31:15));
 puts("PASS optional CMDQ evidence uses guarded read-only RAM and explicit availability");return 0;
}
