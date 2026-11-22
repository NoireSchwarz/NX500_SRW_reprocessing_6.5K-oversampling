/* Optional camera-development exports; never a resource or DMA guard. */
#ifndef NXL_SRW_DIAGNOSTICS_H
#define NXL_SRW_DIAGNOSTICS_H
#include <stdlib.h>
#include <string.h>
static int nxl_srw_diagnostics(void){const char*p=getenv("NX51_SRW_DIAGNOSTICS");return p&&!strcmp(p,"1");}
#endif
