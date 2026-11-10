/* Version adapters describe observed operations; policy never uses error names. */
#include "../../common/resource_ledger.h"
#include "../../common/resource_inventory.h"
static struct ri_inventory inventory;
static char resource_source_hash[65],resource_target_hash[65];
static unsigned source_front_attempt,source_backend_attempt;
static int source_counts(const char*base,unsigned*fa,unsigned*ba,char record_hash[65]){
 char path[1300];snprintf(path,sizeof path,"%s/STATE.journal",base);char*b=malloc(131073);if(!b)return 0;int ok=readtext(path,b,131073);unsigned seq=0;char*last=NULL;
 if(ok)for(char*p=b;*p;){char*e=strstr(p,"end=NX51END1\n");if(!e){ok=0;break;}e+=13;char*sum=strstr(p,"sha256=");if(!sum||sum>=e||e-sum!=85||memcmp(p,"NX51STATE1\n",11)){ok=0;break;}
  char want[65],h[65],j[1100];memcpy(want,sum+7,64);want[64]=0;hexhash(p,(size_t)(sum-p),h);if(strcmp(want,h)||sum[71]!='\n'){ok=0;break;}
  char saved=*e;*e=0;uint32_t n=0;if(!num(p,"seq",&n)||!n||(seq&&n!=seq+1)||!key(p,"job",j,sizeof j)||strcmp(j,base)){ok=0;*e=saved;break;}seq=n;last=p;*e=saved;if(!*e)break;p=e;
 }
 if(ok&&last){ok=num(last,"front_attempt",fa)&&num(last,"backend_attempt",ba)&&*fa<=RI_MAX&&*ba<=RI_MAX&&*fa+*ba<=RI_MAX;hexhash(last,strlen(last),record_hash);}else ok=0;free(b);return ok;
}
static int resource_ledger(const char*dir){
 char path[1300];snprintf(path,sizeof path,"%s/RESOURCE.journal",dir);struct stat st;if(lstat(path,&st))return errno==ENOENT?-1:0;
 struct nr_resource r;if(!nr_read(path,&r))return 0;inventory.operations++;
 if(r.phase==NR_RELEASED){if(!r.ownership_verified)return 0;inventory.resolved++;return 1;}
 if(!r.request_started){if(r.native_calls||r.mcb_sends||r.slot_writes||r.go||r.ownership_verified||r.response_valid)return 0;inventory.resolved++;return 1;}
 if(r.response_valid&&!r.response_status&&!r.response[0]&&!r.response[1]&&r.response[2]==r.capacity&&r.response[3]==2&&!r.go&&!r.ownership_verified){inventory.resolved++;return 1;}
 if(r.response_valid&&r.response[1]&&ri_add(&inventory,RI_EXTENT,r.response[1],r.response[0],r.capacity))return 1;
 if(r.go||r.mcb_sends||r.slot_writes||r.ownership_verified)return 0;return ri_add(&inventory,RI_UNLOCATED,0,0,r.capacity);
}
static int legacy_request_span(const char*dir,unsigned*out){
 char path[1300],b[65536];snprintf(path,sizeof path,"%s/probe.log",dir);if(!readtext(path,b,sizeof b))return 0;
 const char*key="ALLOCATION_CAPACITY required_blocks=";char*p=strstr(b,key);if(!p||strstr(p+1,key))return 0;p+=strlen(key);char*e;unsigned long n=strtoul(p,&e,10);
 if(e==p||*e!=' '||!n||n>16384u)return 0;*out=(unsigned)n*16384u;return 1;
}
static int legacy_allocator_return(const char*dir){
 char path[1300],b[65536];snprintf(path,sizeof path,"%s/trace_events.txt",dir);if(!readtext(path,b,sizeof b))return 0;
 char*p=strstr(b,"phase=request_return ");if(!p||strstr(p+1,"phase=request_return elapsed_ms=" )==NULL)return 0;
 char*event=strstr(p,"EVENT ");if(!event||strncmp(strstr(event,"phase=" )?strstr(event,"phase="):"","phase=request_return ",21))return 0;
 char*regs=strstr(event,"REGISTERS valid=1 errno=0 r0=0x00000000 ");char*verified=strstr(event,"BREAKPOINT_VERIFIED phase=request_return index=1 ");
 return regs&&verified&&regs<verified&&strstr(verified,"DETACH forward_signal=0 restored=1");
}
static int legacy_front_resource(const char*dir){
 char path[1300],b[32768];size_t n;snprintf(path,sizeof path,"%s/case_00/result.json",dir);
 if(!readtext(path,b,sizeof b)){
  snprintf(path,sizeof path,"%s/result.json",dir);if(!readtext(path,b,sizeof b))return 0;n=strlen(b);
  if(!p49_field_text(b,n,"format","NX_FAST_FRONT_BATCH1"))return 0;
  if((p49_field_uint(b,n,"exit",42)||p49_field_uint(b,n,"exit",43))&&p49_field_uint(b,n,"cap_allocation_bytes",0)){inventory.operations++;inventory.resolved++;return 1;}
  snprintf(path,sizeof path,"%s/shared-workspace.bin",dir);struct sw_lease lease;if(!sw_file_read(path,&lease,sizeof lease)||!sw_receipt_valid(&lease))return 0;
  inventory.operations++;return ri_add(&inventory,RI_EXTENT,lease.pa,lease.va,lease.capacity);
 }
 n=strlen(b);if(!p49_field_text(b,n,"format","NXIPCOWNED1"))return 0;uint32_t owned,pa,va,bytes,calls;
 if(!jnum(b,"ownership_verified",&owned)||!jnum(b,"pa",&pa)||!jnum(b,"va",&va)||!jnum(b,"cap_allocation_bytes",&bytes)||!jnum(b,"native_calls",&calls))return 0;
 inventory.operations++;if(owned==1&&pa&&va&&bytes)return ri_add(&inventory,RI_EXTENT,pa,va,bytes);
 const char*zeros[]={"ownership_verified","pa","va","cap_allocation_bytes","mcb_sends","native_packet_readbacks","cache_frame_attempts","temporary_dispatch_pointer_writes","isp_jobs_submitted","srw_decode_jobs","ep_jobs","lut_jobs","api_inflight_at_exit","camera_stopped_at_exit"};
 for(unsigned i=0;i<sizeof zeros/sizeof *zeros;i++)if(!p49_field_uint(b,n,zeros[i],0))return 0;
 if(!calls){inventory.resolved++;return 1;}
 if(calls!=1||!p49_field_uint(b,n,"original_code_restored",1)||!p49_field_uint(b,n,"detached",1)||!legacy_allocator_return(dir)||!legacy_request_span(dir,&bytes))return 0;
 return ri_add(&inventory,RI_UNLOCATED,0,0,bytes);
}
static int backend_resource(const char*dir,int shared_front_resolved){
 char path[1300],b[16384],o[2048],hash[65];snprintf(path,sizeof path,"%s/result.json",dir);if(!readtext(path,b,sizeof b))return shared_front_resolved;
 snprintf(path,sizeof path,"%s/controller-outcome.txt",dir);if(!readtext(path,o,sizeof o)||!key(o,"result_sha256",hash,sizeof hash)||!boundfile(dir,"result.json",hash))return shared_front_resolved;
 uint32_t pa,va,bytes;if(!jnum(b,"pa",&pa)||!jnum(b,"va",&va)||!jnum(b,"requested_bytes",&bytes))return shared_front_resolved;size_t n=strlen(b);inventory.operations++;
 const char*ones[]={"release_returned","allocator_return_verified","original_code_restored","detached","idle_registers_verified"};int released=1;
 for(unsigned i=0;i<sizeof ones/sizeof *ones;i++)released&=p49_field_uint(b,n,ones[i],1);
 const char*zeros[]={"api_inflight_at_exit","camera_stopped_at_exit","copy_memory_quarantined"};for(unsigned i=0;i<sizeof zeros/sizeof *zeros;i++)released&=p49_field_uint(b,n,zeros[i],0);
 if(released){inventory.resolved++;for(unsigned i=0;i<inventory.count;)if(inventory.item[i].mode==RI_EXTENT&&inventory.item[i].pa==pa&&inventory.item[i].bytes==bytes){memmove(inventory.item+i,inventory.item+i+1,(inventory.count-i-1)*sizeof inventory.item[0]);inventory.count--;}else i++;return 1;}if(shared_front_resolved)return 1;if(pa&&va&&bytes)return ri_add(&inventory,RI_EXTENT,pa,va,bytes);
 return p49_field_text(b,n,"resource_disposition","SAFE_NO_SUBMIT")&&p49_field_uint(b,n,"native_calls",0)&&p49_field_uint(b,n,"copy_go_sent",0);
}
static int resource_metadata(void){
 memset(&inventory,0,sizeof inventory);if(!source_counts(previous,&source_front_attempt,&source_backend_attempt,resource_source_hash))return 0;
 unsigned fa,ba;if(!source_counts(job,&fa,&ba,resource_target_hash))return 0;int all_ledgers_resolved=source_front_attempt>0,shared_ledger_coverage=source_front_attempt>0;
 for(unsigned i=1;i<=source_front_attempt;i++){char dir[1200];snprintf(dir,sizeof dir,"%s/front_%u/front",previous,i);if(!no_links(dir))return 0;unsigned before=inventory.count;
  int rc=resource_ledger(dir);if(rc<0){shared_ledger_coverage=0;rc=legacy_front_resource(dir);}if(!rc)return 0;if(inventory.count!=before)all_ledgers_resolved=0;
 }
 for(unsigned i=1;i<=source_backend_attempt;i++){char dir[1200];snprintf(dir,sizeof dir,"%s/jpeg/attempt_%u/hardware",previous,i);
  struct stat st;if(lstat(dir,&st)){if(errno==ENOENT&&(all_ledgers_resolved||shared_ledger_coverage))continue;return 0;}if(!no_links(dir))return 0;
  /* The v1 owner ledger covers the one CAP used by every embedded stage.
   * An interrupted helper report adds no unrecorded allocation. A complete
   * positive helper return receipt can additionally close that same extent. */
  if(!backend_resource(dir,shared_ledger_coverage))return 0;
 }
 old_pa=old_va=backend_pa=backend_va=old_nonce=0;old_bytes=0;
 for(unsigned i=0;i<inventory.count;i++)if(inventory.item[i].mode==RI_EXTENT){if(!old_pa){old_pa=inventory.item[i].pa;old_va=inventory.item[i].va;old_bytes=inventory.item[i].bytes;}else if(!backend_pa){backend_pa=inventory.item[i].pa;backend_va=inventory.item[i].va;}}
 return 1;
}
