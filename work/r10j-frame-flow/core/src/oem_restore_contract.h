/* SPDX-License-Identifier: AGPL-3.0-only. Data-only OEM liveview restore gates. */
#ifndef NX_OEM_RESTORE_CONTRACT_H
#define NX_OEM_RESTORE_CONTRACT_H
#include <stdint.h>
#include <string.h>
#define OEM_HANDLE 0x004aa2f0u
#define OEM_EXPECTED 0x004aa52cu
#define OEM_UI_RUNNING 0x0044b480u
#define OEM_REQUEST 0x8142aae8u
#define OEM_PROCESSOR 0x8142afdcu
#define OEM_VTABLE 0x805808d0u
#define OEM_IOCTL 0x2ab30u
#define OEM_CMD_START 27u
#define OEM_REQUIRED_STABLE 3u
struct oem_snapshot {uint32_t ui_running,expected,handle,processor,vtable,state,request,still_idle,dma_idle;};
struct oem_finish {uint32_t submitted,api_return_verified,stack_restored,code_restored,detached,api_inflight,fresh_log,stable;};
static int oem_processor_valid(uint32_t p){return p>=0x81000000u&&p<=0x93fff000u&&!(p&3u);}
static int oem_preflight(const struct oem_snapshot *s){return s->ui_running==1&&!s->expected&&s->handle&&oem_processor_valid(s->processor)&&s->vtable==OEM_VTABLE&&s->state==1&&!s->request&&s->still_idle&&s->dma_idle;}
static int oem_runtime_consistent(const struct oem_snapshot *s,const struct oem_snapshot *initial){return s->ui_running==1&&!s->expected&&s->handle==initial->handle&&s->processor==initial->processor&&s->vtable==OEM_VTABLE&&(s->state==1||s->state==2)&&s->request<=1;}
static int oem_live(const struct oem_snapshot *s){return s->ui_running==1&&!s->expected&&s->vtable==OEM_VTABLE&&s->state==2&&s->request==1;}
static int oem_completion(const struct oem_snapshot *s,const struct oem_snapshot *initial,const struct oem_finish *f){return oem_runtime_consistent(s,initial)&&oem_live(s)&&f->submitted==1&&f->api_return_verified==1&&f->stack_restored==1&&f->code_restored==1&&f->detached==1&&!f->api_inflight&&f->fresh_log==1&&f->stable>=OEM_REQUIRED_STABLE;}
static int oem_dma_idle(const uint32_t d[14]){
 /* 2/4/8 are retained native configuration values, not busy/pending flags. */
 static const unsigned active[]={0,1,3,5,6,7,9,10,11,12,13};
 for(unsigned i=0;i<sizeof active/sizeof *active;i++){if(d[active[i]])return 0;}
 return 1;
}
static unsigned oem_line_count(const char *s,const char *needle){unsigned n=0;const char *p=s;while((p=strstr(p,needle))){if(strchr(p,'\n'))n++;p+=strlen(needle);}return n;}
/* An error fallback can emit both start-completion and start-frame callbacks.
 * A callback after these failures is not evidence of real liveview frames. */
static int oem_start_failed(const char *s){
 return strstr(s,"LIVEVIEW ERROR")||strstr(s,"Send dummy callback")||strstr(s,"Start wait sem timeout")||
        (strstr(s,"_d5_isp_dvfs_set")&&strstr(s,"cmdq timeout"));
}
static int oem_log_conflict(const char *s){
 return oem_start_failed(s)||strstr(s,"LIVEVIEW_STOP_COMPLETION")||strstr(s,"LIVEVIEW_STOP_CALLBACK")||strstr(s,"(0x0000c104)")||strstr(s,"(0x0000c103)")||strstr(s,"NORM_LIVE >> E_LV_STATE_IDLE")||strstr(s,"NORM_LIVE >> IDLE")||oem_line_count(s,"LIVEVIEW_START_COMPLETION(0x0000c102)")>1||oem_line_count(s,"[CAP] SEQ << LIVE: CAPTURE_CALLBACK_ID_LIVEVIEW_START_COMPLETION")>1;
}
static int oem_fresh_completion(const char *s){
 return !oem_log_conflict(s)&&(oem_line_count(s,"LIVEVIEW_START_COMPLETION(0x0000c102)")||oem_line_count(s,"[CAP] SEQ << LIVE: CAPTURE_CALLBACK_ID_LIVEVIEW_START_COMPLETION"));
}
#endif
