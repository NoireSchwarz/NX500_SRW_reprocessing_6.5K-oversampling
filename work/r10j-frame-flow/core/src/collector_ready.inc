/* SPDX-License-Identifier: AGPL-3.0-only.
 * Collection readiness is distinct from nonce-bound RTOS task readiness.
 * Publish a complete marker before any slow source hashing/staging. It grants
 * no native operation; all existing ownership/READY/GO/stop gates still apply.
 */
static int collector_ready(const char *dir){
 char pending[1200],path[1200];struct stat st;
 int n=snprintf(path,sizeof path,"%s/READY",dir),p=snprintf(pending,sizeof pending,"%s/READY.pending",dir);
 if(n<0||p<0||(size_t)n>=sizeof path||(size_t)p>=sizeof pending)return 0;
 if(lstat(path,&st)==0||errno!=ENOENT)return 0;
 FILE *f=fopen(pending,"wx");if(!f)return 0;
 int ok=fputs("P49_DEMO_COLLECTOR_READY_ONLY\nRAW_PREPARATION_NOT_STARTED\nNATIVE_GO_NOT_SENT\n",f)>=0;
 if(fflush(f)||fsync(fileno(f)))ok=0;if(fclose(f))ok=0;
 if(!ok||rename(pending,path)){unlink(pending);return 0;}
 event("COLLECTOR_READY_BEFORE_REAL_SOURCE");return 1;
}
