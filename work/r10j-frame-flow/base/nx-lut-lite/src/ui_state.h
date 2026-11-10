/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_UI_STATE_H
#define NXL_UI_STATE_H
#define NXL_DEMO_VERSION "0.4.0-demo51-422"
#define NXL_DEMO_SOURCE "SAM_0183.SRW"
#define NXL_DEMO_ROWS 13
#include "menu_srw_layout.h"
static int nxl_demo_look_known(const char *id) {
    return !strcmp(id,"kodak_portra400") || !strcmp(id,"fuji_provia100f") || !strcmp(id,"mono_hp5");
}
enum nxl_raw_state { NXL_RAW_INVALID, NXL_RAW_NEED_STILL_INIT, NXL_RAW_FRONT_RUNNING,
    NXL_RAW_NEED_JPEG_INIT, NXL_RAW_BACKEND_RUNNING, NXL_RAW_COMPLETE,
    NXL_RAW_RETRYABLE, NXL_RAW_RESTART_REQUIRED };
struct nxl_raw_status { enum nxl_raw_state state; int valid,front_verified; char resume[8],reason[96],look_id[81]; int strength; };
static enum nxl_raw_state nxl_raw_state_for(const char *name) {
    static const char *names[]={"", "NEED_STILL_INIT", "FRONT_RUNNING", "NEED_JPEG_INIT",
        "BACKEND_RUNNING", "COMPLETE", "RETRYABLE", "RESTART_REQUIRED"};
    for(unsigned i=1;i<sizeof names/sizeof *names;i++)if(!strcmp(name,names[i]))return (enum nxl_raw_state)i;
    return NXL_RAW_INVALID;
}
static const char *nxl_raw_state_name(enum nxl_raw_state state) {
    static const char *names[]={"STATUS UNAVAILABLE", "BOOT INIT REQUIRED", "PIPELINE RUNNING", "OLD SPLIT JOB",
        "BACKEND RUNNING", "COMPLETE", "RETRYABLE", "RESOURCE CHECK REQUIRED"};
    return state>=NXL_RAW_INVALID && state<=NXL_RAW_RESTART_REQUIRED?names[state]:names[0];
}
static const char *nxl_raw_action(enum nxl_raw_state state) {
    if(state==NXL_RAW_NEED_STILL_INIT)return "RUN SRW/LUT/JPEG";
    if(state==NXL_RAW_NEED_JPEG_INIT)return "NEW JOB REQUIRED";
    if(state==NXL_RAW_RETRYABLE)return "RETRY PIPELINE";
    if(state==NXL_RAW_COMPLETE)return "VIEW STATUS";
    if(state==NXL_RAW_RESTART_REQUIRED)return "CHECK REPORT";
    return "VIEW STATUS";
}
static const char *nxl_raw_hint(enum nxl_raw_state state) {
    if(state==NXL_RAW_NEED_STILL_INIT)return "BOOT: TAKE PHOTO + ALBUM EDIT / RESIZE";
    if(state==NXL_RAW_FRONT_RUNNING)return "SRW / LUT / JPEG - ONE RUN, PLEASE WAIT";
    if(state==NXL_RAW_NEED_JPEG_INIT)return "OLD JOB - CREATE A NEW PIPELINE TASK";
    if(state==NXL_RAW_BACKEND_RUNNING)return "NATIVE LUT / JPEG 4:2:2 - PLEASE WAIT";
    if(state==NXL_RAW_COMPLETE)return "NATIVE.JPG SAVED - NEW SESSION TO REPEAT";
    if(state==NXL_RAW_RETRYABLE)return "SAFE STOP - PREPARE CAMERA THEN RETRY";
    if(state==NXL_RAW_RESTART_REQUIRED)return "HARDWARE STATE HELD - CHECK PROGRESS / LOG";
    return "SESSION STATUS FAILED - CHECK SD";
}
/* This is an explicit proof-only reset request, never permission to reuse
 * old YUV or run native hardware. The runner independently verifies proof. */
static int nxl_raw_can_prepare_full(const struct nxl_raw_status *s) {
    return s->valid && s->state==NXL_RAW_RESTART_REQUIRED &&
        (!strcmp(s->reason,"LEGACY_STATE_DIVERGENCE") || !strcmp(s->reason,"FRONT_RESTART_OR_PATH_INVALID"));
}
static int nxl_raw_can_rearm(const struct nxl_raw_status *s) {
    return s->valid && s->state==NXL_RAW_RESTART_REQUIRED;
}
static const char *nxl_raw_status_state_name(const struct nxl_raw_status *s) {
    /* The durable legacy state name is kept for protocol compatibility.
     * This empty inherited session needs the read-only resource check;
     * repeating a power cycle is not the action this screen can verify. */
    if(nxl_raw_can_rearm(s))return "RESOURCE CHECK REQUIRED";
    return nxl_raw_state_name(s->state);
}
static const char *nxl_raw_status_action(const struct nxl_raw_status *s) {
    if(nxl_raw_can_rearm(s))return "CHECK RESOURCES";
    if(nxl_raw_can_prepare_full(s))return "VERIFY RESOURCES";
    if(!strcmp(s->reason,"FRONT_REINSTALL_REQUIRED"))return "REINSTALL PACKAGE";
    return nxl_raw_action(s->state);
}
static const char *nxl_raw_status_hint(const struct nxl_raw_status *s) {
    if(s->state==NXL_RAW_RETRYABLE&&(strstr(s->reason,"_IDENTITY_FAILED")||strstr(s->reason,"_VERSION_")))return "PROGRAM CHECK FAILED - INSTALL MATCHING PACKAGE";
    if(!strcmp(s->reason,"PIPELINE_NEEDS_BOOT_INITIALIZATION"))return "BOOT: TAKE PHOTO + ALBUM EDIT / RESIZE";
    if(!strcmp(s->reason,"PIPELINE_BANK2_CAPACITY_UNAVAILABLE"))return "NO WORKSPACE - CLOSE EDITOR / NEW TRY";
    if(!strcmp(s->reason,"OLD_SPLIT_JOB_START_NEW_PIPELINE"))return "CREATE NEW JOB FOR THE SINGLE PIPELINE";
    if(!strcmp(s->reason,"FRONT_NEEDS_STILL_INIT"))return "INITIALIZE STILL + JPEG AT BOOT, THEN RUN";
    if(nxl_raw_can_rearm(s))return "CHECK CURRENT RESOURCES - NO REBOOT ASSUMED";
    if(nxl_raw_can_prepare_full(s))return "VERIFY OLD HOLD - NEXT RAW RUN STARTS FRESH";
    if(!strcmp(s->reason,"FRONT_REINSTALL_REQUIRED"))return "FRONT GRANT SPENT - REINSTALL FULL PACKAGE";
    return nxl_raw_hint(s->state);
}
/* Parse only status emitted by the checked session manager, never evaluate it.
 * Required fields and terminator are unique; partial output cannot enable an action. */
static int nxl_raw_status_parse(const char *text,struct nxl_raw_status *out) {
    struct nxl_raw_status result;memset(&result,0,sizeof result);
    unsigned fields=0,line_number=0,ended=0;const char *p=text;
    while(*p) {
        char line[256];const char *end=strchr(p,'\n');if(!end || (size_t)(end-p)>=sizeof line)return 0;
        size_t n=(size_t)(end-p);memcpy(line,p,n);line[n]=0;p=end+1;line_number++;
        if(ended)return 0;
        if(line_number==1){if(strcmp(line,"NX51STATUS1"))return 0;continue;}
        if(!strcmp(line,"end=NX51END1")){ended=1;continue;}
        if(!strncmp(line,"version=",8)){if((fields&1)||strcmp(line+8,NXL_DEMO_VERSION))return 0;fields|=1;}
        else if(!strncmp(line,"state=",6)){if(fields&2)return 0;result.state=nxl_raw_state_for(line+6);if(!result.state)return 0;fields|=2;}
        else if(!strncmp(line,"front_verified=",15)){if((fields&4)||(strcmp(line+15,"0")&&strcmp(line+15,"1")))return 0;result.front_verified=line[15]=='1';fields|=4;}
        else if(!strncmp(line,"resume=",7)){if((fields&8)||(strcmp(line+7,"front")&&strcmp(line+7,"backend")&&strcmp(line+7,"none")))return 0;strcpy(result.resume,line+7);fields|=8;}
        else if(!strncmp(line,"look_id=",8)){if(fields&32)return 0;if(strcmp(line+8,"-") && strcmp(line+8,"raw422"))return 0;if(strcmp(line+8,"-"))strcpy(result.look_id,line+8);fields|=32;}
        else if(!strncmp(line,"strength=",9)){if(fields&64)return 0;const char *v=line+9;if(strcmp(v,"0")&&strcmp(v,"25")&&strcmp(v,"50")&&strcmp(v,"75")&&strcmp(v,"100"))return 0;result.strength=atoi(v);fields|=64;}
        else if(!strncmp(line,"reason=",7)){if((fields&16)||strlen(line+7)>=sizeof result.reason)return 0;for(const char *q=line+7;*q;q++)if((unsigned char)*q<32||(unsigned char)*q>126)return 0;strcpy(result.reason,line+7);fields|=16;}
    }
    if((fields&31)!=31||!ended)return 0;result.valid=1;*out=result;return 1;
}
/* Read-only display record; never an authorization to start hardware. */
struct nxl_progress_status { int valid; char phase[49],state[49],detail[1025],time[65]; };
static int nxl_progress_field(const char **p,const char *prefix,char *out,size_t capacity,int token) {
    const char *end=strchr(*p,'\n');size_t pre=strlen(prefix);
    if(!end || (size_t)(end-*p)<pre || strncmp(*p,prefix,pre))return 0;
    const char *begin=*p+pre;size_t n=(size_t)(end-begin);if(n>=capacity || (token&&!n))return 0;
    for(size_t i=0;i<n;i++) {
        unsigned char c=(unsigned char)begin[i];
        if(c<32 || c==127 || (token && !((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')))return 0;
    }
    memcpy(out,begin,n);out[n]=0;*p=end+1;return 1;
}
static int nxl_progress_status_parse(const char *text,struct nxl_progress_status *out) {
    struct nxl_progress_status result;memset(&result,0,sizeof result);
    const char *p=text;const char *header="NXDEMO51_PROGRESS1\n";
    if(strncmp(p,header,strlen(header)))return 0;p+=strlen(header);
    if(!nxl_progress_field(&p,"phase=",result.phase,sizeof result.phase,1) ||
       !nxl_progress_field(&p,"state=",result.state,sizeof result.state,1) ||
       !nxl_progress_field(&p,"detail=",result.detail,sizeof result.detail,0) ||
       !nxl_progress_field(&p,"time=",result.time,sizeof result.time,0) || !result.time[0] ||
       strcmp(p,"end=NXDEMO51_PROGRESS_END1\n"))return 0;
    result.valid=1;*out=result;return 1;
}
#define NXL_HELP_DELAY_MS 1500UL
struct nxl_help_state { unsigned long since; int visible, suppressed; };
static void nxl_help_reset(struct nxl_help_state *s, unsigned long now) {
    s->since=now; s->visible=0; s->suppressed=0;
}
static void nxl_help_close(struct nxl_help_state *s) { s->visible=0; s->suppressed=1; }
static int nxl_help_tick(struct nxl_help_state *s, unsigned long now, int held) {
    if (!held && !s->visible && !s->suppressed && now-s->since>=NXL_HELP_DELAY_MS) {
        s->visible=1; return 1;
    }
    return 0;
}
#endif
