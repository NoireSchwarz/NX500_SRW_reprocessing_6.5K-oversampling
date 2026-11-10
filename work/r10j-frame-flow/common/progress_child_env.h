/* SPDX-License-Identifier: AGPL-3.0-only. Minimal native worker environment. */
#ifndef NXL_PROGRESS_CHILD_ENV_H
#define NXL_PROGRESS_CHILD_ENV_H
#include <stdio.h>
#include <string.h>
static unsigned nxl_progress_child_env(char*env[5],char*storage,size_t bytes,const char*operation){
 env[0]="PATH=/usr/sbin:/usr/bin:/sbin:/bin";env[1]="LC_ALL=C";env[2]="LD_BIND_NOW=1";env[3]=env[4]=NULL;
 if(operation&&strlen(operation)==32&&strspn(operation,"0123456789abcdef")==32){
  int n=snprintf(storage,bytes,"NX51_PROGRESS_TOKEN=%s",operation);if(n>0&&(size_t)n<bytes){env[3]=storage;return 4;}
 }return 3;
}
#endif
