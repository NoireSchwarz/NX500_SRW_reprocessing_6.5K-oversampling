/* SPDX-License-Identifier: AGPL-3.0-only. Fixed PC-verified experiment plan. */
#ifndef P48_CASE_H
#define P48_CASE_H
#include <stdint.h>
#include "p48_sources.h"
struct p48_case {const char *id;unsigned source,wb,cc,initial_iso;};
static const struct p48_case p48_cases[]={
 {"SELECTED_DCIM_NATIVE_WB_BLACK_SOURCE_CC",0,1,1,200}
};
#define P48_CASE_COUNT (sizeof p48_cases/sizeof p48_cases[0])
static unsigned p48_case_index=UINT32_MAX;
static const struct p48_case *p48_case;
static const struct p48_source *p48_source;
/* A selected file is pinned by a read-only descriptor before native work. */
static struct p48_source p48_selected_source;
static unsigned p48_selected_ready;
static int p48_select_case(const char *s){
 if(!s||!*s||s[0]<'0'||s[0]>'9'||s[1])return 0;
 unsigned n=(unsigned)(s[0]-'0');if(n>=P48_CASE_COUNT)return 0;
 p48_case_index=n;p48_case=&p48_cases[n];p48_source=p48_selected_ready?&p48_selected_source:&p48_sources[p48_case->source];return 1;
}
#endif
