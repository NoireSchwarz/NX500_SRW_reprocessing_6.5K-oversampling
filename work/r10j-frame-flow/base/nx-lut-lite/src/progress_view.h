/* Stage milestones are not an estimate of time remaining. UI only. */
#ifndef NXL_PROGRESS_VIEW_H
#define NXL_PROGRESS_VIEW_H
struct nxl_progress_view {unsigned percent,changed,issue,auto_init;char operation[33],title[81],detail[200];};
struct nxl_progress_stage {const char*phase,*title;unsigned percent;};
static const struct nxl_progress_stage nxl_progress_stages[]={
 {"AUTO_RECORD","Starting initialization recorder",3},{"AUTO_BASELINE","Reading initial Still state",10},
 {"AUTO_EP_CYCLE","Testing original EP lifecycle",25},{"AUTO_EP_OPEN","Calling original EP open",35},
 {"AUTO_EP_CLOSE","Returning the EP reference",55},{"AUTO_EP_RETURN","Verifying code and reference return",70},
 {"AUTO_FINAL","Checking final Still readiness",85},{"AUTO_STOP_RECORD","Saving initialization evidence",95},
 {"AUTO_READY","Initialization complete",100},{"AUTO_STILL_COLD","Test saved - Still is not ready",100},
 {"RAW_REQUEST","Starting the workflow",1},{"SETUP","Checking task and camera",2},
 {"PIPE_PREPARE","Preparing shared workspace",2},{"PIPE_SRW","Reading SRW",8},
 {"PIPE_SOURCE_VERIFY","Verifying source and camera settings",10},{"PIPE_WORKSPACE","Preparing shared workspace",15},{"PIPE_RAW_DECODE","Decoding SRW pixels",22},{"PIPE_ISP","Running native Still ISP",28},{"PIPE_STILL_SETTLE","Verifying Still handoff",35},{"PIPE_FRONT_DONE","Still output ready",45},
 {"PIPE_LDC","Applying native lens correction",49},{"PIPE_LUT","Applying native LUT",53},{"PIPE_JPEG","Encoding native JPEG",65},
 {"PIPE_JPEG_WRITTEN","JPEG stream written",80},{"PIPE_RETIRE","Checking callback and DMA retirement",85},
 {"PIPE_CALLBACKS_QUIET","Callbacks and DMA are quiet",86},
 {"PIPE_NATIVE_RETURN","Returning the shared workspace",88},
 {"PIPE_ALLOCATOR_VERIFY","Verifying original allocator return",89},
 {"PIPE_RELEASED","Workspace return verified",90},{"PIPE_SAVED","Final JPEG saved",92},
 {"PIPE_VERIFY","Verifying the saved JPEG",94},{"PIPE_RESTORE","Restoring the camera",97},{"PIPE_IDLE_VERIFY","Checking native idle state",97},
 {"RAW_RESOURCE_CHECK","Starting resource check",2},{"CHECK_AUTH","Checking current task",5},
 {"CHECK_METADATA","Reading resource records",12},{"CHECK_CODE","Verifying original code",20},
 {"CHECK_POOL1","Checking allocator / Still 1 of 3",30},{"CHECK_POOL2","Checking allocator / Still 2 of 3",45},
 {"CHECK_POOL3","Checking allocator / Still 3 of 3",60},{"CHECK_BACKEND1","Checking DMA / queues 1 of 16",68},
 {"CHECK_BACKEND2","Checking DMA / queues 5 of 16",73},{"CHECK_BACKEND3","Checking DMA / queues 9 of 16",78},
 {"CHECK_BACKEND4","Checking DMA / queues 13 of 16",83},{"CHECK_FINAL","Rechecking the allocator",90},
 {"CHECK_LOCKS","Renewing round locks",94},{"CHECK_GRANT","Renewing front permission",97},
 {"CHECK_COMMIT","Creating the fresh task",99}
};
/* AUTO INIT contains a resource-check subtransaction followed by the pipeline.
 * Percentages below are relative to that entire action, not to the nested
 * check. They convey stage order only and never grant native permission. */
static const struct nxl_progress_stage nxl_auto_progress_stages[]={
 {"AUTO_RECORD","Starting initialization recorder",3},{"AUTO_BASELINE","Reading initial Still state",8},
 {"RAW_RESOURCE_CHECK","Checking resources for initialization",9},
 {"CHECK_AUTH","Checking current task",10},{"CHECK_METADATA","Reading resource records",11},
 {"CHECK_CODE","Verifying original code",12},{"CHECK_POOL1","Checking allocator / Still 1 of 3",13},
 {"CHECK_POOL2","Checking allocator / Still 2 of 3",14},{"CHECK_POOL3","Checking allocator / Still 3 of 3",15},
 {"CHECK_BACKEND1","Checking DMA / queues 1 of 16",16},{"CHECK_BACKEND2","Checking DMA / queues 5 of 16",16},
 {"CHECK_BACKEND3","Checking DMA / queues 9 of 16",17},{"CHECK_BACKEND4","Checking DMA / queues 13 of 16",17},
 {"CHECK_FINAL","Rechecking the allocator",18},{"CHECK_LOCKS","Renewing round locks",19},
 {"CHECK_GRANT","Renewing front permission",19},{"CHECK_COMMIT","Fresh task ready for initialization",20},
 {"AUTO_PRECHECK_DONE","Camera ready for the shared pipeline",20},{"AUTO_EP_CYCLE","Verifying source SRW and preparing pipeline",22},
 {"RAW_REQUEST","Starting initialization pipeline",22},{"SETUP","Checking task and camera",22},
 {"PIPE_PREPARE","Preparing shared workspace",23},{"AUTO_EP_ACQUIRE","Acquiring original EP reference",25},
 {"AUTO_EP_OPEN","Calling original EP open",25},{"PIPE_SRW","Reading SRW",28},
 {"PIPE_SOURCE_VERIFY","Verifying source and camera settings",29},{"PIPE_WORKSPACE","Preparing shared workspace",31},{"PIPE_RAW_DECODE","Decoding SRW pixels",34},{"PIPE_ISP","Running native Still ISP",38},{"PIPE_STILL_SETTLE","Verifying Still handoff",43},
 {"PIPE_FRONT_DONE","Still output ready",48},{"PIPE_LDC","Applying native lens correction",49},{"PIPE_LUT","Applying native LUT",53},
 {"PIPE_JPEG","Encoding native JPEG",65},{"PIPE_JPEG_WRITTEN","JPEG stream written",78},
 {"PIPE_RETIRE","Checking callback and DMA retirement",80},{"PIPE_CALLBACKS_QUIET","Callbacks and DMA are quiet",81},
 {"PIPE_NATIVE_RETURN","Returning the shared workspace",83},{"PIPE_ALLOCATOR_VERIFY","Verifying original allocator return",85},
 {"PIPE_RELEASED","Workspace return verified",87},{"PIPE_SAVED","Final JPEG saved",88},
 {"AUTO_EP_RELEASE","Returning the test EP reference",90},{"AUTO_EP_CLOSE","Returning the EP reference",90},
 {"AUTO_EP_RETURN","Verifying code and reference return",92},
 {"PIPE_VERIFY","Verifying the saved JPEG",94},{"PIPE_RESTORE","Restoring the camera",96},
 {"PIPE_IDLE_VERIFY","Checking native idle state",97},{"AUTO_FINAL","Checking final Still readiness",98},
 {"AUTO_STOP_RECORD","Saving initialization evidence",99},
 {"AUTO_READY","Initialization complete",100},{"AUTO_STILL_COLD","Test saved - Still is not ready",100}
};
static void nxl_progress_view_begin(struct nxl_progress_view*v,const char*operation,int check){memset(v,0,sizeof *v);strcpy(v->operation,operation);strcpy(v->title,check?"Starting resource check":"Starting SRW / LUT / JPEG");strcpy(v->detail,"Waiting for the first verified stage");v->changed=1;}
static int nxl_progress_view_record(struct nxl_progress_view*v,const struct np_record*r){
 if(!np_valid(r)||r->version!=3||strcmp(v->operation,r->operation))return 0;
 unsigned issue=!strcmp(r->state,"HELD")||!strcmp(r->state,"ERROR")||!strcmp(r->state,"RESTART_REQUIRED")||!strcmp(r->state,"RETRYABLE");
 if(issue){v->issue=1;strcpy(v->title,strstr(r->detail,"LDC_DMA_STOP_ACK_UNVERIFIED")?"Stopped - LDC DMA drain unverified":strstr(r->detail,"LDC_COMPLETION_TIMEOUT")?"Stopped - lens correction did not finish":strstr(r->detail,"LDC_CONFIGURATION_READBACK_FAILED")?"Stopped - lens configuration readback failed":strstr(r->detail,"LDC_FAILED")||strstr(r->detail,"LDC_WORKER_REPORTED_FAILURE")?"Stopped - lens correction failed":"Stopped - resources kept safe");
  snprintf(v->detail,sizeof v->detail,"%s",strstr(r->detail,"LDC_DMA_STOP_ACK_UNVERIFIED")?"LDC returned EOF, but DMA retirement is unverified; power off/on before another task.":r->detail);v->changed=1;return 1;}
 if(v->issue)return 0;
 if(!strcmp(r->phase,"PIPE_ABORT_CHECK")){strcpy(v->title,"Checking failed task ownership");snprintf(v->detail,sizeof v->detail,"%s",r->detail);v->changed=1;return 1;}
 const struct nxl_progress_stage*stages=v->auto_init?nxl_auto_progress_stages:nxl_progress_stages;
 unsigned count=v->auto_init?sizeof nxl_auto_progress_stages/sizeof *stages:sizeof nxl_progress_stages/sizeof *stages;
 for(unsigned i=0;i<count;i++)if(!strcmp(r->phase,stages[i].phase)){
  if(stages[i].percent<v->percent)return 0;
  if(!strcmp(v->title,stages[i].title)&&!strcmp(v->detail,r->detail))return 0;
  v->percent=stages[i].percent;strcpy(v->title,stages[i].title);snprintf(v->detail,sizeof v->detail,"%s",r->detail);v->changed=1;return 1;
 }
 return 0;
}
static void nxl_progress_view_finish(struct nxl_progress_view*v,int confirmed,int check){
 if(confirmed){v->percent=100;v->issue=0;strcpy(v->title,check?"Resource check passed":"JPEG saved - workspace returned");strcpy(v->detail,check?"Fresh task ready; returning to EV+AEL menu":"All three stages and final checks completed");}
 else{if(!v->issue)strcpy(v->title,"Stopped - returning to EV+AEL menu");v->issue=1;if(!v->detail[0]||!strcmp(v->detail,"Waiting for the first verified stage"))strcpy(v->detail,"Open SESSION STATE / PROGRESS for the reason");}
 v->changed=1;
}
#endif
