static int release_next_status;
static int release_prepare_next(void){
 int fd,status=0;if(!task_child_watch_open())return 0;
 pid_t worker=backend("release-next","",&fd);if(worker<0){task_child_watch_close();return 0;}
 FILE*log=fopen("/tmp/nx-lut-lite/finish-srw.log","w");
 strcpy(task_progress.title,"JPEG已儲存，正在準備新工作階段");strcpy(task_progress.detail,"沿用原有資源核驗流程，不自動開始下一張");task_progress.changed=1;task_progress_paint();
 pid_t done=task_wait_backend(worker,fd,log,&status);if(log)fclose(log);
 load_raw_status();
 return done==worker&&WIFEXITED(status)&&!WEXITSTATUS(status)&&raw_status.valid&&raw_status.state==NXL_RAW_NEED_STILL_INIT;
}
