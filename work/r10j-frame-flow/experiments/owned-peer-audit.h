/* Read-only peer diagnostics. Never whitelist, close or signal a peer.
 * The pre-existing p7_other_ep_users predicate remains the sole admission gate. */
static unsigned owner_peer_pid,owner_peer_saved;
static char owner_peer_kind[48]="NOT_SCANNED";
static const char*owner_peer_reason="EP_PROCESS_VIEW_UNAVAILABLE";
static void owner_peer_plain(char*s){for(;*s;s++)if((unsigned char)*s<32||*s==127)*s=' ';}
static int owner_peer_check(const char*dir){
 int admitted=p7_other_ep_users(-1);char path[1400];
 owner_peer_pid=owner_peer_saved=0;
 const char*reject=strstr(p7_peer_audit,"rejected=");
 if(reject){
  const char*line=reject;while(line>p7_peer_audit&&line[-1]!='\n')line--;
  unsigned scan=0;sscanf(line,"scan=%u pid=%u",&scan,&owner_peer_pid);
  unsigned n=0;reject+=9;while(n<sizeof owner_peer_kind-1&&((reject[n]>='A'&&reject[n]<='Z')||reject[n]=='_')){owner_peer_kind[n]=reject[n];n++;}owner_peer_kind[n]=0;
 }else snprintf(owner_peer_kind,sizeof owner_peer_kind,"%s",admitted?"CLEAR":p7_peer_audit_bytes+120>=sizeof p7_peer_audit?"AUDIT_LIMIT":"PROC_SCAN_UNAVAILABLE");
 if(!strcmp(owner_peer_kind,"EP_FD"))owner_peer_reason="EP_DEVICE_FD_IN_USE";
 else if(!strcmp(owner_peer_kind,"EP_MAPPING")||!strcmp(owner_peer_kind,"EP_DEVMEM_MAPPING"))owner_peer_reason="EP_REGISTER_MAPPING_IN_USE";
 else if(!strcmp(owner_peer_kind,"AUDIT_LIMIT"))owner_peer_reason="EP_PEER_AUDIT_FULL";
 else owner_peer_reason=admitted?"EP_PEER_SCAN_CLEAR":"EP_PROCESS_VIEW_UNAVAILABLE";
 if(p7_peer_audit_bytes>=sizeof p7_peer_audit)p7_peer_audit[sizeof p7_peer_audit-1]=0;
 snprintf(path,sizeof path,"%s/peer-ep-users.txt",dir);
 int audit_ok=owner_save(path,p7_peer_audit[0]?p7_peer_audit:"PROC_SCAN_UNAVAILABLE\n");
 char comm[128]="UNAVAILABLE",exe[512]="UNAVAILABLE",stat[4096]="UNAVAILABLE",proc[96];
 if(owner_peer_pid){
  snprintf(proc,sizeof proc,"/proc/%u/comm",owner_peer_pid);file_line(proc,comm,sizeof comm);owner_peer_plain(comm);
  snprintf(proc,sizeof proc,"/proc/%u/stat",owner_peer_pid);file_line(proc,stat,sizeof stat);owner_peer_plain(stat);
  snprintf(proc,sizeof proc,"/proc/%u/exe",owner_peer_pid);ssize_t n=readlink(proc,exe,sizeof exe-1);if(n>0&&n<(ssize_t)sizeof exe)exe[n]=0;else strcpy(exe,"UNAVAILABLE");owner_peer_plain(exe);
 }
 /* No command line/environment is collected: executable identity is enough
  * to diagnose inheritance, recorder maps or an unrelated EP client. */
 char identity[6000];int n=snprintf(identity,sizeof identity,"NXEPPEERS1\nadmitted=%d\nreason=%s\nkind=%s\npid=%u\ncomm=%s\nexe=%s\nstat=%s\nscans=%u\nchecked_pids=%u\nscan_bytes=%u\nscan_log_saved=%u\nnative_calls_at_scan=%u\nattach_attempted_at_scan=%u\nCAP_requests=0\npeer_closed=0\npeer_signalled=0\nend=NXEPPEERSEND1\n",admitted,owner_peer_reason,owner_peer_kind,owner_peer_pid,comm,exe,stat,p7_peer_scans,p7_peer_pids,(unsigned)p7_peer_audit_bytes,audit_ok,native_calls,attach_attempted);
 snprintf(path,sizeof path,"%s/peer-identity.txt",dir);
 int identity_ok=n>0&&n<(int)sizeof identity&&owner_save(path,identity);
 owner_peer_saved=audit_ok&&identity_ok;
 if(log_file){fprintf(log_file,"EP_PEERS admitted=%d kind=%s pid=%u reason=%s evidence_saved=%u native_calls=%u attached=%u\n",admitted,owner_peer_kind,owner_peer_pid,owner_peer_reason,owner_peer_saved,native_calls,attach_attempted);fflush(log_file);}
 if(!owner_peer_saved){owner_peer_reason="EP_PEER_AUDIT_SAVE_FAILED";return 0;}
 return admitted;
}
