/* SPDX-License-Identifier: AGPL-3.0-only. Persisted return receipt, no hardware. */
static int verify_workspace_return(const char*root,unsigned release_only){
 char path[1400],text[16384],hash[65],want[65],*line[7];struct sw_lease issued,returned;
 snprintf(path,sizeof path,"%s/shared-workspace.bin",root);if(!sw_file_read(path,&issued,sizeof issued)||!sw_valid(&issued)||issued.phase!=SW_FRONT_READY)return 0;
 snprintf(path,sizeof path,"%s/workspace-released.bin",root);if(!sw_file_read(path,&returned,sizeof returned)||!sw_same_allocation(&issued,&returned)||returned.phase!=SW_RELEASED)return 0;
 snprintf(path,sizeof path,"%s/result.json",root);int fd=open_regular(path,0);struct stat st;if(fd<0)return 0;
 int ok=!fstat(fd,&st)&&st.st_size>0&&st.st_size<(off_t)sizeof text&&read_exact(fd,text,(size_t)st.st_size,0);close(fd);if(!ok)return 0;
 const char*ones[]={"ownership_verified","shared_workspace_borrowed","shared_workspace_released","release_returned","allocator_return_verified","original_code_restored","detached","idle_registers_verified","backend_CAP_releases"};
 const char*zero[]={"backend_CAP_requests","request_returned","allocation_retained","api_inflight_at_exit","camera_stopped_at_exit","copy_memory_quarantined"};
 for(unsigned i=0;i<sizeof ones/sizeof *ones;i++)if(!p49_field_uint(text,(size_t)st.st_size,ones[i],1))return 0;
 for(unsigned i=0;i<sizeof zero/sizeof *zero;i++)if(!p49_field_uint(text,(size_t)st.st_size,zero[i],0))return 0;
 if(!p49_field_uint(text,(size_t)st.st_size,"front_retirement_samples",16)||!p49_field_uint(text,(size_t)st.st_size,"pa",issued.pa)||
    !p49_field_uint(text,(size_t)st.st_size,"va",issued.va))return 0;
 if(release_only&&(!p49_field_uint(text,(size_t)st.st_size,"exit",0)||!p49_field_uint(text,(size_t)st.st_size,"release_only",1)||
    !p49_field_uint(text,(size_t)st.st_size,"copy_go_sent",0)||!p49_field_text(text,(size_t)st.st_size,"backend","SHARED_WORKSPACE_RELEASE_ONLY")))return 0;
 snprintf(path,sizeof path,"%s/result.json",root);if(!path_hash(path,hash,NULL))return 0;
 snprintf(path,sizeof path,"%s/controller-outcome.txt",root);fd=open_regular(path,0);if(fd<0)return 0;ok=lines_fd(fd,text,sizeof text,line,7);close(fd);
 if(!ok||strcmp(line[0],"NX51CONTROLLER1")||!equal_line(line[1],"classification","SAFE_RELEASED")||!equal_line(line[6],"result_complete","1"))return 0;
 const char*v=value(line[4],"result_sha256");if(!v||!hex64(v))return 0;strcpy(want,v);return !strcmp(want,hash);
}
