/* Display proof from this operation only; never grants hardware permission. */
static int auto_init_menu_result(int rc){
 if(rc!=0&&rc!=42)return 0;
 char path[256],text[512],want[512];
 snprintf(path,sizeof path,"/mnt/mmc/NXDEMO51/INIT_AUTO_%s/MENU.result",task_progress.operation);
 struct stat st;if(srw_lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<1||st.st_size>=(off_t)sizeof text)return 0;
 FILE*f=fopen(path,"r");if(!f)return 0;size_t n=fread(text,1,sizeof text-1,f);int ok=!ferror(f)&&feof(f);if(fclose(f))ok=0;else text[n]=0;
 if(!ok)return 0;
 snprintf(want,sizeof want,"NXAUTOMENU1\ntoken=%s\noutcome=%s\nreport=/mnt/mmc/NXDEMO51/INIT_AUTO_%s\nend=NXAUTOMENUEND1\n",task_progress.operation,rc==42?"STILL_COLD":"READY",task_progress.operation);
 return n==strlen(want)&&!memcmp(text,want,n)?rc==42?2:1:0;
}
static void auto_init_progress_complete(int result){
 if(!result){task_progress_complete(0,0);return;}
 task_progress.percent=100;task_progress.issue=result==2;
 strcpy(task_progress.title,result==2?"Test saved - Still is not ready":"Auto configuration and JPEG complete");
 strcpy(task_progress.detail,result==2?"Original EP reference returned; cold Still initialization remains to be traced":NXL_LENS_TEST_UI?"Workspace and EP reference returned; lens recorder remains ON":"Workspace and original EP reference returned");
 task_progress.changed=1;task_progress_paint();
}
